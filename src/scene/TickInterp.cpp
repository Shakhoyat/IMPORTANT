#include "scene/TickInterp.h"

#include <algorithm>
#include <cmath>

namespace ts {

int FixedStepClock::advance(double frameSeconds) {
    constexpr double kEps = 1e-9;   // frame times that are whole ticks (1/60 s) must not leave alpha at 0.99999
    accum_ += std::max(0.0, frameSeconds);
    int n = static_cast<int>((accum_ + kEps) / kTickSeconds);
    if (n > kMaxTicksPerFrame) {
        dropped_ += n - kMaxTicksPerFrame;
        accum_ -= n * kTickSeconds;   // keep the phase, forget the backlog
        n = kMaxTicksPerFrame;
    } else {
        accum_ -= n * kTickSeconds;
    }
    if (accum_ < 0.0) accum_ = 0.0;
    ticks_ += n;
    return n;
}

namespace {

Vec3 column(const Mat4& m, int c) { return {m.at(0, c), m.at(1, c), m.at(2, c)}; }

}  // namespace

bool blendTickWorld(const Mat4& a, const Mat4& b, float t, Mat4& out, float maxMove, float maxTurnDeg) {
    out = b;
    const Vec3 ta = column(a, 3), tb = column(b, 3);
    if (length(tb - ta) > maxMove) return false;
    const float minCos = std::cos(radians(maxTurnDeg));
    for (int c = 0; c < 3; ++c) {
        const Vec3 ca = column(a, c), cb = column(b, c);
        const float la = length(ca), lb = length(cb);
        if (la < 1e-8f || lb < 1e-8f) continue;   // degenerate (zero-scaled part): nothing to turn
        if (dot(ca, cb) / (la * lb) < minCos) return false;
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out.at(r, c) = a.at(r, c) + (b.at(r, c) - a.at(r, c)) * t;
    return true;
}

bool blendTickPose(const RigPose& a, const RigPose& b, float t, RigPose& out, float maxMove, float maxTurnDeg) {
    out = b;
    if (length(b.eye - a.eye) > maxMove || quatAngleDeg(a.rot, b.rot) > maxTurnDeg) return false;
    out.eye = a.eye + (b.eye - a.eye) * t;
    out.rot = quatSlerp(a.rot, b.rot, t);
    out.fovDeg = a.fovDeg + (b.fovDeg - a.fovDeg) * t;
    out.nearZ = b.nearZ;
    return true;
}

}  // namespace ts
