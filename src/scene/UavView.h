// v9 M5: game-style UAV views (pure CPU, unit-tested in tests/test_v9_m5.cpp).
//   CHASE   low spring-arm chase camera like a drone game: the follow point and the heading trail the drone through
//           critically damped springs (lag without overshoot), the drone sits on the lower-third line of the frame,
//           the camera banks with the turn rate and pitches with the forward acceleration, and a probe sphere pulls the
//           arm in at once when a solid or the ground comes between the drone and the camera (eases back out)
//   FPV     nose camera: rigidly on the airframe (pitch and roll with it), tilted up like a racing quad's camera
//   GIMBAL  the stabilised thermal / searchlight gimbal camera (the tracker's camera)
// Also the projections of the UAV HUD (pitch ladder rungs, altitude / speed tapes).
#pragma once

#include <vector>

#include "scene/CameraRig.h"

namespace ts {

enum class UavView { Chase = 0, Fpv = 1, Gimbal = 2 };
const char* uavViewName(UavView v);   // "CHASE", "FPV", "GIMBAL"
UavView nextUavView(UavView v);       // chase -> FPV -> gimbal -> chase

// Critically damped spring step (exact for a constant target over dt, stable for any dt): moves x (velocity v)
// towards 'target' with natural frequency omega (rad/s); the step response (1 + w t) e^(-w t) never overshoots.
void springStep(float& x, float& v, float target, float omega, float dt);
void springStep(Vec3& x, Vec3& v, const Vec3& target, float omega, float dt);

struct UavChaseParams {
    float distance = 2.6f;        // m behind the drone (arm length, horizontal)
    float height = 0.80f;         // m above it
    float fovDeg = 64.0f;
    float framing = 0.40f;        // the drone sits this far below the screen centre (NDC): just inside the lower third (> 1/3)
    float followOmega = 10.0f;    // rad/s: follow point (trails a steady flight by 2 v / omega: 1.2 m at 6 m/s)
    float yawOmega = 5.0f;        // rad/s: heading lag
    float bankPerTurn = 0.30f;    // deg of camera roll per deg/s of turn rate (into the turn)
    float maxBank = 15.0f;
    float pitchPerAccel = 1.8f;   // deg of extra camera pitch (nose down) per m/s^2 forward acceleration
    float maxPitchTilt = 8.0f;
    float tiltOmega = 6.0f;       // rad/s: bank / tilt smoothing
    float probe = 0.25f;          // m: probe sphere radius (> the near plane)
    float probeMin = 0.08f;       // m: thin probe for solids the big one already touches at the pivot
    float minBack = 0.85f;        // the follow lag never brings the camera closer than this share of 'distance'
    float armOutTau = 0.35f;      // s: arm extension after a pull-in
    float pivotLift = 0.25f;      // m: probe origin above the drone centre
    float nearZ = 0.12f;
};

// What the chase camera follows this tick.
struct UavChaseInput {
    Vec3 position;            // drone body centre (world)
    Vec3 velocity;            // m/s
    float yawDeg = 0.0f;      // heading (0 = +z, the sim's convention: forward = (sin yaw, 0, cos yaw))
};

class UavChaseCam {
public:
    bool valid() const { return valid_; }
    void invalidate() { valid_ = false; }
    void reset(const UavChaseInput& in, const UavChaseParams& p);
    // Advance the springs by dt (0 = evaluate only) and return the camera pose. 'cast' gives the free length along a
    // ray for the probe sphere (solids, ground); may be empty (no collision).
    RigPose update(const UavChaseInput& in, float dt, const UavChaseParams& p, const ArmCast& cast);
    float arm() const { return arm_; }            // current arm length (pivot -> eye)
    float wantedArm() const { return want_; }     // unobstructed length
    float bankDeg() const { return bank_; }
    float tiltDeg() const { return tilt_; }
    float yawDeg() const { return yaw_; }
    Vec3 follow() const { return follow_; }

private:
    bool valid_ = false;
    Vec3 follow_, followVel_;
    float yaw_ = 0.0f, yawVel_ = 0.0f;
    float bank_ = 0.0f, bankVel_ = 0.0f, tilt_ = 0.0f, tiltVel_ = 0.0f;
    float arm_ = -1.0f, want_ = 0.0f;
    float prevYaw_ = 0.0f;
    Vec3 prevVel_;
};

// Camera orientation that puts 'target' on the screen at NDC (0, -framing) for a vertical field of view fovDeg,
// looking from 'eye' (world up kept, then rolled by rollDeg about the view axis and pitched down by tiltDeg).
Quat framedLook(const Vec3& eye, const Vec3& target, float fovDeg, float framing, float rollDeg = 0.0f, float tiltDeg = 0.0f);
// NDC (x right, y up) of a world point seen by a pose with the given aspect; false if behind the camera.
bool poseNdc(const RigPose& pose, float aspect, const Vec3& p, float& x, float& y);

// FPV nose camera on the airframe 'body' (the UAV root's world matrix, forward +Z): eye at the canopy nose, looking
// along the body forward tilted up by uptiltDeg, rolling and pitching with the airframe.
constexpr float kFpvUptiltDeg = 14.0f, kFpvFovDeg = 92.0f;
RigPose uavFpvPose(const Mat4& body);
// Gimbal camera pose from the gimbal pitch node's world matrix (looks along its +Z), vertical FOV fovDeg.
RigPose uavGimbalPose(const Mat4& gimbalPitchWorld, float fovDeg = 40.0f);

// --- HUD projections ---------------------------------------------------------------------------------------------------
// Attitude of a pose: heading (0..360, 0 = +z north), pitch (deg, + up) and roll (deg, + = right wing down) of the view.
void poseAttitude(const RigPose& p, float& headingDeg, float& pitchDeg, float& rollDeg);
// Pitch ladder: rungs every stepDeg; offset = signed pixels from the screen centre along the view's up axis (+ up) for
// a camera pitched at camPitchDeg with a vertical FOV fovYDeg on a screen 'screenH' pixels tall (exact perspective:
// (H/2) tan(rung - pitch) / tan(fov/2)); only rungs within maxOffset are returned.
struct LadderRung {
    int deg = 0;
    float offset = 0.0f;
};
std::vector<LadderRung> pitchLadder(float camPitchDeg, float fovYDeg, float screenH, int stepDeg, float maxOffset);
// Tape (altitude / speed): marks every 'step' around 'value'; offset = pixels from the tape centre (+ = up = larger).
struct TapeMark {
    float value = 0.0f;
    float offset = 0.0f;
    bool major = false;
};
std::vector<TapeMark> tapeMarks(float value, float step, int majorEvery, float pixelsPerUnit, float halfHeight);
// Lock ring progress 0..1 from the squad's lock time and the lead lock requirement.
float lockProgress(float lockSeconds, float needSeconds);

}  // namespace ts
