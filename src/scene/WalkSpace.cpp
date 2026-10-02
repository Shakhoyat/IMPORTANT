#include "scene/WalkSpace.h"

#include <algorithm>
#include <cmath>

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

}  // namespace ts
