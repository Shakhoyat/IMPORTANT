#include "scene/UavView.h"

#include <algorithm>
#include <cmath>

#include "objects/Uav.h"

namespace ts {

namespace {

float wrapDeg(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};

}  // namespace

const char* uavViewName(UavView v) {
    switch (v) {
        case UavView::Chase: return "CHASE";
        case UavView::Fpv: return "FPV";
        case UavView::Gimbal: return "GIMBAL";
    }
    return "?";
}

UavView nextUavView(UavView v) { return static_cast<UavView>((static_cast<int>(v) + 1) % 3); }

// Exact solution of x'' = w^2 (target - x) - 2 w x' over dt for a constant target:
//   x(t) = target + (c + (v + w c) t) e^(-w t),  v(t) = (v - w (v + w c) t) e^(-w t),  c = x(0) - target.
void springStep(float& x, float& v, float target, float omega, float dt) {
    if (dt <= 0.0f) return;
    const float e = std::exp(-omega * dt);
    const float c = x - target;
    const float temp = (v + omega * c) * dt;
    v = (v - omega * temp) * e;
    x = target + (c + temp) * e;
}

void springStep(Vec3& x, Vec3& v, const Vec3& target, float omega, float dt) {
    springStep(x.x, v.x, target.x, omega, dt);
    springStep(x.y, v.y, target.y, omega, dt);
    springStep(x.z, v.z, target.z, omega, dt);
}

Quat framedLook(const Vec3& eye, const Vec3& target, float fovDeg, float framing, float rollDeg, float tiltDeg) {
    Vec3 d = target - eye;
    d = length(d) > 1e-5f ? normalize(d) : Vec3{0.0f, 0.0f, 1.0f};
    Vec3 right = cross(d, kUp);
    right = length(right) > 1e-4f ? normalize(right) : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 up = normalize(cross(right, d));
    // Raise the view axis by theta above the target: the target then sits tan(theta) / tan(fov / 2) = framing below
    // the centre of the screen. A positive tilt pitches the view down again (nose-down acceleration).
    const float theta = std::atan(framing * std::tan(radians(fovDeg) * 0.5f)) - radians(tiltDeg);
    const Vec3 f = d * std::cos(theta) + up * std::sin(theta);
    const Vec3 u = up * std::cos(theta) - d * std::sin(theta);
    Quat q = quatLook(f, u);
    // Roll about the view axis: positive = right side down (camera-local +z points backwards).
    if (std::fabs(rollDeg) > 1e-4f) q = quatNormalize(quatMul(q, quatAxisAngle({0.0f, 0.0f, 1.0f}, radians(-rollDeg))));
    return q;
}

bool poseNdc(const RigPose& pose, float aspect, const Vec3& p, float& x, float& y) {
    const Vec3 rel = p - pose.eye;
    const float zf = dot(rel, pose.forward());
    if (zf <= 1e-5f) return false;
    const float t = std::tan(radians(pose.fovDeg) * 0.5f);
    x = dot(rel, pose.right()) / (zf * t * aspect);
    y = dot(rel, pose.up()) / (zf * t);
    return true;
}

void UavChaseCam::reset(const UavChaseInput& in, const UavChaseParams& p) {
    (void)p;
    follow_ = in.position;
    followVel_ = {0.0f, 0.0f, 0.0f};
    yaw_ = in.yawDeg;
    yawVel_ = 0.0f;
    bank_ = bankVel_ = tilt_ = tiltVel_ = 0.0f;
    arm_ = -1.0f;
    prevYaw_ = in.yawDeg;
    prevVel_ = in.velocity;
    valid_ = true;
}

RigPose UavChaseCam::update(const UavChaseInput& in, float dt, const UavChaseParams& p, const ArmCast& cast) {
    if (!valid_) reset(in, p);
    if (dt > 0.0f) {
        // Turn rate and forward acceleration of the drone since the last tick.
        const float yawRate = wrapDeg(in.yawDeg - prevYaw_) / dt;   // deg/s, + = towards +x = a left turn
        const Vec3 accel = (in.velocity - prevVel_) / dt;
        prevYaw_ = in.yawDeg;
        prevVel_ = in.velocity;
        springStep(follow_, followVel_, in.position, p.followOmega, dt);
        springStep(yaw_, yawVel_, yaw_ + wrapDeg(in.yawDeg - yaw_), p.yawOmega, dt);
        const Vec3 fwd{std::sin(radians(yaw_)), 0.0f, std::cos(radians(yaw_))};
        const float bankTarget = clampf(-p.bankPerTurn * yawRate, -p.maxBank, p.maxBank);   // into the turn
        const float tiltTarget = clampf(p.pitchPerAccel * dot(accel, fwd), -p.maxPitchTilt, p.maxPitchTilt);
        springStep(bank_, bankVel_, bankTarget, p.tiltOmega, dt);
        springStep(tilt_, tiltVel_, tiltTarget, p.tiltOmega, dt);
    }
    const Vec3 fwd{std::sin(radians(yaw_)), 0.0f, std::cos(radians(yaw_))};
    Vec3 wanted = follow_ - fwd * p.distance + kUp * p.height;
    // The lag never brings the camera closer than minBack * distance behind the drone (flying backwards, braking).
    const float back = -dot(wanted - in.position, fwd);
    if (back < p.minBack * p.distance) wanted = wanted - fwd * (p.minBack * p.distance - back);
    // Spring arm from the drone (not the lagged follow point) to the wanted eye: the probe sphere stops at the first
    // solid / the ground; a pull-in is immediate, the way back out eases (no popping).
    const Vec3 pivot = in.position + kUp * p.pivotLift;
    Vec3 dir = wanted - pivot;
    const float len = length(dir);
    dir = len > 1e-4f ? dir / len : Vec3{0.0f, 0.0f, -1.0f};
    want_ = len;
    float free = len;
    // A probe that already touches a solid at the pivot ignores it (the subject's own volume): a thin second probe
    // keeps the eye out of such a box too.
    if (cast) free = std::min({len, std::max(0.0f, cast(pivot, dir, len, p.probe)), std::max(0.0f, cast(pivot, dir, len, p.probeMin))});
    if (arm_ < 0.0f || free < arm_) arm_ = free;
    else if (dt > 0.0f) arm_ += (free - arm_) * (1.0f - std::exp(-dt / std::max(p.armOutTau, 1e-3f)));
    arm_ = std::min(arm_, free);
    RigPose pose;
    pose.eye = pivot + dir * arm_;
    pose.fovDeg = p.fovDeg;
    pose.nearZ = p.nearZ;
    pose.rot = framedLook(pose.eye, in.position, p.fovDeg, p.framing, bank_, tilt_);
    return pose;
}

RigPose uavFpvPose(const Mat4& body) {
    RigPose p;
    p.eye = transformPoint(body, {0.0f, 0.085f, 0.08f});   // on top of the canopy, behind its nose
    const Vec3 fwd = normalize(transformDirection(body, {0.0f, 0.0f, 1.0f}));
    const Vec3 up = normalize(transformDirection(body, {0.0f, 1.0f, 0.0f}));
    const float a = radians(kFpvUptiltDeg);
    p.rot = quatLook(fwd * std::cos(a) + up * std::sin(a), up * std::cos(a) - fwd * std::sin(a));
    p.fovDeg = kFpvFovDeg;
    p.nearZ = 0.03f;
    return p;
}

RigPose uavGimbalPose(const Mat4& cam, float fovDeg) {
    RigPose p;
    p.eye = transformPoint(cam, {0.0f, 0.0f, kLampFrontOffset + 0.03f});
    p.rot = quatLook(transformDirection(cam, {0.0f, 0.0f, 1.0f}), normalize(transformDirection(cam, {0.0f, 1.0f, 0.0f})));
    p.fovDeg = fovDeg;
    p.nearZ = 0.05f;
    return p;
}

void poseAttitude(const RigPose& p, float& headingDeg, float& pitchDeg, float& rollDeg) {
    const Vec3 f = p.forward(), r = p.right(), u = p.up();
    headingDeg = std::fmod(degrees(std::atan2(f.x, f.z)) + 360.0f, 360.0f);
    pitchDeg = degrees(std::asin(clampf(f.y, -1.0f, 1.0f)));
    rollDeg = degrees(std::atan2(-r.y, u.y));
}

std::vector<LadderRung> pitchLadder(float camPitchDeg, float fovYDeg, float screenH, int stepDeg, float maxOffset) {
    std::vector<LadderRung> out;
    if (stepDeg <= 0) return out;
    const float k = 0.5f * screenH / std::tan(radians(fovYDeg) * 0.5f);
    for (int deg = -90; deg <= 90; deg += stepDeg) {
        const float rel = static_cast<float>(deg) - camPitchDeg;
        if (std::fabs(rel) >= 89.0f) continue;
        const float off = k * std::tan(radians(rel));
        if (std::fabs(off) <= maxOffset) out.push_back({deg, off});
    }
    return out;
}

std::vector<TapeMark> tapeMarks(float value, float step, int majorEvery, float pixelsPerUnit, float halfHeight) {
    std::vector<TapeMark> out;
    if (step <= 0.0f || pixelsPerUnit <= 0.0f) return out;
    const int n = static_cast<int>(std::ceil(halfHeight / (step * pixelsPerUnit))) + 1;
    const float base = std::floor(value / step);
    for (int k = -n; k <= n; ++k) {
        const float idx = base + static_cast<float>(k);
        const float v = idx * step;
        const float off = (v - value) * pixelsPerUnit;
        if (std::fabs(off) > halfHeight) continue;
        const long long i = static_cast<long long>(std::llround(idx));
        out.push_back({v, off, majorEvery > 0 && i % majorEvery == 0});
    }
    return out;
}

float lockProgress(float lockSeconds, float needSeconds) {
    return needSeconds > 0.0f ? clampf(lockSeconds / needSeconds, 0.0f, 1.0f) : 1.0f;
}

}  // namespace ts
