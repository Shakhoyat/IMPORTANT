#include "scene/CameraRig.h"

#include <algorithm>
#include <cmath>

#include "scene/CharacterMotor.h"

namespace ts {

// --- quaternions -------------------------------------------------------------------------------------------------------

Quat quatMul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quat quatConj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }

Quat quatNormalize(const Quat& q) {
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-12f) return {};
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

Vec3 quatRotate(const Quat& q, const Vec3& v) {
    // v' = v + 2 w (u x v) + 2 u x (u x v), u = (x, y, z)
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

Quat quatAxisAngle(const Vec3& axis, float radiansValue) {
    const Vec3 a = normalize(axis);
    const float s = std::sin(0.5f * radiansValue);
    return {std::cos(0.5f * radiansValue), a.x * s, a.y * s, a.z * s};
}

Quat quatSlerp(const Quat& a, const Quat& bIn, float t) {
    Quat b = bIn;
    float d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0.0f) {  // shortest arc: q and -q are the same rotation
        b = {-b.w, -b.x, -b.y, -b.z};
        d = -d;
    }
    if (d > 0.9995f) {  // nearly parallel: normalised lerp
        return quatNormalize({a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
    }
    const float th = std::acos(clampf(d, -1.0f, 1.0f));
    const float s = std::sin(th);
    const float ka = std::sin((1.0f - t) * th) / s, kb = std::sin(t * th) / s;
    return {a.w * ka + b.w * kb, a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb};
}

float quatAngleDeg(const Quat& a, const Quat& b) {
    const float d = std::fabs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
    return degrees(2.0f * std::acos(clampf(d, 0.0f, 1.0f)));
}

Quat quatFromMat4(const Mat4& m) {
    // Shepperd's method on the upper 3x3 (rows / columns as at(row, col)).
    const float m00 = m.at(0, 0), m11 = m.at(1, 1), m22 = m.at(2, 2);
    const float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m.at(2, 1) - m.at(1, 2)) / s;
        q.y = (m.at(0, 2) - m.at(2, 0)) / s;
        q.z = (m.at(1, 0) - m.at(0, 1)) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m.at(2, 1) - m.at(1, 2)) / s;
        q.x = 0.25f * s;
        q.y = (m.at(0, 1) + m.at(1, 0)) / s;
        q.z = (m.at(0, 2) + m.at(2, 0)) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m.at(0, 2) - m.at(2, 0)) / s;
        q.x = (m.at(0, 1) + m.at(1, 0)) / s;
        q.y = 0.25f * s;
        q.z = (m.at(1, 2) + m.at(2, 1)) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m.at(1, 0) - m.at(0, 1)) / s;
        q.x = (m.at(0, 2) + m.at(2, 0)) / s;
        q.y = (m.at(1, 2) + m.at(2, 1)) / s;
        q.z = 0.25f * s;
    }
    return quatNormalize(q);
}

Quat quatLook(const Vec3& forward, const Vec3& upHint) {
    const Vec3 f = normalize(forward);
    Vec3 r = cross(f, upHint);
    if (length(r) < 1e-4f) r = cross(f, std::fabs(f.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0});  // looking straight up / down
    r = normalize(r);
    const Vec3 u = cross(r, f);
    Mat4 m;  // columns: right, up, back (-forward)
    m.at(0, 0) = r.x; m.at(1, 0) = r.y; m.at(2, 0) = r.z;
    m.at(0, 1) = u.x; m.at(1, 1) = u.y; m.at(2, 1) = u.z;
    m.at(0, 2) = -f.x; m.at(1, 2) = -f.y; m.at(2, 2) = -f.z;
    return quatFromMat4(m);
}

// --- poses ---------------------------------------------------------------------------------------------------------------

Mat4 RigPose::world() const {
    const Vec3 r = right(), u = up(), b = forward() * -1.0f;
    Mat4 m;
    m.at(0, 0) = r.x; m.at(1, 0) = r.y; m.at(2, 0) = r.z;
    m.at(0, 1) = u.x; m.at(1, 1) = u.y; m.at(2, 1) = u.z;
    m.at(0, 2) = b.x; m.at(1, 2) = b.y; m.at(2, 2) = b.z;
    m.at(0, 3) = eye.x; m.at(1, 3) = eye.y; m.at(2, 3) = eye.z;
    return m;
}

Mat4 RigPose::view() const {
    const Vec3 r = right(), u = up(), b = forward() * -1.0f;
    Mat4 m;  // rows: right, up, back; translation -R^T eye
    m.at(0, 0) = r.x; m.at(0, 1) = r.y; m.at(0, 2) = r.z; m.at(0, 3) = -dot(r, eye);
    m.at(1, 0) = u.x; m.at(1, 1) = u.y; m.at(1, 2) = u.z; m.at(1, 3) = -dot(u, eye);
    m.at(2, 0) = b.x; m.at(2, 1) = b.y; m.at(2, 2) = b.z; m.at(2, 3) = -dot(b, eye);
    return m;
}

RigPose RigPose::look(const Vec3& eye, const Vec3& target, float fovDeg, const Vec3& up) {
    RigPose p;
    p.eye = eye;
    const Vec3 d = target - eye;
    p.rot = quatLook(length(d) > 1e-6f ? d : Vec3{0, 0, 1}, up);
    p.fovDeg = fovDeg;
    return p;
}

RigPose RigPose::fromView(const Mat4& view, const Vec3& eye, float fovDeg, float nearZ) {
    Mat4 r;  // camera -> world rotation = transpose of the view's 3x3
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.at(i, j) = view.at(j, i);
    RigPose p;
    p.eye = eye;
    p.rot = quatFromMat4(r);
    p.fovDeg = fovDeg;
    p.nearZ = nearZ;
    return p;
}

float fovFromProjection(const Mat4& proj) {
    const float f = proj.at(1, 1);
    return f > 1e-6f ? degrees(2.0f * std::atan(1.0f / f)) : 50.0f;
}

float headingDeg(const Vec3& dir) {
    if (std::fabs(dir.x) < 1e-9f && std::fabs(dir.z) < 1e-9f) return 0.0f;
    const float h = degrees(std::atan2(dir.x, dir.z));
    return h < 0.0f ? h + 360.0f : h;
}

// --- blend ---------------------------------------------------------------------------------------------------------------

float springDecay(float omega, float t) {
    if (t <= 0.0f) return 1.0f;
    const float wt = omega * t;
    return (1.0f + wt) * std::exp(-wt);
}

void CameraRig::start(const RigPose& from, const RigPose& to) {
    dPos_ = from.eye - to.eye;
    dRot_ = quatNormalize(quatMul(quatConj(to.rot), from.rot));
    dFov_ = from.fovDeg - to.fovDeg;
    arc_ = std::min(kArcMax, kArcPerMetre * length(dPos_));
    t_ = 0.0f;
    active_ = length(dPos_) > 1e-4f || quatAngleDeg(from.rot, to.rot) > 0.01f || std::fabs(dFov_) > 0.01f;
}

void CameraRig::advance(float dt) {
    if (!active_) return;
    t_ += std::max(dt, 0.0f);
    if (t_ >= kBlendTime) active_ = false;
}

float CameraRig::offsetWeight() const {
    if (!active_) return 0.0f;
    // Normalised so the weight reaches exactly 0 at kBlendTime (no visible snap of the last 1 %).
    const float end = springDecay(kOmega, kBlendTime);
    return clampf((springDecay(kOmega, t_) - end) / (1.0f - end), 0.0f, 1.0f);
}

RigPose CameraRig::evaluate(const RigPose& target) const {
    if (!active_) return target;
    const float k = offsetWeight();
    RigPose p = target;
    p.eye = target.eye + dPos_ * k + Vec3{0.0f, arc_ * 4.0f * k * (1.0f - k), 0.0f};
    p.rot = quatNormalize(quatMul(target.rot, quatSlerp(Quat{}, dRot_, k)));
    p.fovDeg = target.fovDeg + dFov_ * k;
    return p;
}

// --- game feel -----------------------------------------------------------------------------------------------------------

float jumpLetterbox(float progress, float peak) {
    if (progress <= 0.0f || progress >= 1.0f) return 0.0f;
    // Bars slide in fast and leave slowly (sin^0.6 bump).
    return peak * std::pow(std::sin(3.14159265f * progress), 0.6f);
}

float fovKickTarget(bool boost, float speed, float topSpeed, float maxKickDeg) {
    if (!boost || topSpeed <= 1e-4f) return 0.0f;
    return maxKickDeg * clampf(std::fabs(speed) / topSpeed, 0.0f, 1.0f);
}

float easeTowards(float value, float target, float dt, float tau) {
    if (tau <= 1e-6f) return target;
    return target + (value - target) * std::exp(-std::max(dt, 0.0f) / tau);
}

float hitMarkerAlpha(float age) {
    if (age < 0.0f || age > 0.4f) return 0.0f;
    if (age <= 0.06f) return 1.0f;
    return 1.0f - (age - 0.06f) / 0.34f;
}

std::vector<CompassTick> compassTicks(float heading, float span, float widthPx, int stepDeg) {
    static const char* kNames[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    std::vector<CompassTick> out;
    if (stepDeg <= 0 || span <= 0.0f) return out;
    const float ppd = widthPx / span;
    const int first = static_cast<int>(std::ceil((heading - span * 0.5f) / static_cast<float>(stepDeg))) * stepDeg;
    for (int d = first; static_cast<float>(d) <= heading + span * 0.5f; d += stepDeg) {
        CompassTick t;
        t.x = (static_cast<float>(d) - heading) * ppd;
        t.deg = ((d % 360) + 360) % 360;
        t.major = t.deg % 45 == 0;
        t.label = t.major ? kNames[t.deg / 45] : "";
        out.push_back(t);
    }
    return out;
}

float compassOffset(float heading, float bearing, float span, float widthPx, bool* inside) {
    float d = std::fmod(bearing - heading + 540.0f, 360.0f) - 180.0f;   // -180 .. 180
    if (inside) *inside = std::fabs(d) <= span * 0.5f;
    d = clampf(d, -span * 0.5f, span * 0.5f);
    return d * widthPx / span;
}

// --- free-fly camera -----------------------------------------------------------------------------------------------------

Vec3 FlyCam::step(float yawDeg, float pitchDeg, float fwd, float rightIn, float upIn, bool boost, float dt) {
    const float yaw = radians(yawDeg), pitch = radians(pitchDeg);
    const Vec3 f{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    const Vec3 r = normalize(cross(f, {0, 1, 0}));
    Vec3 wish = f * fwd + r * rightIn + Vec3{0, upIn, 0};
    const float len = length(wish);
    if (len > 1.0f) wish = wish / len;
    const Vec3 goal = wish * (speed * (boost ? kBoost : 1.0f));
    const float k = 1.0f - std::exp(-std::max(dt, 0.0f) / kTau);
    velocity = velocity + (goal - velocity) * k;
    return velocity * dt;
}

void FlyCam::wheel(float steps) { speed = clampf(speed * std::pow(1.25f, steps), kMinSpeed, kMaxSpeed); }


// ------------------------------------------------------------------------------------------------ third person (v7.1 WP-T)

void IndoorArmBlend::step(bool indoor, float dt) {
    if (!valid) reset(indoor);
    critDampStep(blend, vel, indoor ? 1.0f : 0.0f, 2.0f / std::max(tau, 1e-4f), dt);
    blend = clampf(blend, 0.0f, 1.0f);
}

ThirdPersonParams indoorArmParams(ThirdPersonParams p, float blend) {
    const float b = clampf(blend, 0.0f, 1.0f);
    p.arm = lerpf(p.arm, 1.9f, b);
    p.shoulder = lerpf(p.shoulder, 0.45f, b);
    p.maxPitch = lerpf(p.maxPitch, 30.0f, b);
    return p;
}

ThirdPersonParams ThirdPersonParams::vehicle(float arm, float lift, float fovDeg, float pitchMin) {
    ThirdPersonParams p;
    p.arm = p.aimArm = arm;
    p.shoulder = p.aimShoulder = 0.0f;
    p.lift = p.aimLift = lift;
    p.fovDeg = p.aimFovDeg = fovDeg;
    p.minPitch = pitchMin;
    p.maxPitch = 40.0f;
    p.probe = 0.3f;
    p.lagTau = 0.08f;
    p.lagTauY = 0.12f;
    p.maxLag = 2.0f;
    p.armOutTau = 0.4f;
    return p;
}

void ThirdPersonCam::reset(const Vec3& pivot, float yaw, float pitch) {
    valid_ = true;
    pivot_ = pivot;
    shoulderPt_ = pivot;
    yawDeg = yaw;
    pitchDeg = pitch;
    arm_ = -1.0f;
    aim_ = 0.0f;
    side_ = static_cast<float>(side);
    pivotVel_ = {0.0f, 0.0f, 0.0f};
    armVel_ = 0.0f;
}

void ThirdPersonCam::look(float dYaw, float dPitch, const ThirdPersonParams& p) {
    yawDeg = std::fmod(yawDeg - dYaw, 360.0f);
    pitchDeg = clampf(pitchDeg + dPitch, p.minPitch, p.maxPitch);
}

Vec3 ThirdPersonCam::forward() const {
    const float y = radians(yawDeg), x = radians(pitchDeg);
    return {std::cos(x) * std::sin(y), std::sin(x), std::cos(x) * std::cos(y)};
}
Vec3 ThirdPersonCam::flatForward() const { return {std::sin(radians(yawDeg)), 0.0f, std::cos(radians(yawDeg))}; }
Vec3 ThirdPersonCam::flatRight() const { return {-std::cos(radians(yawDeg)), 0.0f, std::sin(radians(yawDeg))}; }

RigPose ThirdPersonCam::update(const Vec3& pivot, float dt, bool aiming, const ThirdPersonParams& p, const ArmCast& cast) {
    if (!valid_) reset(pivot, yawDeg, pitchDeg);
    pitchDeg = clampf(pitchDeg, p.minPitch, p.maxPitch);
    // Lagged pivot: critically damped follow (v9 M3), soft horizontally, softer vertically (steps, jumps, landings),
    // never further than maxLag.
    if (dt > 0.0f) {
        const float wh = 2.0f / std::max(p.lagTau, 1e-4f), wv = 2.0f / std::max(p.lagTauY, 1e-4f);
        critDampStep(pivot_.x, pivotVel_.x, pivot.x, wh, dt);
        critDampStep(pivot_.z, pivotVel_.z, pivot.z, wh, dt);
        critDampStep(pivot_.y, pivotVel_.y, pivot.y, wv, dt);
        aim_ = easeTowards(aim_, aiming ? 1.0f : 0.0f, dt, p.aimTau);
        side_ = easeTowards(side_, static_cast<float>(side), dt, p.sideTau);
    }
    const Vec3 lag = pivot_ - pivot;
    const float lagLen = length(lag);
    if (lagLen > 4.0f) {                                                // teleport: no swoop across the map
        pivot_ = pivot;
        pivotVel_ = {0.0f, 0.0f, 0.0f};
    } else if (lagLen > p.maxLag) {
        pivot_ = pivot + lag * (p.maxLag / lagLen);
    }
    const Vec3 f = forward(), r = flatRight();
    // 1) pivot -> shoulder point (probed: a wall at the shoulder pulls the camera towards the head).
    const float sx = lerpf(p.shoulder, p.aimShoulder, aim_) * side_, lift = lerpf(p.lift, p.aimLift, aim_);
    const Vec3 off = r * sx + Vec3{0.0f, lift, 0.0f};
    const float offLen = length(off);
    shoulderPt_ = pivot_;
    if (offLen > 1e-4f) {
        const Vec3 d = off * (1.0f / offLen);
        float free = cast ? std::min(offLen, cast(pivot_, d, offLen, p.probe)) : offLen;
        // v9 M3: stop 2 cm short of a contact: a shoulder point exactly on the probe boundary can round to inside the
        // box, and the arm cast from there ignores boxes it starts in (the arm then passed through that wall).
        if (free < offLen) free = std::max(0.0f, free - 0.02f);
        shoulderPt_ = pivot_ + d * free;
    }
    // 2) shoulder -> camera, behind along the look direction: pull in at once (never past the first contact), ease back
    // out on a critically damped spring (v9 M3: starts and ends with zero speed, no overshoot past the free length).
    want_ = lerpf(p.arm, p.aimArm, aim_);
    const float free = cast ? std::min(want_, cast(shoulderPt_, -f, want_, p.probe)) : want_;
    if (arm_ < 0.0f || free < arm_) {
        arm_ = free;
        armVel_ = 0.0f;
    } else if (dt > 0.0f) {
        critDampStep(arm_, armVel_, free, 2.0f / std::max(p.armOutTau, 1e-4f), dt);
        if (arm_ > free) {
            arm_ = free;
            armVel_ = 0.0f;
        }
    }
    RigPose pose;
    pose.eye = shoulderPt_ - f * arm_;
    pose.rot = quatLook(f);
    pose.fovDeg = lerpf(p.fovDeg, p.aimFovDeg, aim_);
    pose.nearZ = 0.06f;
    return pose;
}

}  // namespace ts
