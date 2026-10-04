#include "ai/HideController.h"

#include <algorithm>
#include <cstdlib>
#include <queue>

#include "objects/Rover.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

constexpr float kFreeRadius = 18.3f;       // stay inside the berm / perimeter fence (r = 19 m)
constexpr int kLosBuckets = 20;            // 2 m line-of-sight buckets over the grid square
constexpr float kInflate = 0.30f;          // planning margin around obstacles
constexpr float kBodyRadius = 0.15f;       // physical clearance used by the motion check
constexpr float kSensorForward = 0.31f;    // sensor bar distance ahead of the robot centre
constexpr float kSensorSpacing = 0.06f;
constexpr float kLineHalfWidth = 0.05f;    // virtual line is 10 cm wide
constexpr float kMaxSpeed = 0.75f;         // m/s
constexpr float kKp = 3.2f, kKd = 0.25f;   // PD steering gains
constexpr float kReplanPeriod = 0.4f;      // s
constexpr float kWheelAccel = 2.0f;        // m/s^2
const int kDx[4] = {1, 0, -1, 0};
const int kDz[4] = {0, 1, 0, -1};

std::vector<Vec3> chaikin(const std::vector<Vec3>& pts, int iterations) {
    std::vector<Vec3> cur = pts;
    for (int it = 0; it < iterations && cur.size() > 2; ++it) {
        std::vector<Vec3> next;
        next.push_back(cur.front());
        for (size_t i = 0; i + 1 < cur.size(); ++i) {
            next.push_back(lerp(cur[i], cur[i + 1], 0.25f));
            next.push_back(lerp(cur[i], cur[i + 1], 0.75f));
        }
        next.push_back(cur.back());
        cur = next;
    }
    return cur;
}

float approach(float value, float target, float maxStep) {
    if (value < target) return std::min(value + maxStep, target);
    return std::max(value - maxStep, target);
}

}  // namespace

bool HideController::allBlocked(const Vec3* points, int count, const Vec3& observer) const {
    for (int i = 0; i < count; ++i)
        if (!grid_.blocked(points[i], observer)) return false;
    return true;
}

// Exact footprint test: vertical segments through the robot's body (centre + 8 points on a circle of
// 'radius'), from just above the ground to mast height, against the occluder's real primitive shape.
// (A bounding box is far too coarse for thin diagonal parts such as the watchtower braces.)
static bool bodyHits(const Vec3& p, float radius, const Occluder& o) {
    const Box& b = o.bounds;
    if (b.min.y >= p.y + 0.8f) return false;  // low branches below 0.8 m block the robot and its mast
    if (p.x < b.min.x - radius || p.x > b.max.x + radius || p.z < b.min.z - radius || p.z > b.max.z + radius) return false;
    for (int k = 0; k < 9; ++k) {
        const float a = 0.7853982f * static_cast<float>(k);
        const Vec3 q = k == 8 ? p : p + Vec3{std::cos(a) * radius, 0.0f, std::sin(a) * radius};
        if (segmentHitsPrimitive(q + Vec3{0, 0.05f, 0}, q + Vec3{0, 0.8f, 0}, o)) return true;
    }
    return false;
}

bool HideController::collides(const Vec3& p) const {
    if (std::sqrt(p.x * p.x + p.z * p.z) > kFreeRadius + 0.2f) return true;
    for (const Occluder& o : occluders_)
        if (bodyHits(p, kBodyRadius, o)) return true;
    return false;
}

void HideController::worldToCell(const Vec3& p, int& cx, int& cz) {
    const float half = kCellSize * kGridCells * 0.5f;
    cx = std::clamp(static_cast<int>((p.x + half) / kCellSize), 0, kGridCells - 1);
    cz = std::clamp(static_cast<int>((p.z + half) / kCellSize), 0, kGridCells - 1);
}

Vec3 HideController::cellCentre(int cx, int cz) {
    const float half = kCellSize * kGridCells * 0.5f;
    const float x = -half + (static_cast<float>(cx) + 0.5f) * kCellSize;
    const float z = -half + (static_cast<float>(cz) + 0.5f) * kCellSize;
    return {x, terrainHeight(x, z), z};
}

bool HideController::cellFree(int cx, int cz) const {
    if (cx < 0 || cz < 0 || cx >= kGridCells || cz >= kGridCells) return false;
    return free_[index(cx, cz)] != 0;
}

void HideController::setStaticOccluders(const std::vector<Occluder>& occluders) {
    occluders_ = occluders;
    grid_.build(occluders_, kCellSize * kGridCells * 0.5f, kLosBuckets);
    for (int cz = 0; cz < kGridCells; ++cz) {
        for (int cx = 0; cx < kGridCells; ++cx) {
            const Vec3 c = cellCentre(cx, cz);
            bool ok = std::sqrt(c.x * c.x + c.z * c.z) < kFreeRadius;
            const Vec3 ground{c.x, terrainHeight(c.x, c.z), c.z};
            for (const Occluder& o : occluders_) {
                if (!ok) break;
                if (bodyHits(ground, kInflate, o)) ok = false;
            }
            free_[index(cx, cz)] = ok ? 1 : 0;
        }
    }
}

void HideController::computeVisibility(const Vec3& observer) {
    for (int cz = 0; cz < kGridCells; ++cz) {
        for (int cx = 0; cx < kGridCells; ++cx) {
            const int i = index(cx, cz);
            if (!free_[i]) {
                hidden_[i] = 0;
                continue;
            }
            // A robot parked here is hidden only if its whole body is out of sight: test its top and
            // four points around it (the camera would see any visible part).
            const Vec3 c = cellCentre(cx, cz);
            const Vec3 pts[5] = {c + Vec3{0, 0.36f, 0}, c + Vec3{0.25f, 0.2f, 0}, c + Vec3{-0.25f, 0.2f, 0},
                                 c + Vec3{0, 0.2f, 0.25f}, c + Vec3{0, 0.2f, -0.25f}};
            hidden_[i] = allBlocked(pts, 5, observer) ? 1 : 0;
        }
    }
}

void HideController::floodFill() {
    std::fill(flood_.begin(), flood_.end(), -1);
    std::queue<int> frontier;

    // Goals: hidden cells whose four neighbours are hidden too (deep in the shadow, not on its edge).
    auto deepHidden = [this](int cx, int cz) {
        if (!hidden_[index(cx, cz)]) return false;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDx[d], nz = cz + kDz[d];
            if (cellFree(nx, nz) && !hidden_[index(nx, nz)]) return false;
        }
        return true;
    };
    for (int pass = 0; pass < 2 && frontier.empty(); ++pass) {
        for (int cz = 0; cz < kGridCells; ++cz) {
            for (int cx = 0; cx < kGridCells; ++cx) {
                const bool goal = pass == 0 ? deepHidden(cx, cz) : hidden_[index(cx, cz)] != 0;
                if (goal) {
                    flood_[index(cx, cz)] = 0;
                    frontier.push(index(cx, cz));
                }
            }
        }
    }

    // Breadth-first expansion over free cells (4-neighbourhood, like the MicroMouse maze).
    while (!frontier.empty()) {
        const int cur = frontier.front();
        frontier.pop();
        const int cx = cur % kGridCells, cz = cur / kGridCells;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDx[d], nz = cz + kDz[d];
            if (!cellFree(nx, nz) || flood_[index(nx, nz)] >= 0) continue;
            flood_[index(nx, nz)] = flood_[cur] + 1;
            frontier.push(index(nx, nz));
        }
    }
}

std::vector<Vec3> HideController::descend(int cx, int cz) const {
    std::vector<Vec3> path;
    if (!cellFree(cx, cz) || flood_[index(cx, cz)] < 0) return path;
    int lastDir = -1;
    for (int guard = 0; guard < kGridCells * kGridCells && flood_[index(cx, cz)] > 0; ++guard) {
        // Steepest descent: the neighbour with the smallest value (BFS field: value - 1; weighted field: the
        // cheapest way on). Among equal values keep the current heading, so the route has few turns (like a
        // MicroMouse speed run).
        const int here = flood_[index(cx, cz)];
        int bestDir = -1, bestVal = here;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDx[d], nz = cz + kDz[d];
            if (!cellFree(nx, nz)) continue;
            const int v = flood_[index(nx, nz)];
            if (v < 0 || v >= here) continue;
            if (v < bestVal || (v == bestVal && d == lastDir)) {
                bestVal = v;
                bestDir = d;
            }
        }
        if (bestDir < 0) break;
        cx += kDx[bestDir];
        cz += kDz[bestDir];
        lastDir = bestDir;
        path.push_back(cellCentre(cx, cz));
    }
    return path;
}

float HideController::noiseAt(int cx, int cz) const {
    // Smooth value noise over the grid (cell size 4 cells), seeded per rover.
    auto h = [this](int x, int z) {
        uint32_t v = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed_ * 2246822519u;
        v = (v ^ (v >> 13)) * 1274126177u;
        return static_cast<float>((v ^ (v >> 16)) & 0xFFFF) / 65535.0f;
    };
    const float fx = cx / 4.0f, fz = cz / 4.0f;
    const int ix = static_cast<int>(std::floor(fx)), iz = static_cast<int>(std::floor(fz));
    float tx = fx - ix, tz = fz - iz;
    tx = tx * tx * (3 - 2 * tx);
    tz = tz * tz * (3 - 2 * tz);
    return lerpf(lerpf(h(ix, iz), h(ix + 1, iz), tx), lerpf(h(ix, iz + 1), h(ix + 1, iz + 1), tx), tz);
}

void HideController::floodFillToObjective(const std::vector<Vec3>& observers) {
    // Exposure: cells either UAV can see (computed from each observer, OR-ed).
    std::vector<unsigned char> exposed(free_.size(), 0);
    for (const Vec3& o : observers) {
        computeVisibility(o);
        for (size_t i = 0; i < exposed.size(); ++i) exposed[i] |= hidden_[i] ? 0 : (free_[i] ? 1 : 0);
    }
    // Dijkstra from the objective's cell: step cost (x16 fixed point) = 16 (one cell) + 40 if exposed + noise.
    std::fill(flood_.begin(), flood_.end(), -1);
    int gx, gz;
    worldToCell(objective_, gx, gz);
    nearestFreeCell(gx, gz);
    using Item = std::pair<int, int>;  // cost, cell
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    flood_[index(gx, gz)] = 0;
    open.push({0, index(gx, gz)});
    while (!open.empty()) {
        const auto [cost, cur] = open.top();
        open.pop();
        if (cost > flood_[cur]) continue;
        const int cx = cur % kGridCells, cz = cur / kGridCells;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDx[d], nz = cz + kDz[d];
            if (!cellFree(nx, nz)) continue;
            const int ni = index(nx, nz);
            const int step = 16 + (exposed[ni] ? 40 : 0) + static_cast<int>(24.0f * noiseAt(nx, nz));
            if (flood_[ni] < 0 || cost + step < flood_[ni]) {
                flood_[ni] = cost + step;
                open.push({flood_[ni], ni});
            }
        }
    }
}

void HideController::setObjective(const Vec3& objective, uint32_t seed) {
    objective_ = objective;
    seed_ = seed;
    infiltrate_ = true;
    replanTimer_ = kReplanPeriod;  // plan at the next update
}

void HideController::nearestFreeCell(int& cx, int& cz) const {
    if (cellFree(cx, cz)) return;
    for (int radius = 1; radius < kGridCells; ++radius) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != radius || !cellFree(cx + dx, cz + dz)) continue;
                cx += dx;
                cz += dz;
                return;
            }
        }
    }
}

void HideController::replan(const Vec3& observer, const RoverState& rover) {
    if (infiltrate_ && evadeTime_ <= 0.0f) {
        floodFillToObjective(observers_.empty() ? std::vector<Vec3>{observer} : observers_);
    } else {
        computeVisibility(observer);
        floodFill();
    }
    int cx, cz;
    worldToCell(rover.position, cx, cz);
    nearestFreeCell(cx, cz);
    line_.clear();
    const std::vector<Vec3> cells = descend(cx, cz);
    if (cells.empty()) return;  // already on a goal cell (or no route): stay still
    // The line starts under the robot and first runs along its heading, so the IR array begins on it.
    const Vec3 forward{std::sin(rover.yaw), 0.0f, std::cos(rover.yaw)};
    std::vector<Vec3> raw;
    raw.push_back(rover.position);
    raw.push_back(rover.position + forward * (kSensorForward + 0.1f));
    raw.insert(raw.end(), cells.begin(), cells.end());
    line_ = chaikin(raw, 3);
}

float HideController::distanceToLine(const Vec3& p) const {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < line_.size(); ++i) {
        const Vec3 a{line_[i].x, 0, line_[i].z}, b{line_[i + 1].x, 0, line_[i + 1].z}, q{p.x, 0, p.z};
        const Vec3 ab = b - a;
        const float len2 = dot(ab, ab);
        const float t = len2 > 1e-8f ? clampf(dot(q - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, length(q - (a + ab * t)));
    }
    return best;
}

float HideController::sideOfLine(const Vec3& p, const Vec3& left) const {
    // Signed lateral offset of the nearest remaining line point (+ = left of the robot).
    float best = 1e9f, side = 0.0f;
    for (const Vec3& q : line_) {
        const Vec3 d{q.x - p.x, 0.0f, q.z - p.z};
        const float dist = length(d);
        if (dist > 0.2f && dist < best) {
            best = dist;
            side = dot(d, left);
        }
    }
    return side;
}

void HideController::followLine(float dt, RoverState& rover) {
    const Vec3 forward{std::sin(rover.yaw), 0.0f, std::cos(rover.yaw)};
    const Vec3 left{std::cos(rover.yaw), 0.0f, -std::sin(rover.yaw)};

    float targetLeft = 0.0f, targetRight = 0.0f;
    const bool arrived = line_.empty() || length(Vec3{rover.position.x - line_.back().x, 0.0f,
                                                      rover.position.z - line_.back().z}) < 0.12f;
    if (arrived) {
        line_.clear();
        for (bool& s : rover.sensors) s = false;
    } else {
        // Read the IR array: sensor i sits at lateral offset x_i (positive = left of the robot).
        float sum = 0.0f;
        int active = 0;
        for (int i = 0; i < kIrSensorCount; ++i) {
            const float x = -0.12f + kSensorSpacing * static_cast<float>(i);
            const Vec3 s = rover.position + forward * kSensorForward + left * x;
            rover.sensors[i] = distanceToLine(s) < kLineHalfWidth;
            if (rover.sensors[i]) {
                sum += x;
                ++active;
            }
        }
        float error, speed;
        if (active > 0) {
            error = (sum / static_cast<float>(active)) / 0.12f;  // -1 (line far right) .. +1 (far left)
            speed = kMaxSpeed * (1.0f - 0.6f * std::fabs(error));
            lostTime_ = 0.0f;
        } else {
            // Line lost: pivot slowly towards the side of the line (like an LFR search sweep).
            error = sideOfLine(rover.position, left) >= 0.0f ? 1.0f : -1.0f;
            speed = 0.08f;
            lostTime_ += dt;
            if (lostTime_ > 0.8f) replanTimer_ = kReplanPeriod;  // give up and re-plan from here
        }
        const float omega = clampf(kKp * error + kKd * (error - lastError_) / std::max(dt, 1e-3f), -2.5f, 2.5f);
        lastError_ = error;
        targetLeft = speed - omega * kRoverTrackWidth * 0.5f;
        targetRight = speed + omega * kRoverTrackWidth * 0.5f;
    }

    rover.leftSpeed = approach(rover.leftSpeed, targetLeft, kWheelAccel * dt);
    rover.rightSpeed = approach(rover.rightSpeed, targetRight, kWheelAccel * dt);

    // Differential-drive kinematics.
    const float v = 0.5f * (rover.leftSpeed + rover.rightSpeed);
    const float w = (rover.rightSpeed - rover.leftSpeed) / kRoverTrackWidth;
    Vec3 next = rover.position + forward * (v * dt);
    if (!collides(next) || collides(rover.position)) {  // never drive into an obstacle (unless escaping one)
        rover.position = next;
        blockedTime_ = 0.0f;
    } else {
        // Blocked ahead: a differential drive can pivot on the spot towards the line; if it is still
        // blocked after a moment it backs away (bumper-and-reverse, as a real robot would).
        blockedTime_ += dt;
        const float turn = sideOfLine(rover.position, left) >= 0.0f ? 1.0f : -1.0f;
        const Vec3 back = rover.position - forward * (0.12f * dt);
        if (blockedTime_ > 1.2f && !collides(back)) {
            rover.leftSpeed = rover.rightSpeed = -0.12f;
            rover.position = back;
        } else {
            rover.leftSpeed = -0.18f * turn;
            rover.rightSpeed = 0.18f * turn;
        }
        replanTimer_ = kReplanPeriod;
    }
    rover.yaw += w * dt;
    rover.position.y = terrainHeight(rover.position.x, rover.position.z);
    rover.leftWheelAngle += rover.leftSpeed / kRoverWheelRadius * dt;
    rover.rightWheelAngle += rover.rightSpeed / kRoverWheelRadius * dt;
    rover.moving = std::fabs(v) > 0.01f || std::fabs(w) > 0.05f;
}

void HideController::update(float dt, const std::vector<Vec3>& observers, RoverState& rover) {
    observers_ = observers;
    // The nearest UAV (by ear): hide from it when it is close, otherwise keep infiltrating.
    Vec3 nearest = observers.empty() ? Vec3{0, 50, 0} : observers.front();
    float best = 1e9f;
    for (const Vec3& o : observers) {
        const float d = length(Vec3{o.x - rover.position.x, 0, o.z - rover.position.z});
        if (d < best) {
            best = d;
            nearest = o;
        }
    }
    const bool wasEvading = evadeTime_ > 0.0f;
    if (infiltrate_ && best < kEvadeRadius) evadeTime_ = kEvadeTime;
    else evadeTime_ = std::max(0.0f, evadeTime_ - dt);
    if (wasEvading != (evadeTime_ > 0.0f)) replanTimer_ = kReplanPeriod;  // mode change: re-plan now
    update(dt, nearest, rover);
}

void HideController::update(float dt, const Vec3& observer, RoverState& rover) {
    replanTimer_ += dt;
    if (replanTimer_ >= kReplanPeriod) {
        replanTimer_ = 0.0f;
        replan(observer, rover);
    }
    followLine(dt, rover);
    // Body sample points: motor box top and the four chassis corners.
    const Vec3 f{std::sin(rover.yaw), 0.0f, std::cos(rover.yaw)}, l{std::cos(rover.yaw), 0.0f, -std::sin(rover.yaw)};
    const Vec3 p = rover.position;
    const Vec3 body[5] = {p + Vec3{0, 0.36f, 0} - f * 0.14f, p + f * 0.3f + l * 0.2f + Vec3{0, 0.2f, 0},
                          p + f * 0.3f - l * 0.2f + Vec3{0, 0.2f, 0}, p - f * 0.3f + l * 0.2f + Vec3{0, 0.2f, 0},
                          p - f * 0.3f - l * 0.2f + Vec3{0, 0.2f, 0}};
    rover.hidden = allBlocked(body, 5, observer);
}

}  // namespace ts
