// Centripetal Catmull-Rom spline (alpha = 0.5) written as one cubic Hermite segment per control-point pair:
// with knots t_{i+1} = t_i + |P_{i+1} - P_i|^alpha the segment P1 -> P2 has the end tangents (per unit of the
// local parameter u in [0, 1])
//   m1 = (t2 - t1) * ((P1 - P0)/(t1 - t0) - (P2 - P0)/(t2 - t0) + (P2 - P1)/(t2 - t1))
//   m2 = (t2 - t1) * ((P2 - P1)/(t2 - t1) - (P3 - P1)/(t3 - t1) + (P3 - P2)/(t3 - t2))
// (identical to the Barry-Goldman pyramid), so the curve passes through every control point and dP/dt is
// continuous across segments (C1 in the knot parameter, G1 in u). Open paths get mirrored end points.
// Arc length: kSamplesPerSegment chords per segment, cumulative table, binary search + linear blend.
#include "math/Spline.h"

#include <algorithm>
#include <cmath>

namespace ts {

namespace {

struct D3 {
    double x = 0, y = 0, z = 0;
};
D3 d3(const Vec3& v) { return {v.x, v.y, v.z}; }
D3 operator+(const D3& a, const D3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 operator-(const D3& a, const D3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 operator*(const D3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double len(const D3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
Vec3 v3(const D3& a) { return {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)}; }

// Position, first and second derivative (w.r.t. u) of the Hermite segment at u.
struct Eval {
    D3 p, d1, d2;
};

}  // namespace

CatmullRomPath::CatmullRomPath(std::vector<Vec3> points, bool closed, float alpha) : closed_(closed), alpha_(alpha) {
    // Coincident neighbours would give zero knot intervals: drop them.
    for (const Vec3& p : points)
        if (pts_.empty() || ts::length(p - pts_.back()) > 1e-4f) pts_.push_back(p);
    if (closed_ && pts_.size() > 2 && ts::length(pts_.front() - pts_.back()) <= 1e-4f) pts_.pop_back();
    if (closed_ && pts_.size() < 3) closed_ = false;
    if (pts_.size() < 2) return;

    const int segs = segments();
    cum_.assign(static_cast<size_t>(segs) * kSamplesPerSegment + 1, 0.0f);
    double acc = 0.0;
    Vec3 prev = point(0.0f);
    for (int i = 1; i <= segs * kSamplesPerSegment; ++i) {
        const Vec3 p = point(static_cast<float>(i) / kSamplesPerSegment);
        acc += ts::length(p - prev);
        cum_[static_cast<size_t>(i)] = static_cast<float>(acc);
        prev = p;
    }
    total_ = static_cast<float>(acc);
}

int CatmullRomPath::segments() const {
    return pts_.size() < 2 ? 0 : static_cast<int>(pts_.size()) - (closed_ ? 0 : 1);
}

namespace {

// Control point k of the (possibly closed) sequence with mirrored phantoms at open ends.
Vec3 ctrl(const std::vector<Vec3>& p, bool closed, int k) {
    const int n = static_cast<int>(p.size());
    if (closed) return p[static_cast<size_t>(((k % n) + n) % n)];
    if (k < 0) return p[0] * 2.0f - p[1];
    if (k >= n) return p[static_cast<size_t>(n - 1)] * 2.0f - p[static_cast<size_t>(n - 2)];
    return p[static_cast<size_t>(k)];
}

Eval evalSegment(const std::vector<Vec3>& pts, bool closed, float alpha, int seg, double u) {
    const D3 P0 = d3(ctrl(pts, closed, seg - 1)), P1 = d3(ctrl(pts, closed, seg)), P2 = d3(ctrl(pts, closed, seg + 1)),
             P3 = d3(ctrl(pts, closed, seg + 2));
    auto knot = [&](const D3& a, const D3& b) { return std::max(std::pow(len(b - a), static_cast<double>(alpha)), 1e-6); };
    const double t01 = knot(P0, P1), t12 = knot(P1, P2), t23 = knot(P2, P3);
    const D3 m1 = ((P1 - P0) * (1.0 / t01) - (P2 - P0) * (1.0 / (t01 + t12)) + (P2 - P1) * (1.0 / t12)) * t12;
    const D3 m2 = ((P2 - P1) * (1.0 / t12) - (P3 - P1) * (1.0 / (t12 + t23)) + (P3 - P2) * (1.0 / t23)) * t12;
    // Hermite basis and its derivatives.
    const double u2 = u * u, u3 = u2 * u;
    const double h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
    const double d00 = 6 * u2 - 6 * u, d10 = 3 * u2 - 4 * u + 1, d01 = -6 * u2 + 6 * u, d11 = 3 * u2 - 2 * u;
    const double s00 = 12 * u - 6, s10 = 6 * u - 4, s01 = -12 * u + 6, s11 = 6 * u - 2;
    Eval e;
    e.p = P1 * h00 + m1 * h10 + P2 * h01 + m2 * h11;
    e.d1 = P1 * d00 + m1 * d10 + P2 * d01 + m2 * d11;
    e.d2 = P1 * s00 + m1 * s10 + P2 * s01 + m2 * s11;
    return e;
}

Eval evalAt(const std::vector<Vec3>& pts, bool closed, float alpha, int segs, float t) {
    t = clampf(t, 0.0f, static_cast<float>(segs));
    int seg = static_cast<int>(std::floor(t));
    if (seg >= segs) seg = segs - 1;
    return evalSegment(pts, closed, alpha, seg, static_cast<double>(t) - seg);
}

}  // namespace

Vec3 CatmullRomPath::point(float t) const {
    if (pts_.empty()) return {};
    if (pts_.size() < 2) return pts_[0];
    return v3(evalAt(pts_, closed_, alpha_, segments(), t).p);
}

Vec3 CatmullRomPath::tangent(float t) const {
    if (pts_.size() < 2) return {0, 0, 1};
    const Eval e = evalAt(pts_, closed_, alpha_, segments(), t);
    const double l = len(e.d1);
    if (l < 1e-9) return normalize(pts_.back() - pts_.front());
    return v3(e.d1 * (1.0 / l));
}

float CatmullRomPath::paramAtDistance(float s) const {
    if (pts_.size() < 2 || total_ <= 0.0f) return 0.0f;
    if (closed_) {
        s = std::fmod(s, total_);
        if (s < 0.0f) s += total_;
    } else {
        s = clampf(s, 0.0f, total_);
    }
    const auto it = std::upper_bound(cum_.begin(), cum_.end(), s);
    size_t i = static_cast<size_t>(it - cum_.begin());
    if (i == 0) i = 1;
    if (i >= cum_.size()) i = cum_.size() - 1;
    const float a = cum_[i - 1], b = cum_[i];
    const float f = b > a ? (s - a) / (b - a) : 0.0f;
    return (static_cast<float>(i - 1) + clampf(f, 0.0f, 1.0f)) / kSamplesPerSegment;
}

Vec3 CatmullRomPath::pointAtDistance(float s) const { return point(paramAtDistance(s)); }
Vec3 CatmullRomPath::tangentAtDistance(float s) const { return tangent(paramAtDistance(s)); }

// kappa = |r' x r''| / |r'|^3 (independent of the parameterisation).
float CatmullRomPath::curvatureAtDistance(float s) const {
    if (pts_.size() < 2) return 0.0f;
    const Eval e = evalAt(pts_, closed_, alpha_, segments(), paramAtDistance(s));
    const D3 c{e.d1.y * e.d2.z - e.d1.z * e.d2.y, e.d1.z * e.d2.x - e.d1.x * e.d2.z, e.d1.x * e.d2.y - e.d1.y * e.d2.x};
    const double l = len(e.d1);
    if (l < 1e-9) return 0.0f;
    return static_cast<float>(len(c) / (l * l * l));
}

}  // namespace ts
