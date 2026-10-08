// v6 curves (WP-B): centripetal Catmull-Rom splines (alpha = 0.5: no cusps, no self-intersection) with an
// arc-length table, so soldiers and vehicles move along curved routes at a controlled speed.
#pragma once

#include <vector>

#include "math/Math3D.h"

namespace ts {

class CatmullRomPath {
public:
    CatmullRomPath() = default;
    CatmullRomPath(std::vector<Vec3> points, bool closed, float alpha = 0.5f);

    bool empty() const { return pts_.size() < 2; }
    bool closed() const { return closed_; }
    const std::vector<Vec3>& points() const { return pts_; }
    int segments() const;
    Vec3 point(float t) const;                     // t in [0, segments()]; integer t = control points
    Vec3 tangent(float t) const;                   // unit
    float length() const { return total_; }
    Vec3 pointAtDistance(float s) const;           // arc-length parameterisation (wraps when closed)
    Vec3 tangentAtDistance(float s) const;
    float curvatureAtDistance(float s) const;      // 1/m
    float paramAtDistance(float s) const;          // t for arc length s

    static constexpr int kSamplesPerSegment = 64;

private:
    std::vector<Vec3> pts_;
    bool closed_ = false;
    float alpha_ = 0.5f;
    std::vector<float> cum_;                       // cumulative length at each table sample
    float total_ = 0.0f;
};

}  // namespace ts
