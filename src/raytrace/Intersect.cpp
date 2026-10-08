#include "raytrace/Intersect.h"

#include <algorithm>
#include <cmath>

namespace ts::rt {

const char* shapeName(Shape s) {
    switch (s) {
        case Shape::Sphere: return "sphere";
        case Shape::Box: return "box";
        case Shape::Cylinder: return "cylinder";
        case Shape::Cone: return "cone";
        case Shape::Heightfield: return "terrain";
    }
    return "?";
}

namespace {

// Smallest root of a t^2 + 2 b t + c = 0 inside (tmin, tmax); both roots are tried so rays from inside work.
bool quadratic(float a, float b, float c, float& t0, float& t1) {
    if (std::fabs(a) < 1e-12f) {   // linear (ray parallel to a cone generator): one root
        if (std::fabs(b) < 1e-12f) return false;
        t0 = t1 = -c / (2.0f * b);
        return true;
    }
    const float disc = b * b - a * c;
    if (disc < 0.0f) return false;
    const float s = std::sqrt(disc);
    // Numerically stable form: q = -(b + sign(b) s), roots q / a and c / q.
    const float q = -(b + (b >= 0.0f ? s : -s));
    float r0 = q / a, r1 = std::fabs(q) > 1e-20f ? c / q : r0;
    if (r0 > r1) std::swap(r0, r1);
    t0 = r0;
    t1 = r1;
    return true;
}

}  // namespace

// The quadratics are solved from the point of closest approach o + d ts instead of o: for a thin part seen from far
// away (a 1 cm fence wire 60 m off is 5000 local units) |o|^2 - r^2 would lose every significant digit.
bool intersectUnitSphere(const Vec3& o0, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) {
    const float ts = -dot(o0, d) / std::max(dot(d, d), 1e-30f);
    const Vec3 o = o0 + d * ts;
    float t0, t1;
    if (!quadratic(dot(d, d), dot(o, d), dot(o, o) - 0.25f, t0, t1)) return false;
    t0 += ts;
    t1 += ts;
    float r = t0 > tmin ? t0 : t1;
    if (r <= tmin || r >= tmax) return false;
    t = r;
    n = normalize(o0 + d * r);
    return true;
}

bool intersectUnitBox(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) {
    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
    float tNear = -1e30f, tFar = 1e30f;
    int axisNear = 0, axisFar = 0;
    float signNear = 1.0f, signFar = 1.0f;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(dd[k]) < 1e-12f) {
            if (oo[k] < -0.5f || oo[k] > 0.5f) return false;
            continue;
        }
        const float inv = 1.0f / dd[k];
        float ta = (-0.5f - oo[k]) * inv, tb = (0.5f - oo[k]) * inv;
        float sa = -1.0f, sb = 1.0f;   // outward normal sign of the face hit at ta / tb
        if (ta > tb) {
            std::swap(ta, tb);
            std::swap(sa, sb);
        }
        if (ta > tNear) { tNear = ta; axisNear = k; signNear = sa; }
        if (tb < tFar) { tFar = tb; axisFar = k; signFar = sb; }
        if (tNear > tFar) return false;
    }
    float r;
    int axis;
    float sign;
    if (tNear > tmin) { r = tNear; axis = axisNear; sign = signNear; }
    else { r = tFar; axis = axisFar; sign = signFar; }    // origin inside: exit face
    if (r <= tmin || r >= tmax) return false;
    t = r;
    n = {axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f};
    return true;
}

bool intersectUnitCylinder(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) {
    bool found = false;
    float best = tmax;
    // Side: x^2 + y^2 = 0.25, |z| <= 0.5 (solved from the closest approach to the axis)
    float t0, t1;
    const float a = d.x * d.x + d.y * d.y;
    const float ts = a > 1e-30f ? -(o.x * d.x + o.y * d.y) / a : 0.0f;
    const float sx = o.x + d.x * ts, sy = o.y + d.y * ts;
    if (quadratic(a, sx * d.x + sy * d.y, sx * sx + sy * sy - 0.25f, t0, t1)) {
        t0 += ts;
        t1 += ts;
        for (float r : {t0, t1}) {
            if (r <= tmin || r >= best) continue;
            const float z = o.z + d.z * r;
            if (z < -0.5f || z > 0.5f) continue;
            best = r;
            const Vec3 p = o + d * r;
            n = normalize(Vec3{p.x, p.y, 0.0f});
            found = true;
            break;
        }
    }
    // Caps z = +-0.5, x^2 + y^2 <= 0.25
    if (std::fabs(d.z) > 1e-12f) {
        for (float zc : {-0.5f, 0.5f}) {
            const float r = (zc - o.z) / d.z;
            if (r <= tmin || r >= best) continue;
            const float x = o.x + d.x * r, y = o.y + d.y * r;
            if (x * x + y * y > 0.25f) continue;
            best = r;
            n = {0.0f, 0.0f, zc > 0.0f ? 1.0f : -1.0f};
            found = true;
        }
    }
    if (found) t = best;
    return found;
}

bool intersectUnitCone(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) {
    bool found = false;
    float best = tmax;
    // Side: x^2 + y^2 = (0.25 - 0.5 z)^2, z in [-0.5, 0.5]. With k = 0.25 - 0.5 z:
    //   (dx^2 + dy^2 - 0.25 dz^2) t^2 + 2 (ox dx + oy dy + 0.5 k0 dz) t + (ox^2 + oy^2 - k0^2) = 0,  k0 = 0.25 - 0.5 oz
    const float ts = -dot(o, d) / std::max(dot(d, d), 1e-30f);
    const Vec3 os = o + d * ts;
    const float k0 = 0.25f - 0.5f * os.z;
    float t0, t1;
    if (quadratic(d.x * d.x + d.y * d.y - 0.25f * d.z * d.z, os.x * d.x + os.y * d.y + 0.5f * k0 * d.z,
                  os.x * os.x + os.y * os.y - k0 * k0, t0, t1)) {
        t0 += ts;
        t1 += ts;
        for (float r : {t0, t1}) {
            if (r <= tmin || r >= best) continue;
            const float z = o.z + d.z * r;
            if (z < -0.5f || z > 0.5f) continue;   // also rejects the mirrored nappe above the apex
            best = r;
            const Vec3 p = o + d * r;
            n = normalize(Vec3{p.x, p.y, 0.5f * (0.25f - 0.5f * p.z)});   // grad F / 2 = (x, y, r(z) / 2)
            found = true;
            break;
        }
    }
    // Base cap z = -0.5, radius 0.5
    if (std::fabs(d.z) > 1e-12f) {
        const float r = (-0.5f - o.z) / d.z;
        if (r > tmin && r < best) {
            const float x = o.x + d.x * r, y = o.y + d.y * r;
            if (x * x + y * y <= 0.25f) {
                best = r;
                n = {0.0f, 0.0f, -1.0f};
                found = true;
            }
        }
    }
    if (found) t = best;
    return found;
}

bool intersectUnitShape(Shape s, const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) {
    switch (s) {
        case Shape::Sphere: return intersectUnitSphere(o, d, tmin, tmax, t, n);
        case Shape::Box: return intersectUnitBox(o, d, tmin, tmax, t, n);
        case Shape::Cylinder: return intersectUnitCylinder(o, d, tmin, tmax, t, n);
        case Shape::Cone: return intersectUnitCone(o, d, tmin, tmax, t, n);
        case Shape::Heightfield: return false;
    }
    return false;
}

float unitShapeDistance(Shape s, const Vec3& p) {
    switch (s) {
        case Shape::Sphere: return length(p) - 0.5f;
        case Shape::Box: return std::max({std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)}) - 0.5f;
        case Shape::Cylinder: return std::max(std::sqrt(p.x * p.x + p.y * p.y) - 0.5f, std::fabs(p.z) - 0.5f);
        case Shape::Cone: {
            const float side = std::sqrt(p.x * p.x + p.y * p.y) - (0.25f - 0.5f * p.z);
            return std::max(side * 0.894427f, std::max(-0.5f - p.z, p.z - 0.5f));   // 0.894 = cos of the half-angle
        }
        case Shape::Heightfield: return 0.0f;
    }
    return 0.0f;
}

void Aabb::grow(const Vec3& p) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
}
void Aabb::grow(const Aabb& b) {
    if (b.empty()) return;
    grow(b.lo);
    grow(b.hi);
}
float Aabb::area() const {
    if (empty()) return 0.0f;
    const Vec3 e = hi - lo;
    return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
}
bool Aabb::hit(const Vec3& o, const Vec3& invD, float tmin, float tmax, float& tEnter) const {
    const float tx0 = (lo.x - o.x) * invD.x, tx1 = (hi.x - o.x) * invD.x;
    const float ty0 = (lo.y - o.y) * invD.y, ty1 = (hi.y - o.y) * invD.y;
    const float tz0 = (lo.z - o.z) * invD.z, tz1 = (hi.z - o.z) * invD.z;
    const float a = std::max({std::min(tx0, tx1), std::min(ty0, ty1), std::min(tz0, tz1), tmin});
    const float b = std::min({std::max(tx0, tx1), std::max(ty0, ty1), std::max(tz0, tz1), tmax});
    tEnter = a;
    return a <= b;
}

Aabb shapeBounds(Shape s, const Mat4& m) {
    Aabb b;
    const Vec3 c = transformPoint(m, {0, 0, 0});
    // Half extents of the image of the unit shape: for a box sum |M_ij| * 0.5; for a sphere sqrt(sum M_ij^2) * 0.5;
    // the cylinder / cone are bounded by the box in z and by the disc in x, y.
    Vec3 e;
    for (int r = 0; r < 3; ++r) {
        const float ax = m.at(r, 0), ay = m.at(r, 1), az = m.at(r, 2);
        float v = 0.0f;
        switch (s) {
            case Shape::Sphere: v = 0.5f * std::sqrt(ax * ax + ay * ay + az * az); break;
            case Shape::Cylinder:
            case Shape::Cone: v = 0.5f * (std::sqrt(ax * ax + ay * ay) + std::fabs(az)); break;
            default: v = 0.5f * (std::fabs(ax) + std::fabs(ay) + std::fabs(az)); break;
        }
        (r == 0 ? e.x : r == 1 ? e.y : e.z) = v;
    }
    b.lo = c - e;
    b.hi = c + e;
    return b;
}

}  // namespace ts::rt
