// v6 human motion (WP-B): walk / run cycles by forward kinematics with two-link leg inverse kinematics, and the
// work / guard / idle poses of the soldiers. Pure functions (no scene graph): unit-testable and plotted in the report.
//
// Sagittal-plane convention: a segment angle is measured from straight down, positive = forward. Thigh angle =
// hip, shin angle = hip - knee (the knee only bends backwards), foot level when ankle = knee - hip.
// Leg 0 (left) touches down at phase 0, leg 1 (right) at phase 0.5. Stance: the ankle moves backwards relative to
// the hip by stride * duty at the body speed (so it stands still in the world); swing: a cycloid forward with a
// sine lift. The pelvis is highest at mid-stance (two bobs per cycle) and the arms swing against the legs.
#pragma once

#include "math/Math3D.h"

namespace ts {

enum class WorkKind { Wrench, CarryCrate, Inspect };

constexpr float kAnkleHeight = 0.08f;   // ankle joint above the sole (m)

struct GaitParams {
    float speed = 1.3f;        // m/s
    float stride = 1.30f;      // m per full cycle (1.30 walk, 2.0 run)
    float duty = 0.60f;        // stance fraction (0.60 walk, 0.42 run)
    float lift = 0.10f;        // swing ankle lift, m
    float bob = 0.02f;         // pelvis vertical bob amplitude, m
    float sink = 0.06f;        // mean pelvis drop below the standing height, m
    float reachBack = 0.09f;   // stance range shifted behind the hip, m (push-off behind the body)
    float heelRise = 0.08f;    // ankle rise at toe-off (heel lifts in the last 35 % of stance), m
    float armSwingDeg = 18.0f;
    float lean = 4.0f;         // forward trunk lean, degrees
    float thigh = 0.46f, shin = 0.44f, hipHeight = 0.98f;   // hip joint height standing = thigh + shin + ankle
    static GaitParams walk();
    static GaitParams run();
    static GaitParams crouchWalk();   // v9 M3: knees bent, hips 0.3 m lower, short steps, rifle up (0.8 m/s)
    float period() const { return stride / (speed > 1e-3f ? speed : 1e-3f); }   // s per cycle
};

// Joint angles in degrees ([0] left, [1] right). Positive hip / shoulder = swing forward; knee / elbow >= 0 = bend.
struct Pose {
    float hip[2] = {0, 0}, knee[2] = {0, 0}, ankle[2] = {0, 0};
    float shoulder[2] = {0, 0}, elbow[2] = {0, 0};
    float spinePitch = 0, spineYaw = 0, pelvisY = 0, pelvisRoll = 0, headYaw = 0, headPitch = 0;
    // v8: whole-body pitch about the pelvis (positive = forward: +90 face down, head ahead; -90 lying on the back,
    // head behind) and roll, for lying / floor exercises. 0 for every v6 / v7 pose.
    float bodyPitch = 0, bodyRoll = 0;
};

Pose gaitPose(const GaitParams& g, float phase);        // phase in cycles (fractional part used)
Pose idlePose(float t);                                 // breathing + weight shift
Pose guardPose(float t);                                // rifle low ready, head scan
Pose workPose(WorkKind kind, float t);                  // looping work motions, feet planted
Pose sitPose();                                         // seat 0.45 m (pelvisY = seat - 0.45 for other seats)
Pose duckPose(float t);                                 // blast reaction (t = s since the blast)
Pose crouchPose(float t);                               // possessed '\': kneeling ready position
Pose wavePose(float t);                                 // possessed '\': waving the right arm
// v8 barracks life. Lying / floor poses set bodyPitch; pelvisY is for a mattress / mat top at 0 (add its height).
Pose sleepPose(float t);                                // on the back, one hand on the chest, slow breathing
Pose pushUpPose(float t);                               // push-ups on the floor (one repetition ~1.8 s)
Pose pullUpPose(float t);                               // hanging from a bar 2.25 m above the floor, pulling up
Pose sitUpPose(float t);                                // on the back, knees bent, crunching up
Pose talkPose(float t, bool seated);                    // chatting: hand gestures, head turns, nods
Pose blendPose(const Pose& a, const Pose& b, float w);  // w = 0 -> a, 1 -> b

// v9 M3 locomotion blend tree: every gait parameter interpolated (t = 0 -> a, 1 -> b).
GaitParams lerpGait(const GaitParams& a, const GaitParams& b, float t);
// The gait for an actual ground speed (m/s) and crouch share (0..1): walk -> run by speed (smoothstep 1.6 .. 3.0 m/s),
// a shorter stride (higher cadence) below the walking speed, then towards the crouch walk. stride / period follow the
// speed, so a phase advanced by distance / stride keeps the stance feet still on the ground at every speed.
GaitParams locomotionGait(float speed, float crouch);
// Weight of the run cycle in locomotionGait at this speed (0 walk .. 1 run).
float runWeight(float speed);

// Stance test for leg 'leg' at gait phase 'phase' (and the fraction of the stance done, 0..1).
bool inStance(const GaitParams& g, float phase, int leg, float* stanceFraction = nullptr);
// Ankle position (forward x, up y) relative to the hip joint for leg 0 (left) / 1 (right) on flat ground.
void footInPelvis(const GaitParams& g, float phase, int leg, float& x, float& y);
// Two-link IK: hip / knee angles (radians) that put the ankle at (x forward, y up) from the hip; false if clamped.
bool legIK(float x, float y, float L1, float L2, float& hip, float& knee);

// v7 (WP-M) two-bone arm IK in the parent (chest) frame: shoulder joint S, target of the hand centre T, pole = the
// direction the elbow points to. The arm hangs along the shoulder's -Y and the forearm bends towards +Z (the soldier
// rig's convention); returns the shoulder Euler angles (pitch X, yaw Y, roll Z: M = Ry Rx Rz) and the elbow bend
// (degrees, >= 0). Targets beyond reach are clamped (reached = false: the arm points straight at them).
struct ArmIk {
    Vec3 shoulderDeg{0.0f, 0.0f, 0.0f};
    float elbowDeg = 0.0f;
    bool reached = true;
};
ArmIk armIK(const Vec3& S, const Vec3& T, const Vec3& pole, float upper, float lower);
// Euler angles (degrees, M = Ry Rx Rz as Transform::matrix) of the rotation with these orthonormal columns.
Vec3 eulerYXZFromBasis(const Vec3& x, const Vec3& y, const Vec3& z);
// Forward kinematics of the same chain (radians): ankle relative to the hip.
void legFK(float hip, float knee, float L1, float L2, float& x, float& y);

}  // namespace ts
