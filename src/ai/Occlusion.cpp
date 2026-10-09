#include "ai/Occlusion.h"

#include <algorithm>

#include "primitives/Library.h"

namespace ts {

namespace {

// Parameter interval [t0, t1] of the segment a + t*d where x^2 + y^2 (+ z^2 if withZ) <= r^2.
bool quadraticInterval(const Vec3& a, const Vec3& d, bool withZ, float r, float& t0, float& t1) {
    const float A = d.x * d.x + d.y * d.y + (withZ ? d.z * d.z : 0.0f);
    const float B = 2.0f * (a.x * d.x + a.y * d.y + (withZ ? a.z * d.z : 0.0f));
    const float C = a.x * a.x + a.y * a.y + (withZ ? a.z * a.z : 0.0f) - r * r;
    if (A < 1e-12f) {  // segment parallel to the axis (or a point): inside for all t, or never
        t0 = -1e9f;
        t1 = 1e9f;
        return C <= 0.0f;
    }
    const float disc = B * B - 4.0f * A * C;
    if (disc < 0.0f) return false;
    const float s = std::sqrt(disc);
    t0 = (-B - s) / (2.0f * A);
    t1 = (-B + s) / (2.0f * A);
    return true;
}

// v10 M3: exact test against a convex polyhedron given by its face planes (n . p <= d inside), Cyrus-Beck clipping of
// the segment a + t dir, t in [0, 1].
struct Plane {
    Vec3 n;
    float d;
};

bool segmentHitsConvex(const Vec3& a, const Vec3& dir, const std::vector<Plane>& planes) {
    float tEnter = 0.0f, tExit = 1.0f;
    for (const Plane& p : planes) {
        const float denom = dot(p.n, dir), num = p.d - dot(p.n, a);
        if (std::fabs(denom) < 1e-12f) {
            if (num < 0.0f) return false;   // parallel and outside this face
            continue;
        }
        const float t = num / denom;
        if (denom > 0.0f) tExit = std::min(tExit, t);
        else tEnter = std::max(tEnter, t);
        if (tEnter > tExit) return false;
    }
    return true;
}

// Face planes of the flat library primitives, read from the scene meshes (one plane per face, a corner on it).
std::vector<Plane> planesOf(PrimitiveType type) {
    std::vector<Plane> planes;
    const MeshData m = sceneMesh(type);
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        if (i > 0 && length(m.vertices[i].normal - m.vertices[i - 1].normal) < 1e-5f) continue;   // same face
        planes.push_back({m.vertices[i].normal, dot(m.vertices[i].normal, m.vertices[i].position)});
    }
    return planes;
}

const std::vector<Plane>& facePlanes(PrimitiveType type) {
    static const std::vector<Plane> wedge = planesOf(PrimitiveType::Wedge), taper = planesOf(PrimitiveType::TaperedBox),
                                    pyramid = planesOf(PrimitiveType::Pyramid);   // thread-safe one-time init
    return type == PrimitiveType::Wedge ? wedge : type == PrimitiveType::TaperedBox ? taper : pyramid;
}

}  // namespace

Box worldBounds(const Mat4& world) {
    Box box{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
    for (int i = 0; i < 8; ++i) {
        const Vec3 corner{(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f};
        const Vec3 p = transformPoint(world, corner);
        box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
        box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
    }
    return box;
}

void collectOccluders(const SceneNode& node, std::vector<Occluder>& out) {
    if (node.isLeaf() && node.occluder) out.push_back({node.primitive, inverse(node.world), worldBounds(node.world)});
    for (const auto& child : node.children) collectOccluders(*child, out);
}

bool segmentHitsBox(const Vec3& a, const Vec3& b, const Box& box) {
    const float start[3] = {a.x, a.y, a.z};
    const float dir[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z};
    const float hi[3] = {box.max.x, box.max.y, box.max.z};
    float tEnter = 0.0f, tExit = 1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dir[axis]) < 1e-8f) {
            if (start[axis] < lo[axis] || start[axis] > hi[axis]) return false;
            continue;
        }
        float t0 = (lo[axis] - start[axis]) / dir[axis];
        float t1 = (hi[axis] - start[axis]) / dir[axis];
        if (t0 > t1) std::swap(t0, t1);
        tEnter = std::max(tEnter, t0);
        tExit = std::min(tExit, t1);
        if (tEnter > tExit) return false;
    }
    return true;
}

bool segmentHitsPrimitive(const Vec3& a, const Vec3& b, const Occluder& o) {
    if (!segmentHitsBox(a, b, o.bounds)) return false;
    // Back into the primitive's own (unit) space: the segment stays a segment under an affine map.
    const Vec3 la = transformPoint(o.inverseWorld, a);
    const Vec3 d = transformPoint(o.inverseWorld, b) - la;
    switch (o.type) {
        case PrimitiveType::Sphere: {
            float t0, t1;
            return quadraticInterval(la, d, true, 0.5f, t0, t1) && t1 >= 0.0f && t0 <= 1.0f;
        }
        case PrimitiveType::Cylinder: {
            float t0, t1;
            if (!quadraticInterval(la, d, false, 0.5f, t0, t1)) return false;
            // Intersect with the slab |z| <= 0.5 and with the segment range [0, 1].
            float z0 = 0.0f, z1 = 1.0f;
            if (std::fabs(d.z) < 1e-8f) {
                if (std::fabs(la.z) > 0.5f) return false;
            } else {
                z0 = (-0.5f - la.z) / d.z;
                z1 = (0.5f - la.z) / d.z;
                if (z0 > z1) std::swap(z0, z1);
            }
            return std::max({t0, z0, 0.0f}) <= std::min({t1, z1, 1.0f});
        }
        case PrimitiveType::Cone: {
            // Slab |z| <= 0.5 intersected with the segment range, then x^2 + y^2 <= R(z)^2 with
            // R(z) = 0.25 - 0.5 z (0.5 at the base, 0 at the apex). f(t) = A t^2 + B t + C <= 0 somewhere?
            float lo = 0.0f, hi = 1.0f;
            if (std::fabs(d.z) < 1e-8f) {
                if (std::fabs(la.z) > 0.5f) return false;
            } else {
                float z0 = (-0.5f - la.z) / d.z, z1 = (0.5f - la.z) / d.z;
                if (z0 > z1) std::swap(z0, z1);
                lo = std::max(lo, z0);
                hi = std::min(hi, z1);
            }
            if (lo > hi) return false;
            const float k0 = 0.25f - 0.5f * la.z, k1 = -0.5f * d.z;
            const float A = d.x * d.x + d.y * d.y - k1 * k1;
            const float B = 2.0f * (la.x * d.x + la.y * d.y - k0 * k1);
            const float C = la.x * la.x + la.y * la.y - k0 * k0;
            auto f = [&](float t) { return (A * t + B) * t + C; };
            if (f(lo) <= 0.0f || f(hi) <= 0.0f) return true;
            if (A > 1e-12f) {
                const float tv = -B / (2.0f * A);
                return tv > lo && tv < hi && f(tv) <= 0.0f;
            }
            return false;
        }
        // v10 M3 library primitives. Exact: the hemisphere (half ellipsoid) and the flat solids (face planes).
        case PrimitiveType::Hemisphere: {
            // (x, y, z) -> (x, (y + 0.5) / 2, z) maps the half ellipsoid onto the upper half of the sphere r = 0.5.
            const Vec3 a2{la.x, 0.5f * (la.y + 0.5f), la.z}, d2{d.x, 0.5f * d.y, d.z};
            float t0, t1;
            if (!quadraticInterval(a2, d2, true, 0.5f, t0, t1)) return false;
            float lo = std::max(t0, 0.0f), hi = std::min(t1, 1.0f);
            if (std::fabs(d2.y) < 1e-12f) {
                if (a2.y < 0.0f) return false;
            } else if (d2.y > 0.0f) {
                lo = std::max(lo, -a2.y / d2.y);
            } else {
                hi = std::min(hi, -a2.y / d2.y);
            }
            return lo <= hi;
        }
        case PrimitiveType::Wedge:
        case PrimitiveType::TaperedBox:
        case PrimitiveType::Pyramid: return segmentHitsConvex(la, d, facePlanes(o.type));
        // Conservative: the bounding cylinder (radius 0.5, |z| <= 0.5) of the round Z-axis primitives.
        case PrimitiveType::Capsule:
        case PrimitiveType::Torus:
        case PrimitiveType::Annulus:
        case PrimitiveType::Helix: {
            Occluder c = o;
            c.type = PrimitiveType::Cylinder;
            return segmentHitsPrimitive(a, b, c);
        }
        default:
            return segmentHitsBox(la, la + d, Box{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}});
    }
}

bool lineOfSightBlocked(const Vec3& a, const Vec3& b, const std::vector<Occluder>& occluders) {
    for (const Occluder& o : occluders)
        if (segmentHitsPrimitive(a, b, o)) return true;
    return false;
}

void OccluderGrid::build(const std::vector<Occluder>& occluders, float halfExtent, int cellsPerSide) {
    occluders_ = occluders;
    half_ = halfExtent;
    n_ = std::max(1, cellsPerSide);
    cell_ = 2.0f * half_ / static_cast<float>(n_);
    buckets_.assign(static_cast<size_t>(n_ * n_), {});
    outside_.clear();
    stamp_.assign(occluders_.size(), 0u);
    query_ = 0;
    const float eps = 0.01f;  // lines passing exactly through a bucket corner still find their occluders
    for (size_t i = 0; i < occluders_.size(); ++i) {
        const Box& b = occluders_[i].bounds;
        if (b.min.x < -half_ || b.min.z < -half_ || b.max.x > half_ || b.max.z > half_) {
            outside_.push_back(static_cast<int>(i));
            continue;
        }
        const int x0 = std::clamp(static_cast<int>((b.min.x - eps + half_) / cell_), 0, n_ - 1);
        const int x1 = std::clamp(static_cast<int>((b.max.x + eps + half_) / cell_), 0, n_ - 1);
        const int z0 = std::clamp(static_cast<int>((b.min.z - eps + half_) / cell_), 0, n_ - 1);
        const int z1 = std::clamp(static_cast<int>((b.max.z + eps + half_) / cell_), 0, n_ - 1);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) buckets_[static_cast<size_t>(z * n_ + x)].push_back(static_cast<int>(i));
    }
}

bool OccluderGrid::testBucket(const std::vector<int>& bucket, const Vec3& a, const Vec3& b) const {
    for (int i : bucket) {
        if (stamp_[static_cast<size_t>(i)] == query_) continue;  // already tested in an earlier bucket
        stamp_[static_cast<size_t>(i)] = query_;
        if (segmentHitsPrimitive(a, b, occluders_[static_cast<size_t>(i)])) return true;
    }
    return false;
}

bool OccluderGrid::blocked(const Vec3& a, const Vec3& b) const {
    if (occluders_.empty()) return false;
    if (++query_ == 0) {  // wrapped around: reset the stamps
        std::fill(stamp_.begin(), stamp_.end(), 0u);
        query_ = 1;
    }
    if (testBucket(outside_, a, b)) return true;

    // Clip the ground footprint of the segment to the grid square.
    const float dx = b.x - a.x, dz = b.z - a.z;
    float t0 = 0.0f, t1 = 1.0f;
    const float start[2] = {a.x, a.z}, dir[2] = {dx, dz};
    for (int k = 0; k < 2; ++k) {
        if (std::fabs(dir[k]) < 1e-9f) {
            if (start[k] < -half_ || start[k] > half_) return false;
            continue;
        }
        float e0 = (-half_ - start[k]) / dir[k], e1 = (half_ - start[k]) / dir[k];
        if (e0 > e1) std::swap(e0, e1);
        t0 = std::max(t0, e0);
        t1 = std::min(t1, e1);
        if (t0 > t1) return false;
    }
    auto cellOf = [this](float v) { return std::clamp(static_cast<int>((v + half_) / cell_), 0, n_ - 1); };
    int ix = cellOf(a.x + dx * t0), iz = cellOf(a.z + dz * t0);
    const int ex = cellOf(a.x + dx * t1), ez = cellOf(a.z + dz * t1);
    const int stepX = dx > 0.0f ? 1 : -1, stepZ = dz > 0.0f ? 1 : -1;
    const float inf = 1e30f;
    auto firstCrossing = [this](int idx, int step, float origin, float d) {
        const float boundary = static_cast<float>(idx + (step > 0 ? 1 : 0)) * cell_ - half_;
        return (boundary - origin) / d;
    };
    float tMaxX = std::fabs(dx) > 1e-9f ? firstCrossing(ix, stepX, a.x, dx) : inf;
    float tMaxZ = std::fabs(dz) > 1e-9f ? firstCrossing(iz, stepZ, a.z, dz) : inf;
    const float tDeltaX = std::fabs(dx) > 1e-9f ? cell_ / std::fabs(dx) : inf;
    const float tDeltaZ = std::fabs(dz) > 1e-9f ? cell_ / std::fabs(dz) : inf;
    for (int guard = 0; guard < 4 * n_ + 4; ++guard) {
        if (testBucket(buckets_[static_cast<size_t>(iz * n_ + ix)], a, b)) return true;
        if (ix == ex && iz == ez) break;
        if (tMaxX < tMaxZ) {
            ix += stepX;
            tMaxX += tDeltaX;
        } else {
            iz += stepZ;
            tMaxZ += tDeltaZ;
        }
        if (ix < 0 || iz < 0 || ix >= n_ || iz >= n_) break;
    }
    return false;
}

}  // namespace ts
