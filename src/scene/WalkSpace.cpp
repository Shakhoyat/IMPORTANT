#include "scene/WalkSpace.h"

#include <algorithm>
#include <cmath>

#include "objects/Backdrop.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

constexpr float kStep = 0.45f;  // highest step a walker climbs

float floorY(const WalkFloor& f, float x, float z) {
    switch (f.slopeAxis) {
        case 1: return lerpf(f.y0, f.y1, clampf((x - f.x0) / (f.x1 - f.x0), 0.0f, 1.0f));
        case 2: return lerpf(f.y0, f.y1, clampf((z - f.z0) / (f.z1 - f.z0), 0.0f, 1.0f));
        default: return f.y0;
    }
}

bool inside(const WalkFloor& f, float x, float z) { return x >= f.x0 && x <= f.x1 && z >= f.z0 && z <= f.z1; }

// The floor region a walker at feetY stands on: highest one within a step above the feet and whose ceiling is
// still above the feet (so somebody walking on the earth roof is not pulled down into the rooms).
const WalkFloor* pick(const std::vector<WalkFloor>& floors, float x, float z, float feetY, float& y) {
    const WalkFloor* best = nullptr;
    for (const WalkFloor& f : floors) {
        if (!inside(f, x, z)) continue;
        const float fy = floorY(f, x, z);
        if (fy > feetY + kStep || feetY > f.ceiling - 0.3f) continue;
        if (!best || fy > y) {
            best = &f;
            y = fy;
        }
    }
    return best;
}

bool push(float& x, float& z, float radius, const Box& b) {
    const float cx = clampf(x, b.min.x, b.max.x), cz = clampf(z, b.min.z, b.max.z);
    float dx = x - cx, dz = z - cz;
    const float d2 = dx * dx + dz * dz;
    if (d2 >= radius * radius) return false;
    if (d2 > 1e-10f) {
        const float d = std::sqrt(d2), k = (radius - d) / d;
        x += dx * k;
        z += dz * k;
        return true;
    }
    // Centre inside the box: leave through the nearest face.
    const float l = x - b.min.x, r = b.max.x - x, n = z - b.min.z, f = b.max.z - z;
    const float m = std::min(std::min(l, r), std::min(n, f));
    if (m == l) x = b.min.x - radius;
    else if (m == r) x = b.max.x + radius;
    else if (m == n) z = b.min.z - radius;
    else z = b.max.z + radius;
    return true;
}

}  // namespace

bool WalkSpace::pickFloor(float x, float z, float feetY, float& y) const { return pick(floors_, x, z, feetY, y) != nullptr; }

float WalkSpace::floorHeight(float x, float z, float feetY) const {
    float y = 0.0f;
    if (pick(floors_, x, z, feetY, y)) return y;
    return terrainHeight(x, z);
}

float WalkSpace::ceilingHeight(float x, float z, float feetY) const {
    float y = 0.0f;
    const WalkFloor* f = pick(floors_, x, z, feetY, y);
    return f ? f->ceiling : 1e9f;
}

bool WalkSpace::collide(float& x, float& z, float feetY, float headY, float radius,
                        const std::vector<Box>& extra) const {
    bool hit = false;
    for (int pass = 0; pass < 2; ++pass) {  // two passes settle corners
        for (const auto* list : {&walls_, &extra}) {
            for (const Box& b : *list) {
                if (feetY >= b.max.y || headY <= b.min.y) continue;
                hit |= push(x, z, radius, b);
            }
        }
    }
    return hit;
}

bool WalkSpace::indoor(const Vec3& p) const {
    for (const Box& b : indoor_)
        if (p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y && p.z >= b.min.z && p.z <= b.max.z)
            return true;
    return false;
}

// ================================================================== v7.1 WP-T: open world on foot, spring-arm probe

namespace {

const std::vector<TreelinePine>& pines() {
    static const std::vector<TreelinePine> v = treelinePines();
    return v;
}
float pineRadius(const TreelinePine& p) { return 0.2f * p.height; }   // crown at chest height (base radius 0.28 h)

// A solid is a wall for a walker at feetY when it rises more than a step above the feet and reaches below the head.
bool isWall(const Box& b, float feetY, float height, float step) { return b.max.y > feetY + step && b.min.y < feetY + height; }

bool pushCircle(float& x, float& z, float radius, float cx, float cz, float r) {
    const float dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz, rr = radius + r;
    if (d2 >= rr * rr) return false;
    const float d = std::sqrt(d2);
    if (d < 1e-5f) { x = cx + rr; return true; }
    x = cx + dx * (rr / d);
    z = cz + dz * (rr / d);
    return true;
}

// Ray (origin, unit dir) against an AABB: entry distance, or -1 when it misses or starts inside.
float rayBox(const Vec3& o, const Vec3& d, const Vec3& mn, const Vec3& mx, float maxT) {
    float t0 = 0.0f, t1 = maxT;
    bool inside = true;
    const float oa[3] = {o.x, o.y, o.z}, da[3] = {d.x, d.y, d.z}, lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z};
    for (int a = 0; a < 3; ++a) {
        if (oa[a] < lo[a] || oa[a] > hi[a]) inside = false;
        if (std::fabs(da[a]) < 1e-9f) {
            if (oa[a] < lo[a] || oa[a] > hi[a]) return -1.0f;
            continue;
        }
        float ta = (lo[a] - oa[a]) / da[a], tb = (hi[a] - oa[a]) / da[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return -1.0f;
    }
    return inside ? -1.0f : t0;
}

}  // namespace

float groundHeight(const WalkSpace* ws, float x, float z, float feetY) {
    float fy = 0.0f;   // v8: floors of buildings on the apron (the barracks hall) count everywhere
    if (ws && ws->pickFloor(x, z, feetY, fy)) return fy;
    const float r = std::sqrt(x * x + z * z);
    if (r > layout::kBaseRadius) return layout::kApronLevel - kFarGroundGap;   // far ground beyond the terrain mesh
    if (r > kTerrainRadius) return terrainRimHeight();
    return ws ? ws->floorHeight(x, z, feetY) : terrainHeight(x, z);
}

void MapBounds::apply(float ox, float oz, float& nx, float& nz, float softStart, float hardEdge) {
    const float r1 = std::sqrt(nx * nx + nz * nz);
    if (r1 <= softStart) return;
    const float r0 = std::sqrt(ox * ox + oz * oz);
    const float ux = nx / r1, uz = nz / r1;
    const float dx = nx - ox, dz = nz - oz, outward = dx * ux + dz * uz;
    if (outward > 0.0f) {
        float k = clampf(1.0f - (std::max(r0, softStart) - softStart) / std::max(hardEdge - softStart, 1e-3f), 0.0f, 1.0f);
        k *= k;
        nx = ox + dx - ux * outward * (1.0f - k);
        nz = oz + dz - uz * outward * (1.0f - k);
    }
    const float r2 = std::sqrt(nx * nx + nz * nz);
    if (r2 > hardEdge) {
        nx *= hardEdge / r2;
        nz *= hardEdge / r2;
    }
}

float supportHeight(const WalkSpace* ws, const std::vector<Box>& solids, float x, float z, float feetY, const FootParams& p) {
    float g = groundHeight(ws, x, z, feetY);
    const float inset = p.radius * 0.4f;   // the centre must be over the top (feet half over an edge still stand)
    for (const Box& b : solids) {
        if (x < b.min.x - inset || x > b.max.x + inset || z < b.min.z - inset || z > b.max.z + inset) continue;
        if (b.max.y <= feetY + p.step && b.max.y > g) g = b.max.y;
    }
    return g;
}

void stepFoot(FootBody& b, const WalkSpace* ws, const std::vector<Box>& solids, float dx, float dz, bool jump, float dt,
              const FootParams& p) {
    b.jumped = b.landed = b.blocked = false;
    b.landSpeed = 0.0f;
    if (jump && b.grounded) {
        b.vy = p.jumpSpeed;
        b.grounded = false;
        b.jumped = true;
        b.airTime = 0.0f;
    }
    const float ox = b.x, oz = b.z;
    float nx = ox + dx, nz = oz + dz;
    if (p.bounds) MapBounds::apply(ox, oz, nx, nz);
    const float len = std::sqrt((nx - ox) * (nx - ox) + (nz - oz) * (nz - oz));
    const int n = std::max(1, static_cast<int>(std::ceil(len / 0.12f)));
    float cx = ox, cz = oz;
    const std::vector<Box> none;
    for (int i = 1; i <= n; ++i) {
        // Each sub-step moves on from where the previous one ended (a wall slide keeps its correction).
        const float tx = cx + (nx - ox) / static_cast<float>(n), tz = cz + (nz - oz) / static_cast<float>(n);
        float px = tx, pz = tz;
        for (int pass = 0; pass < 2; ++pass) {
            for (const Box& w : solids)
                if (isWall(w, b.feetY, p.height, p.step)) b.blocked |= push(px, pz, p.radius, w);
            if (ws)
                for (const Box& w : ws->walls())
                    if (isWall(w, b.feetY, p.height, p.step)) b.blocked |= push(px, pz, p.radius, w);
            if (p.treeline && px * px + pz * pz > 30.0f * 30.0f)
                for (const TreelinePine& t : pines()) b.blocked |= pushCircle(px, pz, p.radius, t.x, t.z, pineRadius(t));
        }
        cx = px;
        cz = pz;
        if (b.grounded) {
            const float f = supportHeight(ws, solids, cx, cz, b.feetY, p);
            if (f >= b.feetY - p.snapDown) {
                b.feetY = f;
            } else {   // walked off a ledge: fall
                b.grounded = false;
                b.vy = 0.0f;
                b.airTime = 0.0f;
            }
        }
    }
    if (p.bounds) {   // the wall push-out never leaves the map either
        float hx = cx, hz = cz;
        MapBounds::apply(cx, cz, hx, hz);
        cx = hx;
        cz = hz;
    }
    b.x = cx;
    b.z = cz;
    if (!b.grounded && dt > 0.0f) {
        const float before = b.feetY;
        b.airTime += dt;
        b.vy -= p.gravity * dt;
        b.feetY += b.vy * dt;
        const float ceil = ws ? ws->ceilingHeight(b.x, b.z, before) : 1e9f;
        if (b.vy > 0.0f && b.feetY + p.height > ceil) {   // head against the ceiling
            b.feetY = std::max(before, ceil - p.height);
            b.vy = 0.0f;
        }
        const float f = supportHeight(ws, solids, b.x, b.z, std::max(before, b.feetY), p);
        if (b.feetY <= f) {
            b.landSpeed = -b.vy;
            b.feetY = f;
            b.vy = 0.0f;
            b.grounded = true;
            b.landed = true;
        }
    }
}

float sphereCast(const WalkSpace* ws, const std::vector<Box>& solids, const Vec3& origin, const Vec3& dir, float maxLen,
                 float radius, float feetY) {
    float t = maxLen;
    const Vec3 e{radius, radius, radius};
    auto boxes = [&](const std::vector<Box>& list) {
        for (const Box& b : list) {
            const float h = rayBox(origin, dir, b.min - e, b.max + e, t);
            if (h >= 0.0f && h < t) t = h;
        }
    };
    boxes(solids);
    if (ws) boxes(ws->walls());
    // Treeline pines: vertical cylinders.
    if (origin.x * origin.x + origin.z * origin.z > 25.0f * 25.0f) {
        for (const TreelinePine& p : pines()) {
            const float r = pineRadius(p) + radius;
            const float fx = origin.x - p.x, fz = origin.z - p.z;
            const float a = dir.x * dir.x + dir.z * dir.z;
            if (a < 1e-8f || fx * fx + fz * fz < r * r) continue;
            const float bq = 2.0f * (fx * dir.x + fz * dir.z), c = fx * fx + fz * fz - r * r, disc = bq * bq - 4.0f * a * c;
            if (disc < 0.0f) continue;
            const float h = (-bq - std::sqrt(disc)) / (2.0f * a);
            if (h >= 0.0f && h < t && origin.y + dir.y * h < layout::kApronLevel + 0.8f * p.height) t = h;
        }
    }
    // Ground / floors and ceilings: march, then refine the first contact by bisection.
    auto blocked = [&](float s) {
        const Vec3 q = origin + dir * s;
        if (q.y - radius < groundHeight(ws, q.x, q.z, feetY)) return true;
        return ws && q.y + radius > ws->ceilingHeight(q.x, q.z, feetY);
    };
    if (blocked(0.0f)) return 0.0f;
    constexpr float kMarch = 0.1f;
    for (float s = kMarch; s < t + kMarch; s += kMarch) {
        const float sc = std::min(s, t);
        if (!blocked(sc)) continue;
        float lo = std::max(0.0f, sc - kMarch), hi = sc;
        for (int i = 0; i < 6; ++i) {
            const float mid = 0.5f * (lo + hi);
            (blocked(mid) ? hi : lo) = mid;
        }
        t = lo;
        break;
    }
    return std::max(0.0f, t);
}

}  // namespace ts
