// v7 game camera (WP-G): one rig for every view of the program. A view is a RigPose (eye, orientation quaternion,
// vertical field of view); jumping from one view to another (POV picker, Insert / Delete, middle-click pick, free
// camera Home / End, possession keys) starts a blend: the OFFSET between the old view and the new one decays as the
// step response of a critically damped spring, x(t) = (1 + w t) e^(-w t), so the camera leaves with zero velocity,
// arrives with zero velocity and keeps following a target that moves during the blend. Position: offset vector
// (with a small arc up for long jumps); orientation: the relative rotation, slerped from it to the identity.
//
// Also the game-feel curves (letterbox bump during a jump, FOV kick on boost, hit-marker fade, compass strip
// mapping) and the smoothed free-fly camera. Pure CPU code (unit-tested in tests/test_game.cpp).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "math/Math3D.h"

namespace ts {

// --- quaternions (unit, w + xi + yj + zk) ---------------------------------------------------------------------------
struct Quat {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
};
Quat quatMul(const Quat& a, const Quat& b);
Quat quatConj(const Quat& q);
Quat quatNormalize(const Quat& q);
Vec3 quatRotate(const Quat& q, const Vec3& v);
Quat quatAxisAngle(const Vec3& axis, float radians);
// Shortest-arc spherical interpolation (t = 0 -> a, 1 -> b).
Quat quatSlerp(const Quat& a, const Quat& b, float t);
// Angle between two orientations, degrees (0..180).
float quatAngleDeg(const Quat& a, const Quat& b);
// Camera orientation (camera axes: +x right, +y up, -z forward, as lookAt) looking along 'forward', 'up' as the hint.
Quat quatLook(const Vec3& forward, const Vec3& up = {0.0f, 1.0f, 0.0f});
Quat quatFromMat4(const Mat4& rotation);   // upper 3x3 (orthonormal) -> quaternion

// --- poses -------------------------------------------------------------------------------------------------------------
struct RigPose {
    Vec3 eye;
    Quat rot;               // camera -> world
    float fovDeg = 50.0f;   // vertical
    float nearZ = 0.05f;

    Vec3 forward() const { return quatRotate(rot, {0.0f, 0.0f, -1.0f}); }
    Vec3 up() const { return quatRotate(rot, {0.0f, 1.0f, 0.0f}); }
    Vec3 right() const { return quatRotate(rot, {1.0f, 0.0f, 0.0f}); }
    Mat4 view() const;      // world -> camera
    Mat4 world() const;     // camera -> world (the inverse of view())
    static RigPose look(const Vec3& eye, const Vec3& target, float fovDeg, const Vec3& up = {0.0f, 1.0f, 0.0f});
    // From a view matrix (lookAt) and its projection's vertical field of view.
    static RigPose fromView(const Mat4& view, const Vec3& eye, float fovDeg, float nearZ = 0.05f);
};
// Vertical field of view of a perspective() matrix, degrees.
float fovFromProjection(const Mat4& proj);
// Heading of a direction (0 = +z = north, 90 = +x = east), degrees 0..360.
float headingDeg(const Vec3& dir);

// --- blend ---------------------------------------------------------------------------------------------------------------
// Critically damped spring step response from 1 (offset fully on) to 0: (1 + w t) e^(-w t).
float springDecay(float omega, float t);

class CameraRig {
public:
    static constexpr float kBlendTime = 0.6f;    // s: the offset is below 1 % (then snaps to 0)
    static constexpr float kOmega = 11.07f;      // (1 + kOmega * kBlendTime) e^(-kOmega * kBlendTime) = 0.01
    static constexpr float kArcPerMetre = 0.18f, kArcMax = 6.0f;   // long jumps lift up to 6 m mid-way

    // A jump from the view on screen ('from') to a new view whose first target pose is 'to'.
    void start(const RigPose& from, const RigPose& to);
    void advance(float dt);
    void cancel() { active_ = false; t_ = kBlendTime; }
    bool active() const { return active_; }
    float elapsed() const { return t_; }
    // 0 at the start of a jump, 1 when it has finished (or none is running).
    float progress() const { return active_ ? clampf(t_ / kBlendTime, 0.0f, 1.0f) : 1.0f; }
    // Displayed pose for this frame's target pose (the target itself when no blend runs).
    RigPose evaluate(const RigPose& target) const;
    float offsetWeight() const;   // remaining share of the offset, 1 -> 0

private:
    bool active_ = false;
    float t_ = kBlendTime;
    Vec3 dPos_;
    Quat dRot_;        // target-local rotation from the target orientation to the start orientation
    float dFov_ = 0.0f, arc_ = 0.0f;
};

// --- game feel -----------------------------------------------------------------------------------------------------------
// Height of each letterbox bar (fraction of the screen height) during a jump: 0 -> peak -> 0 over the blend.
float jumpLetterbox(float progress, float peak = 0.11f);
// FOV kick (degrees added) towards which the view eases: sprint / boost widen the view with the speed.
float fovKickTarget(bool boost, float speed, float topSpeed, float maxKickDeg = 8.0f);
// Exponential ease of a value towards a target with time constant tau (frame-rate independent).
float easeTowards(float value, float target, float dt, float tau);
// Hit marker opacity, age in s since the hit: full for 60 ms, gone after 0.4 s.
float hitMarkerAlpha(float age);

// Compass strip: 'span' degrees across 'widthPx' pixels, centred on 'heading'.
struct CompassTick {
    float x = 0.0f;      // pixels from the strip's centre
    int deg = 0;         // 0..359
    bool major = false;  // every 45 degrees (labelled)
    const char* label = "";   // N NE E SE S SW W NW, or "" for minor ticks
};
std::vector<CompassTick> compassTicks(float headingDeg, float spanDeg, float widthPx, int stepDeg = 15);
// Signed pixel offset of a bearing on the strip (clamped to +-widthPx/2; 'inside' false when beyond the span).
float compassOffset(float headingDeg, float bearingDeg, float spanDeg, float widthPx, bool* inside = nullptr);

// --- free-fly camera -----------------------------------------------------------------------------------------------------
// Smoothed fly camera (Home): wish direction in camera space (x right, y world up, z forward) at 'speed' m/s,
// velocity approaches it with a 0.12 s time constant (glide, no stutter), mouse look is applied by the caller.
struct FlyCam {
    Vec3 velocity;
    float speed = 8.0f;                         // m/s, mouse wheel x1.25 per step (1 .. 60)
    static constexpr float kMinSpeed = 1.0f, kMaxSpeed = 60.0f, kBoost = 3.0f, kTau = 0.12f;
    // Returns the displacement for this frame. yaw / pitch in degrees (the Camera free-mode convention).
    Vec3 step(float yawDeg, float pitchDeg, float fwd, float right, float up, bool boost, float dt);
    void wheel(float steps);
};


// --- v7.1 WP-T third-person camera ---------------------------------------------------------------------------------
// Over-the-shoulder (soldiers) / chase (vehicles) camera on a spring arm, like a modern third-person shooter:
//   pivot (lagged follow of the subject's chest / hull) -> shoulder point (sideways, probed) -> camera (behind, probed).
// The arm pulls in at once when a wall, the ground or a ceiling comes between the shoulder and the camera, and eases
// back out (no clipping into geometry, no popping). v9 M3: the pivot follow and the arm's way back out are critically
// damped springs (zeta = 1, x'' = -2 w x' - w^2 (x - goal), w = 2 / tau: the same lag behind a steady walk as the v8
// exponential, but no velocity jump when the subject starts / stops and no overshoot). Mouse orbits (yaw / pitch); right mouse aims: a shorter arm, the
// shoulder closer and a narrower field of view; Q swaps the shoulder (smoothed).
struct ThirdPersonParams {
    float arm = 3.1f, aimArm = 1.45f;            // m behind the shoulder point
    float shoulder = 0.62f, aimShoulder = 0.52f; // m to the side
    float lift = 0.18f, aimLift = 0.10f;         // shoulder point above the pivot
    float fovDeg = 62.0f, aimFovDeg = 42.0f;
    float minPitch = -70.0f, maxPitch = 55.0f;
    float probe = 0.22f;                         // probe sphere radius (> near plane)
    float lagTau = 0.05f, lagTauY = 0.11f;       // s: pivot follow (horizontal, vertical: softer for jumps / steps), w = 2 / tau
    float maxLag = 0.8f;                         // m: the lagged pivot never trails further
    float armOutTau = 0.28f;                     // s: arm extension after a pull-in (spring w = 2 / tau)
    float aimTau = 0.07f, sideTau = 0.11f;
    static ThirdPersonParams soldier() { return {}; }
    // Vehicles: centred chase on the same arm (no shoulder offset, no aim zoom).
    static ThirdPersonParams vehicle(float arm, float lift, float fovDeg, float pitchMin = -60.0f);
};

// Free length along a unit direction from an origin for a probe sphere (scene/WalkSpace.h sphereCast in the app).
using ArmCast = std::function<float(const Vec3& origin, const Vec3& dir, float maxLen, float radius)>;

class ThirdPersonCam {
public:
    float yawDeg = 0.0f;     // look heading (0 = +z, 90 = +x; a right turn lowers it, as Soldier::yawDeg)
    float pitchDeg = -12.0f; // look elevation (negative = down)
    int side = 1;            // +1 right shoulder, -1 left

    bool valid() const { return valid_; }
    void invalidate() { valid_ = false; }
    void reset(const Vec3& pivot, float yawDeg, float pitchDeg = -12.0f);
    void look(float dYawRightDeg, float dPitchUpDeg, const ThirdPersonParams& p);   // mouse look
    void swapShoulder() { side = -side; }
    // Advance the follow / arm / aim springs by dt (0 = evaluate only) and return the camera pose.
    RigPose update(const Vec3& pivot, float dt, bool aiming, const ThirdPersonParams& p, const ArmCast& cast);

    Vec3 forward() const;          // look direction
    Vec3 flatForward() const;      // horizontal look direction
    Vec3 flatRight() const;
    float arm() const { return arm_; }            // current arm length (m)
    float wantedArm() const { return want_; }     // unobstructed length for the current aim
    float aimBlend() const { return aim_; }
    float sideBlend() const { return side_; }
    Vec3 pivot() const { return pivot_; }
    Vec3 pivotVelocity() const { return pivotVel_; }
    Vec3 shoulderPoint() const { return shoulderPt_; }

private:
    bool valid_ = false;
    Vec3 pivot_, shoulderPt_;
    float arm_ = -1.0f, want_ = 0.0f, aim_ = 0.0f, side_ = 1.0f;
    Vec3 pivotVel_;              // v9 M3: spring state of the pivot follow (m/s) and of the arm (m/s)
    float armVel_ = 0.0f;
};

// v9 M4: the soldier's arm indoors (bunker rooms, the barracks hall): shorter (3.1 -> 1.9 m), the shoulder closer
// (0.62 -> 0.45 m), a flatter pitch limit (55 -> 30 deg). Crossing a doorway blends the two on a critically damped spring
// (blend 0 outdoors .. 1 indoors), so the arm glides in and out instead of popping. The probe still pulls the arm in at
// once at any contact, so the blend never lets the camera into a wall or a door leaf.
struct IndoorArmBlend {
    float blend = 0.0f, vel = 0.0f;
    float tau = 0.3f;            // s: spring w = 2 / tau (about 0.7 s for the whole change)
    bool valid = false;
    void reset(bool indoor) {
        blend = indoor ? 1.0f : 0.0f;
        vel = 0.0f;
        valid = true;
    }
    void step(bool indoor, float dt);
};
ThirdPersonParams indoorArmParams(ThirdPersonParams outdoor, float blend);

}  // namespace ts
