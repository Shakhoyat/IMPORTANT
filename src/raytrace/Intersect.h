// Exact analytic ray / unit-primitive intersection in the primitive's local space (see RtTypes.h for the shapes).
// o, d: local ray (d unnormalised); returns the nearest root t in (tmin, tmax) and the OUTWARD local normal there.
#pragma once

#include "raytrace/RtTypes.h"

namespace ts::rt {

bool intersectUnitSphere(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n);
bool intersectUnitBox(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n);
bool intersectUnitCylinder(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n);
bool intersectUnitCone(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n);
// Dispatch on the shape (Heightfield is handled by the Heightfield class, returns false here).
bool intersectUnitShape(Shape s, const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n);

// Implicit surface value of the unit shape at local point p (0 on the surface): used by the tests to verify hits.
float unitShapeDistance(Shape s, const Vec3& p);

// Axis-aligned box.
struct Aabb {
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    void grow(const Vec3& p);
    void grow(const Aabb& b);
    bool empty() const { return lo.x > hi.x; }
    Vec3 centre() const { return (lo + hi) * 0.5f; }
    float area() const;   // surface area (0 for an empty box)
    // Slab test; returns the entry distance in tEnter.
    bool hit(const Vec3& o, const Vec3& invD, float tmin, float tmax, float& tEnter) const;
};
// World bounds of a unit shape under an affine matrix (exact for the box, tight for sphere / cylinder / cone).
Aabb shapeBounds(Shape s, const Mat4& world);

}  // namespace ts::rt
