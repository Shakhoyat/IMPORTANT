#include "scene/Garrison.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <queue>

#include "objects/BaseLayout.h"
#include "objects/Bunker.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"
#include "scene/Door.h"
#include "scene/WalkSpace.h"

namespace ts {

namespace {

namespace L = layout;
constexpr float kF = L::kBunkerFloorY;
constexpr float kMinWalkSpeed = 0.25f;   // floor of the curvature speed law (hairpins are still walked)
constexpr float kLeafClearance = 0.2f;   // v9 M4: walkers keep this clear of a door leaf (the leaf stops at soldier::kBodyRadius)

float groundAt(float x, float z) {
    return std::sqrt(x * x + z * z) <= kTerrainRadius ? terrainHeight(x, z) : terrainRimHeight();
}

float boxDistance(float x, float z, const Box& b) {
    const float dx = std::fmax(std::fmax(b.min.x - x, 0.0f), x - b.max.x);
    const float dz = std::fmax(std::fmax(b.min.z - z, 0.0f), z - b.max.z);
    return std::sqrt(dx * dx + dz * dz);
}

float segDistance(float x, float z, float ax, float az, float bx, float bz) {
    const float vx = bx - ax, vz = bz - az;
    const float t = clampf(((x - ax) * vx + (z - az) * vz) / std::fmax(vx * vx + vz * vz, 1e-9f), 0.0f, 1.0f);
    const float dx = x - (ax + vx * t), dz = z - (az + vz * t);
    return std::sqrt(dx * dx + dz * dz);
}

// The inner fence: 20 posts every 18 deg from 99 deg, wire chords between them except in the gate bay (81..99).
float fenceDistance(float x, float z) {
    const float r = std::sqrt(x * x + z * z);
    if (std::fabs(r - kFenceRadius) > 3.0f) return 3.0f;
    float d = 3.0f;
    for (int i = 0; i + 1 < 20; ++i) {
        const float a0 = radians(99.0f + 18.0f * i), a1 = radians(99.0f + 18.0f * (i + 1));
        d = std::fmin(d, segDistance(x, z, kFenceRadius * std::cos(a0), kFenceRadius * std::sin(a0),
                                     kFenceRadius * std::cos(a1), kFenceRadius * std::sin(a1)));
    }
    return d;
}

// The entrance trench seen from the surface (walls 0.35 m outside the 1.5 m half width).
const Box kTrench{{-1.85f, -10.0f, 3.65f}, {1.85f, 10.0f, 13.45f}};
// Samples checked against the surface obstacles: not underground, not in the trench and not in the ramp mouth
// (the trench pseudo-box blocks surface routes but the bunker leg enters it from its top end on purpose).
bool surfaceSample(const Vec3& p) {
    if (p.y < -0.5f) return false;
    if (std::fabs(p.x) < 1.95f && p.z > 3.5f && p.z < 13.6f) return false;
    return !(std::fabs(p.x) < 1.2f && p.z >= 13.6f && p.z < 14.4f);
}

float wrapDeg(float a) {
    a = std::fmod(a + 180.0f, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a - 180.0f;
}

// v7 (WP-M): the held rifle sits in the right shoulder pocket and both hands are on it - right hand on the pistol
// grip, left hand on the handguard - through two-bone arm IK in the chest frame. The chest is bladed 20 deg (left
// shoulder forward, the head turned back by the same angle) so the support hand reaches; the muzzle stays on the
// pelvis heading. Low ready while walking (34 deg nose down), ready at the post (26 deg), lower when ducking.
void holdRifle(SoldierRig& rig, const Pose& pose, float moveW, bool duck) {
    namespace S = soldier;
    constexpr float kBlade = 20.0f;
    rig.chest->local.rotationDeg.y = pose.spineYaw - kBlade;
    rig.head->local.rotationDeg.y += kBlade;
    const float pitch = duck ? 42.0f : 26.0f + 8.0f * moveW;
    const Vec3 rot{pitch, kBlade, 0.0f};
    const Mat4 R = rotateEulerYXZ(rot);
    const Vec3 butt{-0.11f, 0.13f, 0.15f};                        // right shoulder pocket (chest frame)
    const Vec3 origin = butt - transformDirection(R, {0.0f, 0.0f, 1.0f}) * S::kRifleButtZ;
    rig.rifle->local.translation = origin;
    rig.rifle->local.rotationDeg = rot;
    const Mat4 M = translate(origin) * R;
    const Vec3 hand[2] = {transformPoint(M, S::kRifleHandguard), transformPoint(M, S::kRifleGrip)};   // [0] left, [1] right
    const Vec3 pole[2] = {{0.6f, -1.0f, -0.1f}, {-0.6f, -1.0f, -0.4f}};                              // elbows down and out
    for (int i = 0; i < 2; ++i) {
        const ArmIk k = armIK(rig.shoulder[i]->local.translation, hand[i], pole[i], S::kUpperArm, S::kForearm + S::kHandOffset);
        rig.shoulder[i]->local.rotationDeg = k.shoulderDeg;
        rig.elbow[i]->local.rotationDeg = {-k.elbowDeg, 0.0f, 0.0f};
    }
}

void collectLeafBounds(const SceneNode& n, std::vector<Box>& out) {
    if (n.isLeaf() && n.obstacle && n.visible) out.push_back(worldBounds(n.world));
    for (const auto& c : n.children) collectLeafBounds(*c, out);
}

}  // namespace

// ================================================================ site planner

namespace {
const int kDirI[8] = {1, 1, 0, -1, -1, -1, 0, 1};
const int kDirJ[8] = {0, 1, 1, 1, 0, -1, -1, -1};
}  // namespace

SitePlanner::Footprint SitePlanner::footprintOf(const Box& b) {
    Footprint f;
    f.bounds = b;
    f.hull = {{b.min.x, 0, b.min.z}, {b.max.x, 0, b.min.z}, {b.max.x, 0, b.max.z}, {b.min.x, 0, b.max.z}};
    return f;
}

SitePlanner::Footprint SitePlanner::footprintOf(const Mat4& world) {
    Footprint f;
    f.bounds = worldBounds(world);
    std::vector<Vec3> p;
    for (int k = 0; k < 8; ++k) {
        const Vec3 c = transformPoint(world, {k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, k & 4 ? 0.5f : -0.5f});
        p.push_back({c.x, 0.0f, c.z});
    }
    // convex hull (Andrew's monotone chain), counter-clockwise in (x, z)
    std::sort(p.begin(), p.end(), [](const Vec3& a, const Vec3& b) { return a.x < b.x || (a.x == b.x && a.z < b.z); });
    auto turn = [](const Vec3& o, const Vec3& a, const Vec3& b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); };
    std::vector<Vec3> h(16);
    int k = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && turn(h[static_cast<size_t>(k - 2)], h[static_cast<size_t>(k - 1)], p[i]) <= 1e-7f) --k;
        h[static_cast<size_t>(k++)] = p[i];
    }
    for (int i = static_cast<int>(p.size()) - 2, t = k + 1; i >= 0; --i) {
        while (k >= t && turn(h[static_cast<size_t>(k - 2)], h[static_cast<size_t>(k - 1)], p[static_cast<size_t>(i)]) <= 1e-7f) --k;
        h[static_cast<size_t>(k++)] = p[static_cast<size_t>(i)];
    }
    h.resize(static_cast<size_t>(std::max(1, k - 1)));
    f.hull = h;
    return f;
}

namespace {

// Plan distance from (x, z) to a convex polygon (0 inside).
float hullDistance(float x, float z, const SitePlanner::Footprint& f) {
    const std::vector<Vec3>& h = f.hull;
    if (h.size() < 3) return h.empty() ? 1e9f : std::hypot(x - h[0].x, z - h[0].z);
    bool inside = true;
    float d = 1e9f;
    for (size_t i = 0; i < h.size(); ++i) {
        const Vec3& a = h[i];
        const Vec3& b = h[(i + 1) % h.size()];
        if ((b.x - a.x) * (z - a.z) - (b.z - a.z) * (x - a.x) < 0.0f) inside = false;
        d = std::fmin(d, segDistance(x, z, a.x, a.z, b.x, b.z));
    }
    return inside ? 0.0f : d;
}

}  // namespace

float SitePlanner::distanceTo(const Footprint& f, float x, float z) { return hullDistance(x, z, f); }

void SitePlanner::build(const std::vector<Box>& solids) {
    std::vector<Footprint> parts;
    for (const Box& b : solids) parts.push_back(footprintOf(b));
    build(parts);
}

void SitePlanner::build(const std::vector<Footprint>& parts) {
    parts_.clear();
    for (const Footprint& f : parts) {
        const Box& b = f.bounds;
        const float cx = 0.5f * (b.min.x + b.max.x), cz = 0.5f * (b.min.z + b.max.z);
        if (std::fabs(cx) > kHalf + 3.0f || std::fabs(cz) > kHalf + 3.0f) continue;
        if (b.max.x - b.min.x > 30.0f || b.max.z - b.min.z > 30.0f) continue;   // ground discs, sky parts
        const float g = groundAt(clampf(cx, -kHalf, kHalf), clampf(cz, -kHalf, kHalf));
        if (b.max.y < g + 0.15f || b.min.y > g + 1.8f) continue;               // under the feet / above the head
        parts_.push_back(f);
    }
    parts_.push_back(footprintOf(kTrench));
    buckets_.assign(static_cast<size_t>(kCells * kCells), {});
    for (size_t k = 0; k < parts_.size(); ++k) {
        const Box& b = parts_[k].bounds;
        const int i0 = std::max(0, cellOf(b.min.x - kCap)), i1 = std::min(kCells - 1, cellOf(b.max.x + kCap));
        const int j0 = std::max(0, cellOf(b.min.z - kCap)), j1 = std::min(kCells - 1, cellOf(b.max.z + kCap));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) buckets_[static_cast<size_t>(idx(i, j))].push_back(static_cast<int>(k));
    }
    free_.assign(static_cast<size_t>(kCells * kCells), 0);
    for (int j = 0; j < kCells; ++j)
        for (int i = 0; i < kCells; ++i)
            free_[static_cast<size_t>(idx(i, j))] = clearance(centreOf(i), centreOf(j)) >= kPlanClearance ? 1 : 0;
    // A thin part can sit between two free cell centres: every move is checked along its segment.
    edges_.assign(static_cast<size_t>(kCells * kCells), 0);
    for (int j = 0; j < kCells; ++j)
        for (int i = 0; i < kCells; ++i) {
            if (!freeCell(i, j)) continue;
            for (int d = 0; d < 8; ++d) {
                const int di = kDirI[d], dj = kDirJ[d];
                if (!freeCell(i + di, j + dj)) continue;
                bool ok = true;
                for (int k = 1; k < 10 && ok; ++k) {
                    const float t = 0.1f * static_cast<float>(k);
                    ok = clearance(centreOf(i) + t * di, centreOf(j) + t * dj) >= kEdgeClearance;
                }
                if (ok) edges_[static_cast<size_t>(idx(i, j))] |= static_cast<unsigned char>(1u << d);
            }
        }
}

float SitePlanner::clearance(float x, float z) const {
    const int i = cellOf(x), j = cellOf(z);
    if (i < 0 || j < 0 || i >= kCells || j >= kCells || buckets_.empty()) return 0.0f;
    float d = kCap;
    for (int k : buckets_[static_cast<size_t>(idx(i, j))]) {
        const Footprint& f = parts_[static_cast<size_t>(k)];
        if (boxDistance(x, z, f.bounds) < d) d = std::fmin(d, hullDistance(x, z, f));
    }
    d = std::fmin(d, fenceDistance(x, z));
    d = std::fmin(d, (L::kPerimeterRadius - 0.9f) - std::sqrt(x * x + z * z));   // HESCO inner face + margin
    return std::fmax(d, 0.0f);
}

float SitePlanner::clearance(float x, float z, const std::vector<Vec3>& avoid) const {
    float d = clearance(x, z);
    for (const Vec3& a : avoid) d = std::fmin(d, std::fmax(0.0f, std::sqrt((x - a.x) * (x - a.x) + (z - a.z) * (z - a.z)) - a.y));
    return d;
}

int SitePlanner::freeCount() const {
    int n = 0;
    for (unsigned char f : free_) n += f;
    return n;
}

bool SitePlanner::gridPath(const Vec3& a, const Vec3& b, const std::vector<Vec3>& avoid, std::vector<Vec3>& cells) const {
    cells.clear();
    expanded_ = 0;
    if (free_.empty()) return false;
    auto ok = [&](int i, int j) {
        if (!freeCell(i, j)) return false;
        for (const Vec3& v : avoid)
            if (std::hypot(centreOf(i) - v.x, centreOf(j) - v.z) < v.y + 0.3f) return false;
        return true;
    };
    auto snap = [&](const Vec3& p, int& si, int& sj) {
        const int ci = cellOf(p.x), cj = cellOf(p.z);
        float best = 1e9f;
        for (int r = 0; r <= 6 && best > 1e8f; ++r)
            for (int j = cj - r; j <= cj + r; ++j)
                for (int i = ci - r; i <= ci + r; ++i) {
                    if (std::max(std::abs(i - ci), std::abs(j - cj)) != r || !ok(i, j)) continue;
                    const float d = std::hypot(centreOf(i) - p.x, centreOf(j) - p.z);
                    if (d < best) { best = d; si = i; sj = j; }
                }
        return best < 1e8f;
    };
    int si = 0, sj = 0, gi = 0, gj = 0;
    if (!snap(a, si, sj) || !snap(b, gi, gj)) return false;
    const int n = kCells * kCells;
    std::vector<float> gc(static_cast<size_t>(n), 1e30f);
    std::vector<int> from(static_cast<size_t>(n), -1);
    std::vector<unsigned char> closed(static_cast<size_t>(n), 0);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    auto h = [&](int i, int j) {   // octile distance
        const float dx = static_cast<float>(std::abs(i - gi)), dz = static_cast<float>(std::abs(j - gj));
        return std::fmax(dx, dz) + 0.41421356f * std::fmin(dx, dz);
    };
    gc[static_cast<size_t>(idx(si, sj))] = 0.0f;
    open.push({h(si, sj), idx(si, sj)});
    const int goal = idx(gi, gj);
    while (!open.empty()) {
        const int c = open.top().second;
        open.pop();
        if (closed[static_cast<size_t>(c)]) continue;
        closed[static_cast<size_t>(c)] = 1;
        ++expanded_;
        if (c == goal) break;
        const int ci = c % kCells, cj = c / kCells;
        for (int dir = 0; dir < 8; ++dir) {
                const int di = kDirI[dir], dj = kDirJ[dir];
                const int ni = ci + di, nj = cj + dj;
                if (!(edges_[static_cast<size_t>(c)] & (1u << dir)) || !ok(ni, nj)) continue;
                if (di && dj && (!ok(ci + di, cj) || !ok(ci, cj + dj))) continue;   // no corner cutting
                const int nk = idx(ni, nj);
                const float g = gc[static_cast<size_t>(c)] + (di && dj ? 1.41421356f : 1.0f);
                if (g < gc[static_cast<size_t>(nk)]) {
                    gc[static_cast<size_t>(nk)] = g;
                    from[static_cast<size_t>(nk)] = c;
                    open.push({g + h(ni, nj), nk});
                }
            }
    }
    if (!closed[static_cast<size_t>(goal)]) return false;
    std::vector<Vec3> rev;
    for (int c = goal; c >= 0; c = from[static_cast<size_t>(c)]) rev.push_back({centreOf(c % kCells), 0.0f, centreOf(c / kCells)});
    cells.push_back({a.x, 0.0f, a.z});
    for (auto it = rev.rbegin(); it != rev.rend(); ++it) cells.push_back(*it);
    cells.push_back({b.x, 0.0f, b.z});
    return true;
}

std::vector<int> SitePlanner::stringPull(const std::vector<Vec3>& chain, const std::vector<Vec3>& avoid) const {
    std::vector<int> out;
    const int n = static_cast<int>(chain.size());
    if (n < 3) {
        for (int k = 0; k < n; ++k) out.push_back(k);
        return out;
    }
    auto segClear = [&](const Vec3& a, const Vec3& b) {
        if (!surfaceSample(a) || !surfaceSample(b)) return false;
        const float len = std::hypot(b.x - a.x, b.z - a.z);
        const int m = std::max(1, static_cast<int>(std::ceil(len / 0.1f)));
        const float need = std::fmin(kPlanClearance - 0.04f, std::fmin(clearance(a.x, a.z, avoid), clearance(b.x, b.z, avoid)) + 0.02f);
        for (int k = 1; k < m; ++k) {
            const Vec3 p = lerp(a, b, static_cast<float>(k) / m);
            if (clearance(p.x, p.z, avoid) < need) return false;
        }
        return true;
    };
    int i = 0;
    out.push_back(0);
    while (i + 1 < n) {
        int j = i + 1;
        while (j + 1 < n && segClear(chain[static_cast<size_t>(i)], chain[static_cast<size_t>(j + 1)])) ++j;
        out.push_back(j);
        i = j;
    }
    return out;
}

float SitePlanner::minClearance(const CatmullRomPath& p, const std::vector<Vec3>& avoid) const {
    float worst = kCap;
    const float len = p.length();
    for (float s = 0.0f; s <= len; s += 0.1f) {
        const Vec3 q = p.pointAtDistance(s);
        if (surfaceSample(q)) worst = std::fmin(worst, clearance(q.x, q.z, avoid));
    }
    return worst;
}

CatmullRomPath SitePlanner::smooth(const std::vector<Vec3>& chain, std::vector<int> keep, bool closed,
                                   const std::vector<Vec3>& avoid) const {
    const int n = static_cast<int>(chain.size());
    for (int iter = 0; iter < 400; ++iter) {
        // drop kept points that crowd their predecessor (a near-duplicate makes a curvature spike / cusp)
        std::vector<Vec3> pts;
        for (size_t k = 0; k < keep.size(); ++k) {
            const Vec3& c = chain[static_cast<size_t>(keep[k])];
            const bool last = !closed && k + 1 == keep.size();
            if (!pts.empty() && length(c - pts.back()) < 0.3f && !last) continue;
            if (!pts.empty() && length(c - pts.back()) < 0.3f && last) pts.pop_back();
            pts.push_back(c);
        }
        if (closed && pts.size() > 3 && length(pts.back() - pts.front()) < 0.3f) pts.pop_back();
        CatmullRomPath path(pts, closed);
        if (static_cast<int>(keep.size()) >= n) return path;
        // first sample that comes too close to an obstacle (near the stops: as close as the stop itself)
        float failS = -1.0f;
        const float len = path.length();
        const Vec3 a = pts.front(), b = pts.back();
        for (float s = 0.0f; s <= len; s += 0.1f) {
            const Vec3 q = path.pointAtDistance(s);
            if (!surfaceSample(q)) continue;
            float need = kBodyClearance;
            const float da = std::hypot(q.x - a.x, q.z - a.z), db = std::hypot(q.x - b.x, q.z - b.z);
            if (!closed && (da < 0.6f || db < 0.6f)) need = std::fmin(need, std::fmin(clearance(a.x, a.z), clearance(b.x, b.z)) - 0.02f);
            if (clearance(q.x, q.z, avoid) < need) { failS = s; break; }
        }
        if (failS < 0.0f) return path;
        // put back the chain point nearest to the failure from the span of its segment (and its neighbours)
        const Vec3 q = path.pointAtDistance(failS);
        const int m = static_cast<int>(keep.size());
        const int seg = std::min(static_cast<int>(path.paramAtDistance(failS)), m - 1);
        int pick = -1;
        float bd = 1e30f;
        for (int span = seg - 1; span <= seg + 1; ++span) {
            if (!closed && (span < 0 || span >= m - 1)) continue;
            const int s0 = keep[static_cast<size_t>((span + m) % m)], s1 = keep[static_cast<size_t>((span + 1 + m) % m)];
            const int len2 = ((s1 - s0) % n + n) % n;
            for (int k = 1; k < len2; ++k) {
                const int ci = (s0 + k) % n;
                const float dd = length(chain[static_cast<size_t>(ci)] - q);
                if (dd < bd) { bd = dd; pick = ci; }
            }
        }
        if (pick < 0) break;
        keep.push_back(pick);
        if (closed) {
            // keep the loop order starting at keep[0]
            const int base = keep[0];
            std::sort(keep.begin() + 1, keep.end(), [&](int x, int y) { return ((x - base) % n + n) % n < ((y - base) % n + n) % n; });
        } else {
            std::sort(keep.begin(), keep.end());
        }
    }
    return CatmullRomPath(chain, closed);   // through every chain point (its grid edges are clear)
}

// ================================================================ duty programs

// Named (not anonymous) so GarrisonCore / Soldier::State may hold these types without subobject-linkage warnings.
namespace garrison_detail {

struct Stop {
    Vec3 p;                                   // standing point (surface) or the bunker branch end
    float yaw = 0.0f;                         // facing at the stop, degrees
    Activity act = Activity::Guard;
    WorkKind work = WorkKind::Inspect;
    float hold = 0.0f;                        // s; 0 = until the shift changes
    int branch = -1;                          // bunker branch (underground stop)
    Vec3 seat;                                // sit: seat point (feet floor level is taken from the WalkSpace)
    float seatH = 0.45f;
    int crate = 0;                            // +1 picks up, -1 sets down a crate at the end of the hold
    const char* label = "";
    // v8: settle onto 'seat' at the start of the hold (sit, lie down, get on the mat) and back up when leaving;
    // shared: an indoor spot of the barracks (not a post the other soldiers' routes keep clear of)
    bool settle = false, shared = false;
};

struct Program {
    std::vector<Stop> stops;                  // stop programs
    std::vector<Vec3> loop;                   // loop programs: closed patrol through these waypoints
    std::vector<float> pauseAt;               // loop: waypoint indices where the walker stops (hold 'pauseHold')
    float pauseHold = 8.0f;
    bool run = false;
    const char* summary = "";
    bool isLoop() const { return !loop.empty(); }
};

struct Route {
    CatmullRomPath path;
    bool ok = false;
};

// Bunker legs: the trunk from the surface to the tunnel junction, then one branch per indoor stop.
std::vector<Vec3> bunkerTrunk(const WalkSpace* walk) {
    auto ramp = [&](float z) {
        if (walk) return walk->floorHeight(0.0f, z, 0.0f);
        return lerpf(kF, terrainHeight(0.0f, L::kRampTopZ), clampf((z - L::kRampBottomZ) / (L::kRampTopZ - L::kRampBottomZ), 0.0f, 1.0f));
    };
    return {{0.0f, 0.0f, 16.2f}, {0.0f, ramp(14.0f), 14.0f}, {0.0f, ramp(12.0f), 12.0f}, {0.0f, ramp(9.0f), 9.0f},
            {0.0f, kF, 6.2f},     {0.0f, kF, 4.7f},           {0.0f, kF, 3.0f},           {0.0f, kF, 1.4f}};
}

std::vector<Vec3> bunkerBranch(int b) {
    switch (b) {
        // through the bunk room doorway (x -1.8 .. -1.5, z 0.9 .. 1.9) straight, then turn inside the room
        case 0: return {{-0.8f, kF, 1.4f}, {-1.65f, kF, 1.4f}, {-2.2f, kF, 1.5f}, {-2.5f, kF, 2.2f}, {-3.0f, kF, 2.3f}};     // mess bench
        case 1: return {{-0.8f, kF, 1.4f}, {-1.65f, kF, 1.4f}, {-2.2f, kF, 1.5f}, {-2.4f, kF, 2.45f}, {-3.55f, kF, 2.5f}};   // north bunk
        case 2: return {{-0.8f, kF, 1.4f}, {-1.65f, kF, 1.4f}, {-2.2f, kF, 1.3f}, {-2.2f, kF, 0.15f}, {-3.6f, kF, 0.1f}};    // south bunk
        default: return {{0.0f, kF, -0.4f}, {0.0f, kF, -2.0f}, {0.25f, kF, -3.6f}, {0.0f, kF, -5.25f}};   // command room
    }
}

constexpr int kBranches = 4;

}  // namespace garrison_detail
using namespace garrison_detail;

const char* dutyName(Duty d) {
    switch (d) {
        case Duty::Sentry: return "SENTRY";
        case Duty::Watch: return "WATCH";
        case Duty::Gunner: return "GUNNER";
        case Duty::Mechanic: return "MECHANIC";
        case Duty::Logistics: return "LOGISTICS";
        case Duty::Technician: return "TECHNICIAN";
        case Duty::Patrol: return "PATROL";
        case Duty::Runner: return "RUNNER";
        case Duty::Resident: return "BARRACKS";
    }
    return "?";
}

const char* activityName(Activity a) {
    switch (a) {
        case Activity::Idle: return "IDLE";
        case Activity::Guard: return "ON GUARD";
        case Activity::Work: return "WORKING";
        case Activity::Sit: return "OFF DUTY";
        case Activity::Walk: return "WALKING";
        case Activity::Run: return "RUNNING";
        case Activity::Wait: return "WAITING";
        case Activity::Duck: return "TAKING COVER";
        case Activity::Crouch: return "CROUCHED";
        case Activity::Wave: return "WAVING";
        case Activity::Sleep: return "SLEEPING";
        case Activity::PushUps: return "PUSH-UPS";
        case Activity::PullUps: return "PULL-UPS";
        case Activity::SitUps: return "SIT-UPS";
        case Activity::Talk: return "TALKING";
    }
    return "?";
}

// ================================================================ shared garrison state

struct GarrisonCore {
    std::vector<Soldier*> units;
    // per soldier (v8: the eight on duty + the residents): [i][0] night, [i][1] day
    std::vector<std::array<Program, 2>> programs;
    std::vector<std::array<std::vector<Route>, 2>> legRoutes;   // stop k -> k+1 (stop programs)
    std::vector<std::array<Route, 2>> loopRoute;
    std::vector<std::array<std::vector<float>, 2>> loopPause;   // arc lengths of the pauses
    std::vector<CatmullRomPath> allRoutes;
    SitePlanner planner;
    const WalkSpace* walk = nullptr;
    const std::vector<std::unique_ptr<Controllable>>* controllables = nullptr;
    std::vector<Box> staticBoxes;      // planner input (solids + vehicles), for the possessed walker's collisions
    std::vector<Box> dynamicBoxes;     // this frame: doors, moving vehicles
    std::vector<Box> routeBoxes;       // v9 M4: the part route walkers wait for / collide with (not the hinged leaves)
    std::vector<DoorKinematics*> doors;   // v9 M4: hinged doors (auto open for walkers, bodies block the leaf)
    std::vector<Box> plannedBoxes;     // dynamic obstacles at planning time (routes already avoid the parts)
    std::vector<Vec3> footfalls;
    uint32_t seed = 0;
    bool ready = false, day = false;
    float frameTime = -1e30f;
    int shiftChanges = 0;
    std::vector<int> order;            // staggered shift order
    int count() const { return static_cast<int>(programs.size()); }
    void resize(int n) {
        programs.resize(static_cast<size_t>(n));
        legRoutes.resize(static_cast<size_t>(n));
        loopRoute.resize(static_cast<size_t>(n));
        loopPause.resize(static_cast<size_t>(n));
        order.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) order[static_cast<size_t>(i)] = i;
    }

    // Any soldier (also another garrison's): not an obstacle to plan around.
    bool isUnit(const Controllable* c) const { return dynamic_cast<const Soldier*>(c) != nullptr; }
    // Posts of the other soldiers (both shifts) as discs to keep routes clear of them: (x, radius, z).
    std::vector<Vec3> extraAvoid;      // temporary discs (detour around a jam)
    std::vector<Vec3> avoidFor(int self) const {
        std::vector<Vec3> v = extraAvoid;
        for (int i = 0; i < count(); ++i) {
            if (i == self) continue;
            for (int d = 0; d < 2; ++d)
                for (const Stop& s : programs[static_cast<size_t>(i)][static_cast<size_t>(d)].stops)
                    if (s.branch < 0 && !s.shared) v.push_back({s.p.x, 0.45f, s.p.z});
        }
        return v;
    }
    float floorAt(float x, float z, float feetY) const {
        float fy = 0.0f;   // v8: building floors on the apron (barracks hall)
        if (walk && std::sqrt(x * x + z * z) > kTerrainRadius && walk->pickFloor(x, z, feetY, fy)) return fy;
        if (std::sqrt(x * x + z * z) > kTerrainRadius) return terrainRimHeight();
        return walk ? walk->floorHeight(x, z, feetY) : terrainHeight(x, z);
    }
    void frame(const WorldContext& w);
    void separate(float dt);
    Route plan(const Vec3& from, bool fromUnder, const Vec3& to, int toBranch, int self) const;
    Route planLoop(const std::vector<Vec3>& pts, int self) const;
};

namespace {

// Underground position -> polyline back to the surface entry (nearest bunker vertex first).
std::vector<Vec3> exitChain(const Vec3& from, const WalkSpace* walk) {
    const std::vector<Vec3> trunk = bunkerTrunk(walk);
    float best = 1e30f;
    int bestBranch = -2, bestK = 0;
    for (size_t k = 0; k < trunk.size(); ++k) {
        const float d = length(trunk[k] - from);
        if (d < best) { best = d; bestBranch = -1; bestK = static_cast<int>(k); }
    }
    for (int b = 0; b < kBranches; ++b) {
        const std::vector<Vec3> br = bunkerBranch(b);
        for (size_t k = 0; k < br.size(); ++k) {
            const float d = length(br[k] - from);
            if (d < best) { best = d; bestBranch = b; bestK = static_cast<int>(k); }
        }
    }
    std::vector<Vec3> out{from};
    if (bestBranch >= 0) {
        const std::vector<Vec3> br = bunkerBranch(bestBranch);
        for (int k = bestK; k >= 0; --k) out.push_back(br[static_cast<size_t>(k)]);
        bestK = static_cast<int>(trunk.size()) - 1;
    }
    for (int k = bestK; k >= 0; --k) out.push_back(trunk[static_cast<size_t>(k)]);
    return out;
}

}  // namespace

Route GarrisonCore::plan(const Vec3& from, bool fromUnder, const Vec3& to, int toBranch, int self) const {
    Route r;
    const std::vector<Vec3> avoid = avoidFor(self);
    const std::vector<Vec3> trunk = bunkerTrunk(walk);
    std::vector<Vec3> head, tail;   // fixed underground parts before / after the surface part
    Vec3 a{from.x, 0.0f, from.z}, b{to.x, 0.0f, to.z};
    if (fromUnder) {
        head = exitChain(from, walk);
        a = head.back();
        head.pop_back();
    }
    if (toBranch >= 0) {
        tail.assign(trunk.begin() + 1, trunk.end());
        const std::vector<Vec3> br = bunkerBranch(toBranch);
        tail.insert(tail.end(), br.begin(), br.end());
        b = trunk.front();
    }
    std::vector<Vec3> pts;
    if (fromUnder && toBranch >= 0) {
        // indoors -> indoors: back to the junction, then down the other branch
        pts = head;
        while (pts.size() > 1 && pts.back().z > 1.5f) pts.pop_back();   // stop at the tunnel junction
        const std::vector<Vec3> br = bunkerBranch(toBranch);
        pts.insert(pts.end(), br.begin(), br.end());
        r.path = CatmullRomPath(pts, false);
        r.ok = true;
        return r;
    }
    std::vector<Vec3> dense;
    if (!planner.gridPath(a, b, avoid, dense)) { dense = {a, b}; }
    // underground parts are kept as they are; the surface part is pulled and smoothed together with them
    std::vector<Vec3> chain = head;
    std::vector<int> keep;
    for (size_t k = 0; k < head.size(); ++k) keep.push_back(static_cast<int>(k));
    const int off = static_cast<int>(chain.size());
    chain.insert(chain.end(), dense.begin(), dense.end());
    for (int k : planner.stringPull(dense, avoid)) keep.push_back(off + k);
    for (const Vec3& t : tail) {
        keep.push_back(static_cast<int>(chain.size()));
        chain.push_back(t);
    }
    r.path = planner.smooth(chain, keep, false, avoid);
    r.ok = true;
    return r;
}

Route GarrisonCore::planLoop(const std::vector<Vec3>& pts, int self) const {
    Route r;
    const std::vector<Vec3> avoid = avoidFor(self);
    std::vector<Vec3> dense;
    for (size_t k = 0; k < pts.size(); ++k) {
        std::vector<Vec3> leg;
        if (!planner.gridPath(pts[k], pts[(k + 1) % pts.size()], avoid, leg)) { leg = {pts[k], pts[(k + 1) % pts.size()]}; }
        if (!dense.empty()) leg.erase(leg.begin());
        dense.insert(dense.end(), leg.begin(), leg.end());
    }
    std::vector<int> keep = planner.stringPull(dense, avoid);
    dense.pop_back();                                   // == the first point: the spline closes itself
    if (keep.back() >= static_cast<int>(dense.size())) keep.pop_back();
    r.path = planner.smooth(dense, keep, true, avoid);
    r.ok = true;
    return r;
}

void GarrisonCore::frame(const WorldContext& w) {
    footfalls.clear();
    // Doors, barrier arm, vehicles that moved: waited for by walkers, collided with by the possessed soldier.
    dynamicBoxes.clear();
    routeBoxes.clear();
    auto same = [](const Box& a, const Box& b) { return length(a.min - b.min) + length(a.max - b.max) < 0.05f; };
    if (controllables)
        for (const auto& c : *controllables)
            if (c && !isUnit(c.get())) {
                // v9 M4: a hinged door's leaf always collides with the possessed soldier (its boxes); route walkers do not
                // wait for its axis-aligned boxes (a half-open leaf's bounds cover a lot of floor): they use the exact
                // leaf instead (stepRoute: requests, waiting outside the sweep, never stepping into the leaf).
                if (dynamic_cast<const DoorPortal*>(c.get())) {
                    for (const Box& b : c->dynamicObstacles()) dynamicBoxes.push_back(b);
                    continue;
                }
                for (const Box& b : c->dynamicObstacles()) {
                    bool planned = false;
                    for (const Box& p : plannedBoxes) planned = planned || same(b, p);
                    if (!planned) {
                        dynamicBoxes.push_back(b);   // closed door, a vehicle that moved
                        routeBoxes.push_back(b);
                    }
                }
            }
    // v9 M4: every soldier's body can stop a swinging leaf (and keeps an auto-opened door from closing on it).
    if (!doors.empty()) {
        std::vector<DoorBody> bodies;
        for (const Soldier* u : units) bodies.push_back({{u->x_, u->feetY_, u->z_}, soldier::kBodyRadius, 1.8f, u->index_});
        for (DoorKinematics* d : doors) {
            d->setBodies(bodies);
            d->step(w.time, std::max(w.dt, 0.0f));   // the requests of the last tick; the scene's own update is then a no-op
        }
    }
    // Shift change: staggered departures (seeded order), 2.5 s apart.
    const bool d = dayShift(w.hour);
    if (ready && d != day) {
        day = d;
        for (Soldier* s : units) s->startShift(d, w.time);
    }
    separate(clampf(w.dt, 0.0f, 0.05f));
}

// ================================================================ soldier

struct Soldier::State {
    enum class Phase { Hold, Travel, Settle, Unsettle, Loop };
    Phase phase = Phase::Hold;
    const Program* prog = nullptr;
    int stop = 0;                 // stop index (stop programs) / next pause index (loop programs)
    Route route;
    float s = 0.0f;               // arc length on 'route'
    float holdT = 0.0f, holdDur = 0.0f;
    float animT = 0.0f;
    float shiftDelay = -1.0f;
    bool pendingDay = false;
    bool afterTravelLoop = false; // travel ends by joining the loop at loopS
    float loopS = 0.0f;
    float duckT = 1e9f;
    float moveW = 0.0f;           // gait weight (0 = station pose)
    float lateral = 0.0f, latTarget = 0.0f;
    bool yield = false, waitDoor = false, backOff = false;
    float blockedT = 0.0f;        // s the hard separation has held this walker
    float waitT = 0.0f;
    Vec3 settleFrom, settleTo;
    float settleT = 0.0f, settleYawFrom = 0.0f;
    Stop cur;                     // the stop being held
    float vmax = 1.3f;
    float prevPhase = 0.0f;
    bool wasPossessed = false;
    bool nextAfterUnsettle = false;   // v8: a timed settle hold ended: get up, then walk the planned leg on
    // v9 M3: possessed motor in sync with the body, the boxes around it (steps / walls, also the feet's ground), the
    // direction of travel relative to the heading (gait plane), per-leg hip roll and the pelvis drop of the foot IK
    bool motorLive = false;
    std::vector<Box> nearSolids;
    float travelDeg = 0.0f;
    float legRoll[2] = {0.0f, 0.0f};
    float pelvisDrop = 0.0f;
};

// Walkers keep apart. A lateral offset from the route passes soldiers ahead on the side away from them (both pass
// on their own +side when meeting head-on). A walker keeps its distance behind somebody walking the same way and
// waits for a soldier standing in its way (up to 6 s, then it squeezes by on the offset). Crossing / head-on: the
// predicted closest approach over the next 1.5 s decides; the higher roster number gives way and, when the other is
// already close and still coming, backs off along its own route (it came from there, so that space is clear).
void GarrisonCore::separate(float dt) {
    auto velocity = [](const Soldier* s) {
        const Soldier::State& st = *s->st_;
        if (s->possessed_) {
            const float yr = radians(s->yaw_);
            return Vec3{std::sin(yr), 0.0f, std::cos(yr)} * s->speed_;
        }
        if (!s->travelling() || st.route.path.empty()) return Vec3{};
        return st.route.path.tangentAtDistance(st.s) * s->speed_;
    };
    for (Soldier* me : units) {
        Soldier::State& st = *me->st_;
        st.latTarget = 0.0f;
        st.backOff = false;
        const bool moving = me->travelling() && !me->possessed_;
        if (!moving || st.route.path.empty()) {
            st.yield = false;
            continue;
        }
        const CatmullRomPath& path = st.route.path;
        const Vec3 p0 = path.pointAtDistance(st.s), t = path.tangentAtDistance(st.s);
        const Vec3 side{t.z, 0.0f, -t.x};
        const Vec3 vMe = t * std::fmax(me->speed_, 0.6f);   // intended velocity (also while waiting)
        float want = 0.0f;
        bool yield = false, back = false;
        for (Soldier* o : units) {
            if (o == me || std::fabs(o->feetY_ - me->feetY_) > 1.0f) continue;
            const Vec3 d{o->x_ - me->x_, 0.0f, o->z_ - me->z_};
            const float dist = std::hypot(d.x, d.z);
            if (dist > 3.5f) continue;
            const float ahead = dot(d, t), lateralGap = std::fabs(dot(d, side));
            // passing offset
            const Vec3 rel{o->x_ - p0.x, 0.0f, o->z_ - p0.z};
            const float latO = dot(rel, side), aheadO = dot(rel, t);
            if (aheadO > -0.6f && aheadO < 3.0f && std::fabs(latO - st.lateral) < 1.0f)
                want = latO > 0.05f ? std::fmin(want, latO - 0.9f) : std::fmax(want, latO + 0.9f);

            const Soldier::State& os = *o->st_;
            const bool oWalker = (o->travelling() && !os.route.path.empty()) || o->possessed_;
            const Vec3 vO = velocity(o);
            const Vec3 tO = oWalker && !o->possessed_ ? os.route.path.tangentAtDistance(os.s) : normalize(vO);
            if (oWalker && dot(tO, t) > 0.3f) {                       // same direction
                if (ahead > 0.1f && dist < 1.2f && lateralGap < 0.65f) yield = true;
                continue;
            }
            if (!oWalker) {                                            // standing / working / sitting
                if (ahead > 0.1f && dist < 0.8f && lateralGap < 0.6f && st.waitT < 6.0f) yield = true;
                continue;
            }
            // crossing or head-on: closest approach within 1.5 s at the intended speeds
            const Vec3 u = (oWalker ? (o->possessed_ ? vO : tO * std::fmax(o->speed_, 0.6f)) : Vec3{}) - vMe;
            const float uu = dot(u, u);
            const float tc = uu > 1e-6f ? clampf(-dot(d, u) / uu, 0.0f, 1.5f) : 0.0f;
            const Vec3 c = d + u * tc;
            const float dmin = std::hypot(c.x, c.z);
            const bool giveWay = o->possessed_ || me->index_ > o->index_;
            if (dmin < 0.75f && giveWay) {
                yield = true;
                const Vec3 vIntent = o->possessed_ ? vO : tO * std::fmax(o->speed_, 0.6f);
                const bool coming = dot(vIntent, d) < -0.05f;   // the other walks (or wants to walk) towards me
                if (dist < 1.1f && coming) back = true;
            } else if (!giveWay && dist < 0.8f && ahead > 0.0f && lateralGap < 0.6f) {
                yield = true;   // right of way, but the other is still backing out of the way
            }
        }
        st.latTarget = clampf(want, -0.8f, 0.8f);
        st.yield = yield;
        st.backOff = back;
    }
    (void)dt;
}

Soldier::Soldier(int index, Duty duty, const SoldierLook& look, GarrisonCore* g)
    : index_(index), duty_(duty), rig_(buildSoldier(index, look)), g_(g), st_(std::make_unique<State>()) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "SOLDIER %d %s", index + 1, dutyName(duty));
    name_ = buf;
    rig_.root->animation = "Duty route (A* -> string pulling -> centripetal Catmull-Rom), gait by FK + leg IK";
    if (duty == Duty::Resident)   // v8: off duty, rifle in the hall's rack (the renderer draws leaves: hide the parts)
        for (SceneNode* g : {rig_.rifle, rig_.rifleBack})
            if (g) {
                g->visible = false;
                for (auto& c : g->children) c->visible = false;
            }
}

Soldier::~Soldier() = default;

bool Soldier::waitingForDoor() const { return st_->waitDoor; }

bool Soldier::travelling() const {
    return st_->phase == State::Phase::Travel || (st_->phase == State::Phase::Loop && st_->holdDur <= 0.0f);
}

float Soldier::routeLeft() const {
    if (st_->phase != State::Phase::Travel) return 0.0f;
    return std::fmax(0.0f, st_->route.path.length() - st_->s);
}

float Soldier::routeCurvature() const {
    if (!travelling() || st_->route.path.empty()) return 0.0f;
    return st_->route.path.curvatureAtDistance(st_->s);
}

float Soldier::floorAt(float x, float z) const {
    return g_ ? g_->floorAt(x, z, feetY_) : terrainHeight(x, z);
}

void Soldier::place(float x, float z, float yawDeg, float feetY) {
    x_ = x;
    z_ = z;
    yaw_ = yawDeg;
    feetY_ = feetY > -1e8f ? feetY : std::sqrt(x * x + z * z) <= kTerrainRadius ? terrainHeight(x, z) : terrainRimHeight();
    feetY_ = floorAt(x, z);
    st_->phase = State::Phase::Hold;
    st_->holdDur = 0.0f;
    st_->cur = Stop{};
    st_->cur.act = Activity::Idle;
    activity_ = Activity::Idle;
    planted_[0] = planted_[1] = false;
}

bool Soldier::holding() const {
    return st_ && st_->phase == State::Phase::Hold && st_->cur.act != Activity::Sit && !st_->cur.settle && !possessed_;
}

void Soldier::setAboard(bool on) {
    if (on == aboard_) return;
    if (on) aboardScale_ = rig_.root->local.scale;
    else rig_.root->local.scale = aboardScale_;
    aboard_ = on;
}

// Leaves the current post for the first stop of the other shift's program (after the staggered delay).
void Soldier::startShift(bool day, float time) {
    (void)time;
    st_->pendingDay = day;
    int rank = 0;
    for (int i = 0; i < static_cast<int>(g_->order.size()); ++i)
        if (g_->order[static_cast<size_t>(i)] == index_) rank = i;
    st_->shiftDelay = 1.0f + 2.5f * static_cast<float>(rank);
}

void Soldier::resume(float time) {
    (void)time;
    // (Re)plan from wherever the soldier is to the current program position.
    State& s = *st_;
    const Program& p = *s.prog;
    const Vec3 here{x_, feetY_, z_};
    if (p.isLoop()) {
        const Route& loop = g_->loopRoute[index_][day_ ? 1 : 0];
        float bestS = 0.0f, bd = 1e30f;
        for (float q = 0.0f; q < loop.path.length(); q += 1.0f) {
            const Vec3 pt = loop.path.pointAtDistance(q);
            const float d = std::hypot(pt.x - x_, pt.z - z_);
            if (d < bd) { bd = d; bestS = q; }
        }
        s.loopS = bestS;
        if (bd < 0.6f && !underground()) {
            s.phase = State::Phase::Loop;
            s.route = loop;
            s.s = bestS;
            s.holdDur = 0.0f;
            return;
        }
        s.route = g_->plan(here, underground(), loop.path.pointAtDistance(bestS), -1, index_);
        s.afterTravelLoop = true;
    } else {
        const Stop& st = p.stops[static_cast<size_t>(s.stop)];
        s.route = g_->plan(here, underground(), st.p, st.branch, index_);
        s.afterTravelLoop = false;
    }
    s.s = 0.0f;
    s.phase = State::Phase::Travel;
    s.lateral = 0.0f;
}

void Soldier::nextStop(float time) {
    State& s = *st_;
    const Program& p = *s.prog;
    const int n = static_cast<int>(p.stops.size());
    const int from = s.stop;
    s.stop = (s.stop + 1) % n;
    const std::vector<Route>& legs = g_->legRoutes[index_][day_ ? 1 : 0];
    if (from < static_cast<int>(legs.size()) && legs[static_cast<size_t>(from)].ok &&
        std::hypot(legs[static_cast<size_t>(from)].path.points().front().x - x_, legs[static_cast<size_t>(from)].path.points().front().z - z_) < 0.4f &&
        !underground()) {
        s.route = legs[static_cast<size_t>(from)];
        s.s = 0.0f;
        s.phase = State::Phase::Travel;
        s.afterTravelLoop = false;
        s.lateral = 0.0f;
        return;
    }
    resume(time);
}

void Soldier::beginHold(float time) {
    (void)time;
    State& s = *st_;
    const Stop& st = s.prog->stops[static_cast<size_t>(s.stop)];
    s.cur = st;
    s.holdT = 0.0f;
    s.holdDur = st.hold;
    if (g_->seed) {   // seeded runs: +-20 % hold variety
        uint32_t h = g_->seed ^ (static_cast<uint32_t>(index_) * 0x9E3779B9u) ^ (static_cast<uint32_t>(time * 10.0f) * 0x85EBCA6Bu);
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
        s.holdDur *= 0.8f + 0.4f * static_cast<float>(h & 0xFFFF) / 65535.0f;
    }
    if (st.act == Activity::Sit || st.settle) {
        s.phase = State::Phase::Settle;
        s.settleT = 0.0f;
        s.settleFrom = {x_, feetY_, z_};
        s.settleTo = st.seat;
        s.settleYawFrom = yaw_;
    } else {
        s.phase = State::Phase::Hold;
    }
    activity_ = st.act;
    if (st.crate > 0) carrying_ = false;   // picks up at the end of the hold
}

// Route following: speed law v = vmax / (1 + 1.5 kappa) (minimum over the next 1.5 m), braking before the end
// and before pauses, waiting for closed doors / vehicles ahead and yielding to other soldiers.
void Soldier::stepRoute(const WorldContext& w, float dt) {
    State& s = *st_;
    const CatmullRomPath& path = s.route.path;
    const float len = path.length();
    const bool closed = path.closed();
    // braking target from the curvature ahead (2 m), hard limit from the curvature of the next 0.5 m
    float vT = s.vmax, vNow = s.vmax;
    for (float a = 0.0f; a <= 2.0f; a += a < 0.5f ? 0.05f : 0.25f) {
        const float v = std::fmax(kMinWalkSpeed, s.vmax / (1.0f + 1.5f * path.curvatureAtDistance(closed ? s.s + a : std::fmin(len, s.s + a))));
        vT = std::fmin(vT, std::sqrt(v * v + 2.0f * 2.5f * a));
        if (a <= 0.5f) vNow = std::fmin(vNow, v);
    }
    float toEnd = 1e9f;
    if (!closed) toEnd = len - s.s;
    else if (s.phase == State::Phase::Loop) {
        const std::vector<float>& pauses = g_->loopPause[index_][day_ ? 1 : 0];
        for (float ps : pauses) {
            float d = ps - s.s;
            if (d < 0.0f) d += len;
            toEnd = std::fmin(toEnd, d);
        }
    }
    vT = std::fmin(vT, std::sqrt(2.0f * 1.2f * std::fmax(0.0f, toEnd)) + 0.15f);
    // Look-ahead: dynamic obstacles (closed blast doors, a vehicle on the path).
    s.waitDoor = false;
    const Vec3 ahead = path.pointAtDistance(closed ? s.s + 0.7f : std::fmin(len, s.s + 0.7f));
    for (const Box& b : g_->routeBoxes) {
        if (b.min.y > feetY_ + 1.8f || b.max.y < feetY_ + 0.1f) continue;
        if (boxDistance(ahead.x, ahead.z, b) < 0.3f && boxDistance(x_, z_, b) < 1.6f) s.waitDoor = true;
    }
    // v9 M4: hinged doors on the way. A walker whose route crosses a doorway within the door's trigger radius (or who
    // stands in it) asks the door to open; it closes by itself closeDelay after the last request. While a leaf swings,
    // walkers outside the zone it sweeps wait there; a walker that a swinging leaf ran into (it stops against him) walks
    // on if his way leads away from the leaf, or steps back along his route if it leads into it.
    bool doorStop = false;
    for (DoorKinematics* d : g_->doors) {
        const Vec3 dc = d->doorwayCentre();
        const float reach = d->params().autoRadius;
        if (std::hypot(dc.x - x_, dc.z - z_) > reach + 0.5f) continue;
        bool want = d->inDoorway({x_, feetY_, z_}, 0.45f);
        Vec3 prev{x_, feetY_, z_};
        for (float a = 0.25f; !want && a <= reach; a += 0.25f) {
            const float sa = closed ? s.s + a : std::fmin(len, s.s + a);
            const Vec3 q = path.pointAtDistance(sa);
            if (d->crossesDoorway(prev, q)) want = true;
            prev = q;
            if (!closed && sa >= len) break;
        }
        if (want) d->request(w.time, index_);
        if (d->blockedBy() == index_) {
            const float a = d->angleDeg();
            doorStop = d->penetration({ahead, kLeafClearance, 1.8f, index_}, a) > d->penetration({{x_, feetY_, z_}, kLeafClearance, 1.8f, index_}, a);
        }
        // A swinging leaf (or one about to start): wait outside the zone it sweeps until it has latched on a stop.
        if (d->moving() && d->inSweep(ahead, 0.3f) && !d->inSweep({x_, feetY_, z_}, soldier::kBodyRadius)) s.waitDoor = true;
    }
    if (s.waitDoor || s.yield) vT = 0.0f;
    if (s.backOff && (closed || s.s > 0.05f)) vT = -0.6f;   // make room for an oncoming soldier
    if (doorStop && (closed || s.s > 0.05f)) vT = -0.5f;    // v9 M4: out of the leaf's way
    const float acc = vT > speed_ ? 1.6f : 4.0f;
    speed_ += clampf(vT - speed_, -acc * dt, acc * dt);
    speed_ = std::fmin(speed_, vNow);
    // curvature is only piecewise continuous (C1 spline): also respect the law exactly where this step ends
    for (int k = 0; k < 2 && speed_ > 0.0f; ++k) {
        const float sn = closed ? s.s + speed_ * dt : std::fmin(len, s.s + speed_ * dt);
        speed_ = std::fmin(speed_, std::fmax(kMinWalkSpeed, s.vmax / (1.0f + 1.5f * path.curvatureAtDistance(sn))));
    }
    const float sBefore = s.s;
    s.s += speed_ * dt;
    if (closed) s.s = std::fmod(s.s + len, len);
    else s.s = std::fmax(0.0f, s.s);
    s.waitT = (s.waitDoor || s.yield) ? s.waitT + dt : 0.0f;

    // position = path point + lateral offset (passing other soldiers), validated against the clearance
    s.lateral += (s.latTarget - s.lateral) * std::fmin(1.0f, 3.0f * dt);
    const Vec3 p = path.pointAtDistance(s.s), t = path.tangentAtDistance(s.s);
    const Vec3 side{t.z, 0.0f, -t.x};   // to the right of the tangent
    Vec3 q = p;
    if (surfaceSample(p)) {   // the largest part of the offset that keeps clear of the parts around
        for (float k = 1.0f; k > 0.0f; k -= 0.25f) {
            const Vec3 c = p + side * (s.lateral * k);
            if (g_->planner.clearance(c.x, c.z) >= SitePlanner::kBodyClearance) { q = c; s.lateral *= k; break; }
        }
        if (q.x == p.x && q.z == p.z) s.lateral = 0.0f;
    } else {
        q = p + side * s.lateral;   // ramp / bunker: the WalkSpace collision below keeps the offset off the walls
    }
    if (g_->walk && !surfaceSample(p))   // ramp / indoors: the WalkSpace walls (and closed doors) have the last word
        g_->walk->collide(q.x, q.z, feetY_, feetY_ + 1.8f, 0.22f, g_->routeBoxes);
    // v9 M4: never into a door leaf (the exact leaf slab, not its axis-aligned boxes): a step that would cut deeper into
    // a leaf than the body already is does not happen (the walker waits, the leaf stops against him or goes on).
    for (const DoorKinematics* d : g_->doors) {
        const float a = d->angleDeg();
        const float pNew = d->penetration({{q.x, feetY_, q.z}, kLeafClearance, 1.8f, index_}, a);
        if (pNew > 0.0f && pNew > d->penetration({{x_, feetY_, z_}, kLeafClearance, 1.8f, index_}, a) + 1e-5f) {
            s.s = sBefore;
            speed_ = 0.0f;
            s.blockedT += dt;
            return;
        }
    }
    // Hard separation: never step closer to a soldier who is already within 0.6 m (the yielding / back-off rules
    // resolve the encounter; this only guarantees that bodies never interpenetrate).
    for (const Soldier* o : g_->units) {
        if (o == this || std::fabs(o->feetY_ - feetY_) > 1.0f) continue;
        const float dNew = std::hypot(q.x - o->x_, q.z - o->z_), dOld = std::hypot(x_ - o->x_, z_ - o->z_);
        if (dNew < 0.6f && dNew < dOld) {
            s.s = sBefore;
            speed_ = 0.0f;
            s.blockedT += dt;
            return;
        }
    }
    s.blockedT = 0.0f;
    x_ = q.x;
    z_ = q.z;
    feetY_ = floorAt(x_, z_);
    if (t.x * t.x + t.z * t.z > 1e-6f && speed_ > 0.05f) {
        const float want = degrees(std::atan2(t.x, t.z));
        const float diff = wrapDeg(want - yaw_);
        yaw_ = wrapDeg(yaw_ + clampf(diff, -240.0f * dt, 240.0f * dt));
    }
    (void)w;
}

void Soldier::stepPossessed(const WorldContext& w, const ControlInput& in, float dt) {
    State& s = *st_;
    yaw_ = wrapDeg(yaw_ - in.turn * 110.0f * dt - in.aimYaw * 60.0f * dt - in.aimYawDelta);
    viewPitch_ = clampf(viewPitch_ + in.aimPitch * 50.0f * dt + in.aimPitchDelta, -60.0f, 60.0f);
    if (in.actionPressed) stance_ = (stance_ + 1) % 3;
    // v9 M3: the character motor (scene/CharacterMotor.h) moves the body: critically damped velocity, sprint / crouch
    // blends, coyote time + jump buffer, slope limit, step smoothing. Taken over or placed: it starts from rest here.
    const FootBody& mb = motor_.body();
    if (!s.motorLive || mb.x != x_ || mb.z != z_ || mb.feetY != feetY_) {
        motor_.reset(x_, z_, feetY_);
        s.motorLive = true;
    }
    MotorInput mi;
    mi.forward = in.forward;
    mi.strafe = in.strafe;
    mi.yawDeg = yaw_;
    mi.sprint = in.boost;
    mi.crouch = stance_ == 1;
    mi.jumpPressed = in.jump;
    // Collisions: WalkSpace walls, static solids around (low ones are steps / floors), doors / vehicles, the other
    // soldiers; terrain following, jump arc and landing, the treeline and the soft map bound (scene/WalkSpace.h).
    const Vec3 v = motor_.velocity();
    std::vector<Box>& extra = s.nearSolids;
    extra.clear();
    for (const Box& b : g_->staticBoxes)
        if (boxDistance(x_ + v.x * dt, z_ + v.z * dt, b) < 1.5f) extra.push_back(b);
    for (const Box& b : g_->dynamicBoxes) extra.push_back(b);
    for (const Soldier* o : g_->units)
        if (o != this) {
            const float r = soldier::kBodyRadius;
            extra.push_back({{o->x_ - r, o->feetY_, o->z_ - r}, {o->x_ + r, o->feetY_ + 1.8f, o->z_ + r}});
        }
    motor_.foot.radius = soldier::kBodyRadius;
    motor_.step(mi, g_->walk, extra, dt);
    const FootBody& b = motor_.body();
    x_ = b.x;
    z_ = b.z;
    feetY_ = b.feetY;
    vy_ = b.vy;
    airborne_ = !b.grounded;
    airT_ = b.airTime;
    if (motor_.jumped()) stance_ = 0;   // a jump stands up first
    if (motor_.landed()) {
        landSpeed_ = motor_.landSpeed();
        landDip_ = clampf(landSpeed_ / 6.0f, 0.3f, 1.0f);
        ++landings_;
    }
    // Actual ground speed (the animation's distance per tick), negative while moving backwards.
    const Vec3 m = motor_.moved();
    const float yr = radians(yaw_);
    const float along = dt > 0.0f ? (m.x * std::sin(yr) + m.z * std::cos(yr)) / dt : 0.0f;
    speed_ = std::copysign(motor_.groundSpeed(), along < -0.05f ? -1.0f : 1.0f);
    const bool crouched = stance_ == 1;
    activity_ = std::fabs(speed_) > 0.05f ? (motor_.sprintBlend() > 0.5f && !crouched ? Activity::Run : Activity::Walk)
                                           : (stance_ == 1 ? Activity::Crouch : stance_ == 2 ? Activity::Wave : Activity::Idle);
    s.phase = State::Phase::Hold;
    s.holdDur = 0.0f;
    (void)w;
}

float Soldier::drawnFeetY() const { return feetY_ + (possessed_ && st_->motorLive ? motor_.stepOffset() : 0.0f); }

void Soldier::update(const WorldContext& w, const ControlInput* in) {
    if (g_ && w.time != g_->frameTime) {
        g_->frameTime = w.time;
        g_->frame(w);
    }
    State& s = *st_;
    const float dt = clampf(w.dt, 0.0f, 0.05f);
    if (aboard_) {   // inside a vehicle: not drawn, no duty, no steps
        possessed_ = false;
        speed_ = 0.0f;
        rig_.root->local.scale = {0.001f, 0.001f, 0.001f};
        return;
    }
    s.animT += dt;
    s.duckT += dt;
    if (!g_ || !g_->ready || !s.prog) {
        animate(dt, w.time);
        return;
    }
    // shift change after the staggered delay
    if (s.shiftDelay >= 0.0f) {
        s.shiftDelay -= dt;
        if (s.shiftDelay < 0.0f) {
            day_ = s.pendingDay;
            s.prog = &g_->programs[index_][day_ ? 1 : 0];
            s.stop = 0;
            s.vmax = (s.prog->run ? GaitParams::run().speed : GaitParams::walk().speed) * (0.94f + 0.02f * static_cast<float>(index_ % 5));
            g_->shiftChanges++;
            if (!in) {
                if (s.phase == State::Phase::Hold && (activity_ == Activity::Sit || s.cur.settle)) {
                    s.phase = State::Phase::Unsettle;
                    s.settleT = 0.0f;
                    s.settleFrom = {x_, feetY_, z_};
                    s.settleTo = s.cur.p;
                } else {
                    resume(w.time);
                }
            }
        }
    }
    if (in) {
        possessed_ = s.wasPossessed = true;
        stepPossessed(w, *in, dt);
        animate(dt, w.time);
        return;
    }
    possessed_ = false;
    if (s.wasPossessed) {   // released: walk back to the duty program
        s.wasPossessed = false;
        stance_ = 0;
        airborne_ = false;   // v7.1 WP-T: released mid-jump: back on the floor
        vy_ = 0.0f;
        s.motorLive = false;   // v9 M3: the motor starts from rest at the next possession
        if (std::hypot(x_, z_) > L::kPerimeterRadius - 1.5f) {   // left outside the wall: back on the road inside the gate
            x_ = 0.8f;
            z_ = L::kPerimeterRadius - 2.5f;
        }
        feetY_ = floorAt(x_, z_);
        resume(w.time);
    }
    const bool ducking = s.duckT < 2.6f;
    switch (s.phase) {
        case State::Phase::Hold:
            speed_ = 0.0f;
            if (!ducking) s.holdT += dt;
            if (s.holdDur > 0.0f && s.holdT >= s.holdDur && s.shiftDelay < 0.0f) {
                if (s.cur.crate > 0) carrying_ = true;
                if (s.cur.crate < 0) carrying_ = false;
                if (s.cur.settle) {   // v8: off the bunk / mat / bench first, back where the leg to the next stop starts
                    s.phase = State::Phase::Unsettle;
                    s.settleT = 0.0f;
                    s.settleFrom = {x_, feetY_, z_};
                    s.settleTo = s.cur.p;
                    s.nextAfterUnsettle = true;
                    break;
                }
                nextStop(w.time);
            } else if (engaging_ && s.cur.act != Activity::Sit && !s.cur.settle) {   // laid on the intruder
                const float want = degrees(std::atan2(engageAt_.x - x_, engageAt_.z - z_));
                yaw_ = wrapDeg(yaw_ + clampf(wrapDeg(want - yaw_), -200.0f * dt, 200.0f * dt));
                activity_ = Activity::Guard;
            } else if (s.cur.act != Activity::Sit && !s.cur.settle) {
                const float diff = wrapDeg(s.cur.yaw - yaw_);
                yaw_ = wrapDeg(yaw_ + clampf(diff, -150.0f * dt, 150.0f * dt));
            }
            break;
        case State::Phase::Settle:
        case State::Phase::Unsettle: {
            speed_ = 0.0f;
            s.settleT += dt;
            const float k = clampf(s.settleT / 1.0f, 0.0f, 1.0f), e = k * k * (3.0f - 2.0f * k);
            x_ = lerpf(s.settleFrom.x, s.settleTo.x, e);
            z_ = lerpf(s.settleFrom.z, s.settleTo.z, e);
            feetY_ = floorAt(x_, z_);
            if (s.phase == State::Phase::Settle) {
                yaw_ = wrapDeg(s.settleYawFrom + wrapDeg(s.cur.yaw - s.settleYawFrom) * std::fmin(1.0f, 1.5f * k));
                if (k >= 1.0f) s.phase = State::Phase::Hold;
            } else if (k >= 1.0f) {
                activity_ = Activity::Idle;
                if (s.nextAfterUnsettle) {
                    s.nextAfterUnsettle = false;
                    nextStop(w.time);
                } else {
                    resume(w.time);
                }
            }
            break;
        }
        case State::Phase::Travel:
        case State::Phase::Loop: {
            if (s.phase == State::Phase::Loop && s.holdDur > 0.0f) {   // pause on the loop
                speed_ = 0.0f;
                if (!ducking) s.holdT += dt;
                activity_ = Activity::Guard;
                if (s.holdT >= s.holdDur) {
                    s.holdDur = 0.0f;
                    s.s += 0.05f;
                }
                break;
            }
            if (ducking) {
                speed_ = 0.0f;
                break;
            }
            // Jammed for 10 s (not at a door): plan a detour around the soldiers standing close by.
            if (s.waitT > 10.0f && !s.waitDoor && s.phase == State::Phase::Travel) {
                g_->extraAvoid.clear();
                for (const Soldier* o : g_->units)
                    if (o != this && std::hypot(o->x_ - x_, o->z_ - z_) < 5.0f) g_->extraAvoid.push_back({o->x_, 0.5f, o->z_});
                resume(w.time);
                g_->extraAvoid.clear();
                s.waitT = 0.0f;
            }
            const float before = s.s;
            stepRoute(w, dt);
            activity_ = (s.waitDoor || ((s.yield || s.blockedT > 0.0f) && std::fabs(speed_) < 0.05f)) ? Activity::Wait
                        : (s.prog->run ? Activity::Run : Activity::Walk);
            if (s.phase == State::Phase::Travel && s.s >= s.route.path.length() - 0.02f) {
                if (s.afterTravelLoop) {
                    s.phase = State::Phase::Loop;
                    s.route = g_->loopRoute[index_][day_ ? 1 : 0];
                    s.s = s.loopS;
                    speed_ = std::fmin(speed_, std::fmax(kMinWalkSpeed, s.vmax / (1.0f + 1.5f * s.route.path.curvatureAtDistance(s.s))));
                } else {
                    beginHold(w.time);
                }
            } else if (s.phase == State::Phase::Loop) {
                const float len = s.route.path.length();
                for (float ps : g_->loopPause[index_][day_ ? 1 : 0]) {
                    const bool crossed = before <= s.s ? (before < ps && ps <= s.s) : (before < ps || ps <= s.s);
                    if (crossed && len > 0.0f) {
                        s.holdDur = s.prog->pauseHold;
                        s.holdT = 0.0f;
                        speed_ = 0.0f;
                    }
                }
            }
            break;
        }
    }
    if (ducking && s.phase != State::Phase::Settle) activity_ = Activity::Duck;
    else if (activity_ == Activity::Duck) activity_ = s.phase == State::Phase::Hold ? s.cur.act : Activity::Guard;
    animate(dt, w.time);
}

// ---------------------------------------------------------------- animation (FK + leg IK)

void Soldier::animate(float dt, float time) {
    State& s = *st_;
    (void)time;
    const float t = s.animT + 1.7f * static_cast<float>(index_);
    // v9 M3 locomotion blend tree: idle -> walk -> run by the actual ground speed, towards the crouch walk by the motor's
    // crouch blend (scene/Gait.h locomotionGait). The phase advances by the distance travelled over the stride of the
    // current blend, so the stance foot moves back exactly at the body speed (no sliding at any speed).
    const float v = std::fabs(speed_);
    const bool motorOn = possessed_ && s.motorLive;
    const float crouchW = motorOn ? motor_.crouchBlend() : 0.0f;
    const GaitParams gp = locomotionGait(v, crouchW);
    const bool moving = v > 0.02f;
    s.moveW += clampf((moving ? clampf(v / 0.25f, 0.0f, 1.0f) : 0.0f) - s.moveW, -dt / 0.3f, dt / 0.3f);
    phase_ += v * dt / gp.stride;
    phase_ -= std::floor(phase_);
    const float yr = radians(yaw_);
    const Vec3 f{std::sin(yr), 0.0f, std::cos(yr)}, left{std::cos(yr), 0.0f, -std::sin(yr)};
    // Direction of travel relative to the heading: the gait runs in that plane (strafe, walk backwards).
    if (motorOn) {
        const Vec3 m = motor_.moved();
        if (m.x * m.x + m.z * m.z > 1e-10f) {
            const float want = degrees(std::atan2(dot(m, left), dot(m, f)));
            s.travelDeg = wrapDeg(s.travelDeg + wrapDeg(want - s.travelDeg) * (1.0f - std::exp(-dt / 0.08f)));
        }
    } else {
        s.travelDeg = speed_ < 0.0f ? 180.0f : 0.0f;
    }

    // station pose
    Pose station;
    const bool armed = !unarmed() && !rig_.rifleBack->visible;   // guards keep the rifle in their hands
    Activity a = activity_;
    if (a == Activity::Walk || a == Activity::Run || a == Activity::Wait) a = s.phase == State::Phase::Hold ? s.cur.act : Activity::Idle;
    switch (a) {
        case Activity::Guard: station = guardPose(t); break;
        case Activity::Work: station = workPose(s.cur.work, t); break;
        case Activity::Sit: {
            station = sitPose();
            station.pelvisY += s.cur.seatH - 0.45f;
            const float k = s.phase == State::Phase::Settle ? clampf(s.settleT, 0.0f, 1.0f)
                            : s.phase == State::Phase::Unsettle ? 1.0f - clampf(s.settleT, 0.0f, 1.0f) : 1.0f;
            station = blendPose(armed ? guardPose(t) : idlePose(t), station, k * k * (3.0f - 2.0f * k));
            break;
        }
        case Activity::Duck: station = duckPose(s.duckT); break;
        case Activity::Crouch: station = crouchPose(t); break;
        case Activity::Wave: station = wavePose(t); break;
        // v8 barracks life: lying / floor poses settle in over the settle second like sitting down
        case Activity::Sleep:
        case Activity::PushUps:
        case Activity::SitUps:
        case Activity::Talk: {
            station = a == Activity::Sleep ? sleepPose(t) : a == Activity::PushUps ? pushUpPose(t)
                      : a == Activity::SitUps ? sitUpPose(t) : talkPose(t, s.cur.settle);
            station.pelvisY += a == Activity::Talk ? (s.cur.settle ? s.cur.seatH - 0.45f : 0.0f) : s.cur.seatH;
            if (s.cur.settle) {
                const float k = s.phase == State::Phase::Settle ? clampf(s.settleT, 0.0f, 1.0f)
                                : s.phase == State::Phase::Unsettle ? 1.0f - clampf(s.settleT, 0.0f, 1.0f) : 1.0f;
                station = blendPose(idlePose(t), station, k * k * (3.0f - 2.0f * k));
            }
            break;
        }
        case Activity::PullUps: station = pullUpPose(t); break;
        default: station = armed ? guardPose(t) : idlePose(t); break;
    }
    if (activity_ == Activity::Duck) station = duckPose(s.duckT);
    if (motorOn && crouchW > 0.0f) {   // v9 M3: stand <-> crouch follows the motor's 0.2 s blend
        const Pose stand = a == Activity::Crouch ? (armed ? guardPose(t) : idlePose(t)) : station;
        station = blendPose(stand, crouchPose(t), crouchW);
    } else if (motorOn && a == Activity::Crouch) {
        station = armed ? guardPose(t) : idlePose(t);
    }

    // gait pose (arms, spine, pelvis bob; the legs are solved below)
    Pose g = gaitPose(gp, phase_);
    if (armed) {   // rifle at the low ready while walking
        g.shoulder[1] = 35.0f; g.elbow[1] = 78.0f; g.shoulder[0] = 52.0f; g.elbow[0] = 55.0f;
        g.spineYaw *= 0.3f;
    }
    if (carrying_) {
        g.shoulder[0] = g.shoulder[1] = 38.0f;
        g.elbow[0] = g.elbow[1] = 72.0f;
        g.spineYaw *= 0.3f;
    }
    pose_ = blendPose(station, g, s.moveW);
    if (motorOn) {   // v9 M3: the player's head looks where the player looks (no guard / idle head scan)
        pose_.headYaw = 0.0f;
        pose_.headPitch = clampf(-0.6f * viewPitch_, -30.0f, 30.0f);
    }
    // v7.1 WP-T: jump tuck (knees and hips bend in the air) and the landing dip.
    if (airborne_ || landDip_ > 0.0f) {
        const float tuck = airborne_ ? clampf(airT_ / 0.12f, 0.0f, 1.0f) : 0.0f;
        for (int i = 0; i < 2; ++i) {
            const float k = i == 0 ? 1.0f : 0.75f;   // one leg leads
            pose_.hip[i] += 40.0f * tuck * k;
            pose_.knee[i] += 72.0f * tuck * k;
            pose_.ankle[i] += 30.0f * tuck * k;
        }
        pose_.pelvisY -= 0.13f * landDip_;
        pose_.spinePitch += 8.0f * landDip_ + 6.0f * tuck;
        landDip_ = std::max(0.0f, landDip_ - dt / 0.25f);
    }

    // v9 M3 foot IK on the final pose (two-link legIK + hip roll): stance feet world-locked, swing feet on the gait's
    // path in the plane of travel, standing feet on the ground under them (slopes, steps); the pelvis drops when a foot
    // is out of reach and each foot pitches with the ground.
    const float rootY = drawnFeetY();
    const bool standing = (a == Activity::Idle || a == Activity::Guard || a == Activity::Wave) && crouchW < 0.5f &&
                          activity_ != Activity::Duck;
    const bool footIK = !airborne_ && (s.moveW > 0.0f || standing);
    FootParams feetProbe;
    feetProbe.radius = 0.0f;   // a foot stands on a step only when over it
    auto ground = [&](float px, float pz) {
        return motorOn ? supportHeight(g_ ? g_->walk : nullptr, s.nearSolids, px, pz, feetY_, feetProbe) : floorAt(px, pz);
    };
    if (footIK) {
        const float tr = radians(s.travelDeg);
        const Vec3 mdir = f * std::cos(tr) + left * std::sin(tr);
        const float reach = gp.thigh + gp.shin, hipH = GaitParams{}.hipHeight;
        Vec3 target[2];
        float toeDown[2];
        for (int leg = 0; leg < 2; ++leg) {
            const Vec3 side = left * ((leg == 0 ? 1.0f : -1.0f) * soldier::kHipHalfWidth);
            toeDown[leg] = (pose_.knee[leg] - pose_.hip[leg]) - pose_.ankle[leg];
            // station foot: FK of the station leg, put on the ground under it
            float sx = 0.0f, sy = 0.0f;
            legFK(radians(station.hip[leg]), radians(station.knee[leg]), gp.thigh, gp.shin, sx, sy);
            Vec3 S = Vec3{x_, 0.0f, z_} + side + f * sx;
            S.y = (standing ? ground(S.x, S.z) : rootY) + hipH + station.pelvisY + sy;
            // gait foot on its path (relative to the hip, along the direction of travel), over the ground under it
            float fx = 0.0f, fy = 0.0f;
            footInPelvis(gp, phase_, leg, fx, fy);
            Vec3 G = Vec3{x_, 0.0f, z_} + side + mdir * fx;
            G.y = ground(G.x, G.z) + fy + gp.hipHeight + g.pelvisY;
            if (moving && s.moveW >= 0.5f && inStance(gp, phase_, leg)) {
                if (!planted_[leg]) {
                    planted_[leg] = true;
                    lock_[leg] = G;
                    if (!underground()) g_->footfalls.push_back({G.x, G.y - kAnkleHeight, G.z});
                }
                target[leg] = lock_[leg];
                target[leg].y = G.y;   // heel rise / ground follow; the plan position stays locked
            } else {
                planted_[leg] = false;
                target[leg] = S + (G - S) * s.moveW;
            }
        }
        // Pelvis drop: the hips come down until the farther foot is in reach (at once), and rise back eased.
        float need = 0.0f;
        for (int leg = 0; leg < 2; ++leg) {
            const Vec3 hip = Vec3{x_, rootY + hipH + pose_.pelvisY, z_} + left * ((leg == 0 ? 1.0f : -1.0f) * soldier::kHipHalfWidth);
            const Vec3 d = target[leg] - hip;
            const float dist = length(d), over = dist - 0.995f * reach;
            if (over > 0.0f && -d.y > 0.1f) need = std::max(need, over * dist / -d.y);
        }
        need = std::min(need, 0.25f);
        s.pelvisDrop = need >= s.pelvisDrop ? need : need + (s.pelvisDrop - need) * std::exp(-dt / 0.08f);
        pose_.pelvisY -= s.pelvisDrop;
        for (int leg = 0; leg < 2; ++leg) {
            const Vec3 hip = Vec3{x_, rootY + hipH + pose_.pelvisY, z_} + left * ((leg == 0 ? 1.0f : -1.0f) * soldier::kHipHalfWidth);
            const Vec3 d = target[leg] - hip;
            const float sag = dot(d, f), lat = dot(d, left), down = std::sqrt(d.y * d.y + lat * lat);
            const float roll = d.y < -0.05f ? clampf(std::atan2(lat, -d.y), -0.6f, 0.6f) : 0.0f;
            float h = 0.0f, k = 0.0f;
            legIK(sag, -down, gp.thigh, gp.shin, h, k);
            if (planted_[leg] && std::sqrt(sag * sag + down * down) > reach) planted_[leg] = false;   // stretched: let go
            const Vec3 T = target[leg];
            const float slope = (ground(T.x + f.x * 0.12f, T.z + f.z * 0.12f) - ground(T.x - f.x * 0.12f, T.z - f.z * 0.12f)) / 0.24f;
            const float slopeDeg = clampf(degrees(std::atan(slope)), -25.0f, 25.0f);
            pose_.hip[leg] = degrees(h);
            pose_.knee[leg] = degrees(k);
            pose_.ankle[leg] = pose_.knee[leg] - pose_.hip[leg] - toeDown[leg] + slopeDeg;
            s.legRoll[leg] = degrees(roll);
        }
    } else {
        planted_[0] = planted_[1] = false;
        s.legRoll[0] = s.legRoll[1] = 0.0f;
        s.pelvisDrop = 0.0f;
    }

    // ---- write the joint transforms
    rig_.root->local.translation = {x_, rootY, z_};
    rig_.root->local.rotationDeg = {0.0f, yaw_, 0.0f};
    rig_.pelvis->local.translation = {0.0f, GaitParams{}.hipHeight + pose_.pelvisY, 0.0f};
    rig_.pelvis->local.rotationDeg = {pose_.bodyPitch, 0.0f, pose_.bodyRoll};   // v8: lying / floor exercises
    rig_.spine->local.rotationDeg = {pose_.spinePitch, 0.0f, pose_.pelvisRoll};
    rig_.chest->local.rotationDeg = {0.0f, pose_.spineYaw, 0.0f};
    rig_.head->local.rotationDeg = {pose_.headPitch, pose_.headYaw - pose_.spineYaw, 0.0f};
    for (int i = 0; i < 2; ++i) {
        const float sx = i == 0 ? 1.0f : -1.0f;
        if (std::fabs(s.legRoll[i]) > 0.01f) {
            // v9 M3: leg plane rolled about the forward axis, then pitched: R = Rz(roll) Rx(-hip) as Euler Y X Z
            const float r = radians(s.legRoll[i]), p = radians(-pose_.hip[i]);
            const Vec3 X{std::cos(r), std::sin(r), 0.0f};
            const Vec3 Y{-std::cos(p) * std::sin(r), std::cos(p) * std::cos(r), std::sin(p)};
            const Vec3 Z{std::sin(p) * std::sin(r), -std::sin(p) * std::cos(r), std::cos(p)};
            rig_.hip[i]->local.rotationDeg = eulerYXZFromBasis(X, Y, Z);
        } else {
            rig_.hip[i]->local.rotationDeg = {-pose_.hip[i], 0.0f, 0.0f};
        }
        rig_.knee[i]->local.rotationDeg = {pose_.knee[i], 0.0f, 0.0f};
        rig_.ankle[i]->local.rotationDeg = {-pose_.ankle[i], 0.0f, -s.legRoll[i]};
        rig_.shoulder[i]->local.rotationDeg = {-pose_.shoulder[i], 0.0f, sx * 6.0f};
        rig_.elbow[i]->local.rotationDeg = {-pose_.elbow[i], 0.0f, 0.0f};
    }
    if (armed && rig_.rifle->visible && activity_ != Activity::Wave && !carrying_)
        holdRifle(rig_, pose_, s.moveW, activity_ == Activity::Duck || activity_ == Activity::Crouch);
    rig_.crate->visible = carrying_;
    rig_.wrench->visible = activity_ == Activity::Work && s.cur.work == WorkKind::Wrench;
}

CameraHint Soldier::camera(bool firstPerson) const {
    CameraHint h;
    const float yr = radians(yaw_);
    const Vec3 f{std::sin(yr), 0.0f, std::cos(yr)};
    const Vec3 base{x_, drawnFeetY(), z_};
    const bool lying = activity_ == Activity::Sleep || activity_ == Activity::PushUps || activity_ == Activity::SitUps;
    float eyeH = activity_ == Activity::Crouch || activity_ == Activity::Duck ? 1.05f
                 : lying ? st_->cur.seatH + 0.35f
                 : activity_ == Activity::Sit || (activity_ == Activity::Talk && st_->cur.settle) ? 1.15f : soldier::kEyeHeight;
    // v9 M3 (possessed): the eye height follows the motor's crouch blend and dips with the landing (drawn feet: smooth
    // over steps)
    if (possessed_ && st_->motorLive && activity_ != Activity::Duck)
        eyeH = lerpf(soldier::kEyeHeight, 1.05f, motor_.crouchBlend()) - 0.13f * landDip_;
    if (firstPerson) {
        // v9 M3 (possessed): exactly the player's heading / pitch (mouse look stays direct)
        const bool own = possessed_ && st_->motorLive;
        const float hy = radians(yaw_ + (own ? 0.0f : pose_.headYaw)), pr = radians(viewPitch_ - (own ? 0.0f : pose_.headPitch * 0.5f));
        const Vec3 d{std::sin(hy) * std::cos(pr), std::sin(pr), std::cos(hy) * std::cos(pr)};
        h.eye = base + Vec3{0.0f, eyeH, 0.0f} + f * 0.12f;
        h.target = h.eye + d * 10.0f;
        h.fovDeg = 65.0f;
        return h;
    }
    const bool indoor = g_ && g_->walk && g_->walk->ceilingHeight(x_, z_, feetY_) < 1e8f;
    const float back = indoor ? 1.5f : 3.2f, up = indoor ? 1.55f : 2.1f;
    h.eye = base - f * back + Vec3{0.0f, up, 0.0f};
    h.target = base + f * 2.0f + Vec3{0.0f, 1.1f + 0.03f * viewPitch_, 0.0f};
    h.fovDeg = 58.0f;
    return h;
}

std::string Soldier::status() const {
    char buf[200];
    const char* label = st_->phase == State::Phase::Hold || st_->phase == State::Phase::Settle ? st_->cur.label : "";
    std::snprintf(buf, sizeof(buf), "%s  %s%s%s  %s SHIFT  SPEED %.1f M/S  HDG %03d%s", dutyName(duty_), activityName(activity_),
                  *label ? " AT " : "", label, day_ ? "DAY" : "NIGHT", std::fabs(speed_),
                  static_cast<int>(std::lround(yaw_ < 0.0f ? yaw_ + 360.0f : yaw_)) % 360, carrying_ ? "  CARRYING CRATE" : "");
    return buf;
}

std::string Soldier::debugState() const {
    const State& s = *st_;
    char buf[200];
    std::snprintf(buf, sizeof(buf), "phase %d s %.2f/%.2f closed %d yield %d back %d door %d blocked %.1f hold %.1f/%.1f lat %.2f->%.2f wait %.1f",
                  static_cast<int>(s.phase), s.s, s.route.path.length(), s.route.path.closed() ? 1 : 0, s.yield, s.backOff, s.waitDoor,
                  s.blockedT, s.holdT, s.holdDur, s.lateral, s.latTarget, s.waitT);
    return buf;
}

bool Soldier::onBlast(const Vec3& point, float power) {
    const float d = std::hypot(point.x - x_, point.z - z_);
    if (d > 14.0f + 8.0f * power) return false;
    if (st_->duckT > 0.6f) st_->duckT = 0.0f;   // drops into cover (no damage model: friendly personnel)
    return true;
}

// ================================================================ garrison

namespace {

constexpr Duty kDuties[Garrison::kSoldiers] = {Duty::Sentry, Duty::Watch, Duty::Gunner, Duty::Mechanic,
                                               Duty::Logistics, Duty::Technician, Duty::Patrol, Duty::Runner};

Stop guardAt(float x, float z, float yaw, float hold, const char* label) {
    Stop s;
    s.p = {x, 0.0f, z};
    s.yaw = yaw;
    s.act = Activity::Guard;
    s.hold = hold;
    s.label = label;
    return s;
}
Stop workAt(float x, float z, float yaw, WorkKind k, float hold, const char* label, int crate = 0) {
    Stop s = guardAt(x, z, yaw, hold, label);
    s.act = Activity::Work;
    s.work = k;
    s.crate = crate;
    return s;
}
Stop bunkerStop(int branch, Activity act, const char* label) {
    Stop s;
    s.branch = branch;
    s.p = bunkerBranch(branch).back();
    s.act = act;
    s.hold = 0.0f;
    s.label = label;
    switch (branch) {
        case 0: s.seat = {-3.0f, kF, 1.65f}; s.yaw = 180.0f; s.seatH = 0.42f; break;   // mess bench, facing the table
        case 1: s.seat = {-4.0f, kF, 2.5f}; s.yaw = 90.0f; s.seatH = 0.52f; break;     // north bunk edge
        case 2: s.seat = {-4.0f, kF, 0.3f}; s.yaw = 90.0f; s.seatH = 0.52f; break;     // south bunk edge
        default: s.yaw = 180.0f; s.work = WorkKind::Inspect; break;   // plotting table
    }
    return s;
}

// The duty roster. Coordinates: objects/BaseLayout.h, Launchers.h (camp sites), Environment.cpp (compound); every
// stop keeps >= 0.5 m from the parts around it and lies in the connected free area of the site grid (T205).
void makePrograms(std::vector<std::array<Program, 2>>& prog) {
    // 1 SENTRY: day beat between the inner gate and the gate road, night post at the inner gate
    prog[0][1].stops = {guardAt(-0.8f, 21.0f, 0.0f, 30.0f, "INNER GATE"), guardAt(-3.7f, 25.2f, 330.0f, 30.0f, "GATE ROAD")};
    prog[0][1].summary = "inner gate and gate road";
    prog[0][0].stops = {guardAt(-0.8f, 21.0f, 0.0f, 0.0f, "INNER GATE")};
    prog[0][0].summary = "inner gate";
    // 2 WATCH: day at the watchtower and the east wood, night in the sandbag guard post
    prog[1][1].stops = {guardAt(12.5f, 9.5f, 60.0f, 35.0f, "WATCHTOWER"), guardAt(10.5f, 1.5f, 90.0f, 25.0f, "EAST WOOD")};
    prog[1][1].summary = "watchtower and east wood look-out";
    prog[1][0].stops = {guardAt(5.2f, 14.0f, 0.0f, 0.0f, "SANDBAG POST")};
    prog[1][0].summary = "sandbag guard post";
    // 3 GUNNER: day patrol loop on the east apron, night in the machine-gun nest
    prog[2][1].loop = {{19.5f, 0.0f, 5.5f}, {20.0f, 0.0f, -8.0f}, {22.2f, 0.0f, -16.0f}, {28.5f, 0.0f, -12.0f}, {26.5f, 0.0f, -3.0f}, {25.5f, 0.0f, 0.8f}};
    prog[2][1].pauseAt = {2, 4};
    prog[2][1].summary = "patrol loop on the east apron";
    prog[2][0].stops = {guardAt(-10.0f, 10.5f, 135.0f, 0.0f, "MG NEST")};
    prog[2][0].summary = "machine-gun nest";
    // 4 MECHANIC: day at the south tank bay and the helicopter, night off duty (south bunk)
    prog[3][1].stops = {workAt(5.6f, -21.6f, 120.0f, WorkKind::Wrench, 30.0f, "MBT-2 ENGINE DECK"),
                        workAt(19.6f, 18.6f, 156.0f, WorkKind::Wrench, 25.0f, "HELICOPTER")};
    prog[3][1].summary = "wrench at the parked tank and the helicopter";
    prog[3][0].stops = {bunkerStop(2, Activity::Sit, "BUNK ROOM")};
    prog[3][0].summary = "bunk room (south bunk)";
    // 5 LOGISTICS: day crate loop depot <-> ammunition bunker, night off duty (mess bench)
    prog[4][1].stops = {workAt(-6.5f, -24.8f, 180.0f, WorkKind::CarryCrate, 6.0f, "FUEL DEPOT", +1),
                        workAt(10.5f, 21.0f, 0.0f, WorkKind::CarryCrate, 6.0f, "AMMO BUNKER", -1)};
    prog[4][1].summary = "carry crates from the depot to the ammunition bunker";
    prog[4][0].stops = {bunkerStop(0, Activity::Sit, "MESS BENCH")};
    prog[4][0].summary = "bunk room (mess table)";
    // 6 TECHNICIAN: day at the radar and the AA gun, night in the command room
    prog[5][1].stops = {workAt(24.7f, -4.8f, 67.0f, WorkKind::Inspect, 30.0f, "RADAR"),
                        workAt(23.1f, -18.1f, 247.0f, WorkKind::Inspect, 20.0f, "AA GUN PIT")};
    prog[5][1].summary = "inspect the radar and the AA gun";
    prog[5][0].stops = {bunkerStop(3, Activity::Work, "COMMAND ROOM")};
    prog[5][0].summary = "plotting table in the command room";
    // 7 PATROL: day loop inside the compound, night at the east guard tower
    prog[6][1].loop = {{-6.5f, 0.0f, 6.5f}, {-14.0f, 0.0f, 1.0f}, {-10.0f, 0.0f, -10.0f}, {2.0f, 0.0f, -10.5f}, {11.0f, 0.0f, -8.0f}, {9.8f, 0.0f, 7.3f}};
    prog[6][1].pauseAt = {2, 4};
    prog[6][1].summary = "patrol loop inside the compound";
    prog[6][0].stops = {guardAt(28.5f, -10.4f, 110.0f, 0.0f, "GUARD TOWER E")};
    prog[6][0].summary = "east guard tower";
    // 8 RUNNER: day running loop on the south apron, night off duty (north bunk)
    prog[7][1].loop = {{-8.0f, 0.0f, -21.0f}, {-1.0f, 0.0f, -30.5f}, {10.0f, 0.0f, -30.0f}, {16.0f, 0.0f, -22.5f}, {3.0f, 0.0f, -20.0f}};
    prog[7][1].pauseAt = {2};
    prog[7][1].pauseHold = 10.0f;
    prog[7][1].run = true;
    prog[7][1].summary = "running loop on the south apron";
    prog[7][0].stops = {bunkerStop(1, Activity::Sit, "BUNK ROOM")};
    prog[7][0].summary = "bunk room (north bunk)";
}

// ---- v8 barracks residents (objects/BaseLayout.h, layout::hall) ---------------------------------------------------
// Every spot belongs to one resident (no two arrive at the same bunk, mat or seat); the hold times vary by +-20 %
// per run seed, and each resident starts at a random point of its day, so the hall never plays the same loop twice.
Stop settleAt(Activity act, float standX, float standZ, float seatX, float seatZ, float yaw, float seatH, float hold,
              const char* label) {
    Stop s;
    s.p = {standX, 0.0f, standZ};
    s.seat = {seatX, 0.0f, seatZ};
    s.yaw = yaw;
    s.act = act;
    s.seatH = seatH;
    s.hold = hold;
    s.label = label;
    s.settle = true;
    s.shared = true;
    return s;
}
Stop spotAt(Activity act, float x, float z, float yaw, float hold, const char* label, WorkKind k = WorkKind::Inspect) {
    Stop s = guardAt(x, z, yaw, hold, label);
    s.act = act;
    s.work = k;
    s.shared = true;
    return s;
}

void makeResidentPrograms(std::vector<std::array<Program, 2>>& prog, int first, int count) {
    namespace H = layout::hall;
    constexpr float kAisleX = 21.5f;                     // free column in front of the bunks
    auto bunkSleep = [&](int frame, bool upper, float hold) {
        const float z = H::kBunkZ[frame];
        return settleAt(Activity::Sleep, kAisleX, z, H::kBunkX0 + 1.05f, z, 90.0f, upper ? H::kBunkHigh : H::kBunkLow, hold,
                        upper ? "UPPER BUNK" : "LOWER BUNK");
    };
    auto bunkEdge = [&](int frame, float hold) {
        const float z = H::kBunkZ[frame];
        return settleAt(Activity::Sit, kAisleX, z, H::kBunkX0 + H::kBunkLen - 0.3f, z, 90.0f, H::kBunkLow, hold, "BUNK");
    };
    auto seat = [&](bool north, int i, float hold) {
        const float x = H::kSeatX[i];
        return north ? settleAt(Activity::Talk, x, -9.5f, x, H::kBenchNZ, 180.0f, 0.45f, hold, "MESS TABLE")
                     : settleAt(Activity::Talk, x, -12.5f, x, H::kBenchSZ, 0.0f, 0.45f, hold, "MESS TABLE");
    };
    const Stop matPush = settleAt(Activity::PushUps, 23.5f, -13.4f, H::kMatX - 0.2f, H::kMatAZ, 90.0f, 0.02f, 28.0f, "GYM MAT");
    const Stop matSitUps = settleAt(Activity::SitUps, 23.5f, -12.6f, H::kMatX + 0.1f, H::kMatBZ, 90.0f, 0.02f, 30.0f, "GYM MAT");
    const Stop bar = spotAt(Activity::PullUps, H::kBarX, H::kBarZ, 0.0f, 16.0f, "PULL-UP BAR");
    const Stop locker0 = spotAt(Activity::Work, 20.2f, -9.85f, 0.0f, 14.0f, "LOCKERS");
    const Stop locker1 = spotAt(Activity::Work, 21.7f, -9.85f, 0.0f, 12.0f, "LOCKERS");
    const Stop ptBar = spotAt(Activity::PullUps, H::kPtBarX, H::kPtBarZ, 180.0f, 18.0f, "PT AREA");
    // outdoors, clear of the path from the door (x 23.5) along the north side of the hall
    const Stop ptPush1 = settleAt(Activity::PushUps, 20.4f, -6.0f, 20.4f, -6.0f, 270.0f, 0.0f, 30.0f, "PT AREA");
    const Stop ptPush2 = settleAt(Activity::PushUps, 21.0f, -4.8f, 21.0f, -4.8f, 270.0f, 0.0f, 25.0f, "PT AREA");
    const Stop chatA = spotAt(Activity::Talk, 22.5f, -5.0f, 90.0f, 40.0f, "PT AREA");
    const Stop chatB = spotAt(Activity::Talk, 23.5f, -5.0f, 270.0f, 45.0f, "PT AREA");
    const char* sum = "off duty in the barracks hall (bunks, PT, mess table)";
    for (int r = 0; r < count && r < Garrison::kMaxResidents; ++r) {
        std::array<Program, 2>& p = prog[static_cast<size_t>(first + r)];
        std::vector<Stop> day;
        switch (r) {
            case 0: day = {bunkSleep(0, false, 140.0f), seat(true, 0, 50.0f), locker0}; break;
            case 1: day = {matPush, seat(true, 1, 55.0f), bar, bunkEdge(1, 35.0f)}; break;
            case 2: day = {seat(false, 0, 60.0f), chatA, bunkSleep(2, false, 90.0f)}; break;
            case 3: day = {ptPush1, ptBar, seat(false, 1, 50.0f), matSitUps}; break;
            case 4: day = {bunkSleep(1, true, 160.0f), chatB}; break;
            default: day = {locker1, ptPush2, bunkSleep(2, true, 120.0f)}; break;
        }
        p[1].stops = day;
        p[1].summary = sum;
        const int frame = r % 3;
        p[0].stops = {bunkSleep(frame, r >= 3, 0.0f)};   // night: asleep in their own bunk
        p[0].summary = "asleep in the barracks hall";
    }
}

SoldierLook lookFor(int i, uint32_t seed, Duty d) {
    SoldierLook l;
    PropRng rng(seed ? seed ^ (0xA511E9B3u * static_cast<uint32_t>(i + 1)) : 0x51D0u + 977u * static_cast<uint32_t>(i));
    const float tint = rng.range(-0.03f, 0.03f);
    l.uniform = Vec3{0.29f, 0.31f, 0.22f} + Vec3{tint, tint * 0.8f, tint * 0.5f} + Vec3{rng.range(-0.02f, 0.02f), 0.0f, 0.0f};
    l.helmet = Vec3{0.26f, 0.28f, 0.20f} * rng.range(0.85f, 1.12f);
    const float sk = rng.range(0.0f, 1.0f);
    l.skin = lerp(Vec3{0.62f, 0.46f, 0.35f}, Vec3{0.36f, 0.25f, 0.18f}, sk);
    l.build = rng.range(0.93f, 1.07f);
    l.rifleSlung = d == Duty::Mechanic || d == Duty::Logistics || d == Duty::Technician || d == Duty::Runner;
    l.backpack = d != Duty::Runner && d != Duty::Mechanic && d != Duty::Resident;
    if (d == Duty::Resident) l.rifleSlung = true;   // the rifle node is hidden (in the rack) anyway
    return l;
}

}  // namespace

Garrison::Garrison() : impl_(std::make_unique<Impl>()) {}
Garrison::~Garrison() = default;

std::vector<std::unique_ptr<Controllable>> Garrison::build(uint32_t seed, int residents) {
    std::vector<std::unique_ptr<Controllable>> out;
    impl_->seed = seed;
    impl_->units.clear();
    residents = std::clamp(residents, 0, kMaxResidents);
    impl_->resize(kSoldiers + residents);
    makePrograms(impl_->programs);
    makeResidentPrograms(impl_->programs, kSoldiers, residents);
    for (int i = 0; i < kSoldiers + residents; ++i) {
        const Duty duty = i < kSoldiers ? kDuties[i] : Duty::Resident;
        auto s = std::make_unique<Soldier>(i, duty, lookFor(i, seed, duty), impl_.get());
        impl_->units.push_back(s.get());
        out.push_back(std::move(s));
    }
    return out;
}

void Garrison::init(const GarrisonContext& ctx) {
    Impl& g = *impl_;
    g.walk = ctx.walk;
    g.controllables = ctx.controllables;
    if (ctx.seed) g.seed = ctx.seed;
    g.staticBoxes.clear();
    if (ctx.solids) g.staticBoxes = *ctx.solids;
    std::vector<const SceneNode*> vehicleRoots;
    if (ctx.controllables)
        for (const auto& c : *ctx.controllables)
            if (c && !g.isUnit(c.get()) && c->root()) {
                collectLeafBounds(*c->root(), g.staticBoxes);
                vehicleRoots.push_back(c->root());
            }
    g.plannedBoxes.clear();
    g.doors.clear();
    std::vector<Box> doorRest;   // v9 M4: hinged door leaves on their holders: the routes keep clear of them
    if (ctx.controllables)
        for (const auto& c : *ctx.controllables)
            if (c && !g.isUnit(c.get())) {
                if (auto* portal = dynamic_cast<DoorPortal*>(c.get())) {
                    g.doors.push_back(&portal->door());
                    for (const Box& b : portal->door().restOpenBoxes()) doorRest.push_back(b);
                    continue;   // the leaf is never a planned (ignored) obstacle: it collides wherever it is
                }
                // The blast doors start shut but open for walkers: routes are planned through the gate, so a shut leaf is
                // always a dynamic obstacle that walkers wait for (the gate opens when they arrive).
                if (std::string(c->name()) == "BLAST DOORS") continue;
                for (const Box& b : c->dynamicObstacles()) g.plannedBoxes.push_back(b);
            }
    if (ctx.leaves) {
        // exact oriented footprints: static obstacle parts + every part of the vehicles / installations
        std::vector<SitePlanner::Footprint> parts;
        for (const SceneNode* n : *ctx.leaves) {
            if (!n->obstacle || !n->visible || n->primitive == PrimitiveType::BeamCone || n->shading == ShadingModel::Beam ||
                n->material.opacity < 1.0f)
                continue;   // light beams, rotor blur discs
            const SceneNode* top = n;
            while (top->parent) top = top->parent;
            const bool vehicle = std::find(vehicleRoots.begin(), vehicleRoots.end(), top) != vehicleRoots.end();
            if (n->dynamic && !vehicle) continue;
            if (n->object.rfind("Soldier ", 0) == 0) continue;
            parts.push_back(SitePlanner::footprintOf(n->world));
        }
        for (const Box& b : doorRest) parts.push_back(SitePlanner::footprintOf(b));
        g.planner.build(parts);
    } else {
        std::vector<Box> planBoxes = g.staticBoxes;
        planBoxes.insert(planBoxes.end(), doorRest.begin(), doorRest.end());
        g.planner.build(planBoxes);
    }
    // staggered shift order (seeded permutation; seed 0 = roster order)
    const int n = g.count();
    for (int i = 0; i < n; ++i) g.order[static_cast<size_t>(i)] = i;
    if (g.seed) {
        PropRng rng(g.seed ^ 0x5A1F7u);
        for (int i = n - 1; i > 0; --i)
            std::swap(g.order[static_cast<size_t>(i)], g.order[static_cast<size_t>(rng.next() * static_cast<float>(i + 1))]);
    }
    // plan every duty route once
    g.allRoutes.clear();
    for (int i = 0; i < n; ++i)
        for (int d = 0; d < 2; ++d) {
            const Program& p = g.programs[i][d];
            g.legRoutes[i][d].clear();
            g.loopPause[i][d].clear();
            if (p.isLoop()) {
                g.loopRoute[i][d] = g.planLoop(p.loop, i);
                const CatmullRomPath& lp = g.loopRoute[i][d].path;
                for (float k : p.pauseAt) {   // arc length nearest to the pause waypoint
                    const Vec3 w = p.loop[static_cast<size_t>(k)];
                    float bestS = 0.0f, bd = 1e30f;
                    for (float s = 0.0f; s < lp.length(); s += 0.25f) {
                        const Vec3 q = lp.pointAtDistance(s);
                        const float dd = std::hypot(q.x - w.x, q.z - w.z);
                        if (dd < bd) { bd = dd; bestS = s; }
                    }
                    g.loopPause[i][d].push_back(bestS);
                }
                g.allRoutes.push_back(lp);
            } else if (p.stops.size() > 1) {
                for (size_t k = 0; k < p.stops.size(); ++k) {
                    const Stop& a = p.stops[k];
                    const Stop& b = p.stops[(k + 1) % p.stops.size()];
                    g.legRoutes[i][d].push_back(g.plan(a.p, a.branch >= 0, b.p, b.branch, i));
                    g.allRoutes.push_back(g.legRoutes[i][d].back().path);
                }
            }
        }
    // the bunker legs (night) as routes too: from the day post to the indoor stop
    for (int i = 0; i < n; ++i) {
        const Program& night = g.programs[i][0];
        if (night.stops.empty() || night.stops[0].branch < 0) continue;
        const Program& day = g.programs[i][1];
        const Vec3 from = day.isLoop() ? day.loop[0] : day.stops[0].p;
        g.allRoutes.push_back(g.plan(from, false, night.stops[0].p, night.stops[0].branch, i).path);
    }

    // posts for the start hour
    g.day = dayShift(ctx.startHour);
    g.ready = true;
    PropRng rng(g.seed ? g.seed ^ 0x0BADF00Du : 0x1234567u);
    for (Soldier* s : g.units) {
        Soldier::State& st = *s->st_;
        const int i = s->index_;
        s->day_ = g.day;
        st.prog = &g.programs[i][g.day ? 1 : 0];
        st.vmax = (st.prog->run ? GaitParams::run().speed : GaitParams::walk().speed) * (0.94f + 0.02f * static_cast<float>(i % 5));
        st.shiftDelay = -1.0f;
        s->carrying_ = false;
        const float r = rng.next();
        if (st.prog->isLoop()) {
            st.route = g.loopRoute[i][g.day ? 1 : 0];
            st.s = r * st.route.path.length();
            const Vec3 p = st.route.path.pointAtDistance(st.s), t = st.route.path.tangentAtDistance(st.s);
            s->x_ = p.x;
            s->z_ = p.z;
            s->yaw_ = degrees(std::atan2(t.x, t.z));
            st.phase = Soldier::State::Phase::Loop;
            st.holdDur = 0.0f;
        } else {
            st.stop = static_cast<int>(r * static_cast<float>(st.prog->stops.size())) % static_cast<int>(st.prog->stops.size());
            const Stop& stop = st.prog->stops[static_cast<size_t>(st.stop)];
            const Vec3 at = stop.act == Activity::Sit || stop.settle ? stop.seat : stop.p;
            s->x_ = at.x;
            s->z_ = at.z;
            s->feetY_ = at.y < -1.0f ? at.y : 0.0f;
            s->yaw_ = stop.yaw;
            st.cur = stop;
            st.phase = Soldier::State::Phase::Hold;
            st.holdDur = stop.hold;
            st.holdT = rng.next() * stop.hold * 0.8f;
            s->activity_ = stop.act;
            if (stop.crate < 0) s->carrying_ = true;   // on the way to set one down
        }
        s->feetY_ = g.floorAt(s->x_, s->z_, s->feetY_ < -1.0f ? s->feetY_ : groundAt(s->x_, s->z_));
        s->animate(0.0f, 0.0f);
    }
}

void Garrison::update(const WorldContext& w) {
    for (Soldier* s : impl_->units) s->update(w, nullptr);
}

int Garrison::count() const { return static_cast<int>(impl_->units.size()); }
const Controllable* Garrison::soldier(int i) const { return impl_->units[static_cast<size_t>(i)]; }
Soldier* Garrison::unit(int i) { return impl_->units[static_cast<size_t>(i)]; }
const Soldier* Garrison::unit(int i) const { return impl_->units[static_cast<size_t>(i)]; }

std::string Garrison::roster(int i, bool day) const {
    if (i < 0 || i >= impl_->count()) return {};
    // "<kind>: <what>" with kind off duty / guard / work / walk / run (the workbook counts "off duty").
    const Program& p = impl_->programs[i][day ? 1 : 0];
    const char* kind = p.isLoop() ? (p.run ? "run" : "walk") : "guard";
    for (const Stop& st : p.stops) {
        if (st.act == Activity::Sit) kind = "off duty";
        else if (st.act == Activity::Work && std::string(kind) == "guard") kind = "work";
    }
    std::string s = std::string(kind) + ": " + p.summary;
    if (!p.stops.empty()) {
        s += " (";
        for (size_t k = 0; k < p.stops.size(); ++k) s += (k ? " -> " : "") + std::string(p.stops[k].label);
        s += ")";
    }
    return s;
}

std::vector<float> Garrison::routeLengths() const {
    std::vector<float> v;
    for (const CatmullRomPath& p : impl_->allRoutes) v.push_back(p.length());
    return v;
}

std::vector<Vec3> Garrison::footfalls() const { return impl_->footfalls; }

int Garrison::takeShiftChanges() {
    const int n = impl_->shiftChanges;
    impl_->shiftChanges = 0;
    return n;
}

const SitePlanner& Garrison::planner() const { return impl_->planner; }
const std::vector<CatmullRomPath>& Garrison::routes() const { return impl_->allRoutes; }

std::string Garrison::planJson() const {
    const GarrisonCore& g = *impl_;
    auto f = [](float v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.2f", v);
        return std::string(b);
    };
    auto poly = [&](const CatmullRomPath& r) {
        std::string o = "[";
        if (r.empty()) return o + "]";
        const float L = r.length();
        const int n = std::max(2, static_cast<int>(L / 0.5f) + 1);
        for (int k = 0; k < n; ++k) {
            const Vec3 q = r.pointAtDistance(L * static_cast<float>(k) / static_cast<float>(n - 1));
            o += (k ? ", [" : "[") + f(q.x) + ", " + f(q.z) + "]";
        }
        return o + "]";
    };
    std::string o = "{\"soldiers\": [";
    for (int i = 0; i < g.count(); ++i) {
        o += i ? ",\n        {" : "\n        {";
        o += "\"index\": " + std::to_string(i) + ", \"shifts\": [";
        for (int d = 0; d < 2; ++d) {
            const Program& p = g.programs[static_cast<size_t>(i)][static_cast<size_t>(d)];
            o += d ? ", {" : "{";
            o += std::string("\"shift\": \"") + (d ? "day" : "night") + "\", \"loop\": " + (p.isLoop() ? "true" : "false") + ", \"stops\": [";
            if (p.isLoop()) {
                for (size_t k = 0; k < p.loop.size(); ++k)
                    o += (k ? ", " : "") + std::string("{\"label\": \"\", \"p\": [") + f(p.loop[k].x) + ", " + f(p.loop[k].z) + "], \"underground\": false}";
            } else {
                for (size_t k = 0; k < p.stops.size(); ++k) {
                    const Stop& st = p.stops[k];
                    o += (k ? ", " : "") + std::string("{\"label\": \"") + st.label + "\", \"activity\": \"" + activityName(st.act) +
                         "\", \"p\": [" + f(st.p.x) + ", " + f(st.p.z) + "], \"underground\": " + (st.branch >= 0 ? "true" : "false") + "}";
                }
            }
            o += "], \"routes\": [";
            if (p.isLoop()) {
                o += poly(g.loopRoute[static_cast<size_t>(i)][static_cast<size_t>(d)].path);
            } else {
                const auto& legs = g.legRoutes[static_cast<size_t>(i)][static_cast<size_t>(d)];
                for (size_t k = 0; k < legs.size(); ++k) o += (k ? ", " : "") + poly(legs[k].path);
            }
            o += "]}";
        }
        o += "]}";
    }
    return o + "]}";
}
bool Garrison::dayActive() const { return impl_->day; }

}  // namespace ts
