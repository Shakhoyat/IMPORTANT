#include "scene/Gait.h"

#include <algorithm>
#include <cmath>

#include "math/Math3D.h"

namespace ts {

namespace {

constexpr float kTwoPi = 2.0f * PI;

float fract(float x) { return x - std::floor(x); }
float smooth(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Pelvis drop (relative to the standing hip height) that keeps the longer leg's sole on the floor: used by the
// poses whose feet are planted (FK only, no IK).
void settle(Pose& p) {
    const GaitParams g;
    float reach = 0.0f;
    for (int i = 0; i < 2; ++i) {
        float x = 0.0f, y = 0.0f;
        legFK(radians(p.hip[i]), radians(p.knee[i]), g.thigh, g.shin, x, y);
        reach = std::fmax(reach, -y);
    }
    p.pelvisY = reach - (g.thigh + g.shin);
}

// Ankle angle that keeps the foot level (sole parallel to the floor).
void levelFeet(Pose& p) {
    for (int i = 0; i < 2; ++i) p.ankle[i] = p.knee[i] - p.hip[i];
}

float pelvisBob(const GaitParams& g, float phase) {
    return -g.sink + g.bob * std::cos(2.0f * kTwoPi * (phase - 0.5f * g.duty));
}

}  // namespace

GaitParams GaitParams::walk() { return {}; }

GaitParams GaitParams::run() {
    GaitParams g;
    g.speed = 3.4f; g.stride = 2.0f; g.duty = 0.42f; g.lift = 0.18f; g.bob = 0.035f; g.sink = 0.10f;
    g.reachBack = 0.10f; g.heelRise = 0.10f; g.armSwingDeg = 35.0f; g.lean = 10.0f;
    return g;
}

bool inStance(const GaitParams& g, float phase, int leg, float* stanceFraction) {
    const float p = fract(phase - 0.5f * static_cast<float>(leg));
    if (stanceFraction) *stanceFraction = p < g.duty ? p / g.duty : (p - g.duty) / (1.0f - g.duty);
    return p < g.duty;
}

void footInPelvis(const GaitParams& g, float phase, int leg, float& x, float& y) {
    const float half = 0.5f * g.stride * g.duty;
    const float xf = half - g.reachBack, xb = -half - g.reachBack;   // touch-down / toe-off positions
    float s = 0.0f, worldY = kAnkleHeight;
    if (inStance(g, phase, leg, &s)) {
        x = xf - (xf - xb) * s;                                       // world-locked: moves back at body speed
        worldY += g.heelRise * smooth((s - 0.65f) / 0.35f);
    } else {
        x = xb + (xf - xb) * (s - std::sin(kTwoPi * s) / kTwoPi);     // cycloid: zero relative speed at both ends
        worldY += g.heelRise * (1.0f - s) * (1.0f - s) + g.lift * std::sin(PI * s);
    }
    y = worldY - (g.hipHeight + pelvisBob(g, phase));
}

bool legIK(float x, float y, float L1, float L2, float& hip, float& knee) {
    const float dRaw = std::sqrt(x * x + y * y);
    const float dMin = std::fabs(L1 - L2) + 1e-4f, dMax = (L1 + L2) * 0.9995f;
    const float d = clampf(dRaw, dMin, dMax);
    const float cosKnee = clampf((L1 * L1 + L2 * L2 - d * d) / (2.0f * L1 * L2), -1.0f, 1.0f);
    knee = PI - std::acos(cosKnee);                                  // 0 = straight leg
    const float cosA = clampf((L1 * L1 + d * d - L2 * L2) / (2.0f * L1 * d), -1.0f, 1.0f);
    hip = std::atan2(x, -y) + std::acos(cosA);                       // knee in front of the hip-ankle line
    return dRaw >= dMin && dRaw <= dMax;
}

void legFK(float hip, float knee, float L1, float L2, float& x, float& y) {
    x = L1 * std::sin(hip) + L2 * std::sin(hip - knee);
    y = -L1 * std::cos(hip) - L2 * std::cos(hip - knee);
}

Pose gaitPose(const GaitParams& g, float phase) {
    Pose p;
    phase = fract(phase);
    p.pelvisY = pelvisBob(g, phase);
    for (int leg = 0; leg < 2; ++leg) {
        float x = 0.0f, y = 0.0f, h = 0.0f, k = 0.0f, s = 0.0f;
        footInPelvis(g, phase, leg, x, y);
        legIK(x, y, g.thigh, g.shin, h, k);
        p.hip[leg] = degrees(h);
        p.knee[leg] = degrees(k);
        const bool stance = inStance(g, phase, leg, &s);
        const float rise = stance ? g.heelRise * smooth((s - 0.65f) / 0.35f) : g.heelRise * (1.0f - s) * (1.0f - s);
        const float toeDown = degrees(std::asin(clampf(rise / 0.16f, 0.0f, 1.0f)));   // foot pivots on the toe
        p.ankle[leg] = p.knee[leg] - p.hip[leg] - toeDown;
    }
    // Arms swing against the legs: the left arm is forward when the right leg is (phase 0.5).
    const float c = std::cos(kTwoPi * phase);
    p.shoulder[0] = -g.armSwingDeg * c;
    p.shoulder[1] = g.armSwingDeg * c;
    for (int i = 0; i < 2; ++i) p.elbow[i] = 12.0f + 0.6f * std::fmax(0.0f, p.shoulder[i]) + (g.speed > 2.5f ? 60.0f : 0.0f);
    p.spineYaw = 5.0f * c * g.armSwingDeg / 18.0f;   // shoulders counter-rotate against the pelvis
    p.spinePitch = g.lean;
    p.pelvisRoll = 2.0f * std::sin(kTwoPi * phase);
    p.headPitch = -0.5f * g.lean;
    return p;
}

Pose idlePose(float t) {
    Pose p;
    const float breathe = std::sin(kTwoPi * t / 4.2f), shift = std::sin(kTwoPi * t / 9.0f);
    p.hip[0] = 2.0f + 1.5f * shift;  p.knee[0] = 4.0f + 3.0f * std::fmax(0.0f, shift);
    p.hip[1] = 2.0f - 1.5f * shift;  p.knee[1] = 4.0f + 3.0f * std::fmax(0.0f, -shift);
    p.shoulder[0] = p.shoulder[1] = 4.0f + 1.0f * breathe;
    p.elbow[0] = p.elbow[1] = 12.0f;
    p.spinePitch = 1.0f + 0.8f * breathe;
    p.pelvisRoll = 1.5f * shift;
    p.headYaw = 18.0f * std::sin(kTwoPi * t / 11.0f);
    p.headPitch = 2.0f * std::sin(kTwoPi * t / 7.0f);
    levelFeet(p);
    settle(p);
    return p;
}

Pose guardPose(float t) {
    Pose p;
    // Rifle at the low ready, slow head scan with dwell at both ends (tanh-squashed sine).
    const float scan = std::tanh(2.2f * std::sin(kTwoPi * t / 10.0f)) / std::tanh(2.2f);
    p.headYaw = 55.0f * scan;
    p.spineYaw = 0.25f * p.headYaw;
    p.headPitch = 3.0f;
    p.shoulder[1] = 35.0f; p.elbow[1] = 78.0f;    // trigger hand
    p.shoulder[0] = 52.0f; p.elbow[0] = 55.0f;    // support hand on the handguard
    p.hip[0] = 4.0f; p.hip[1] = -2.0f; p.knee[0] = 6.0f; p.knee[1] = 4.0f;
    p.spinePitch = 2.0f + 0.6f * std::sin(kTwoPi * t / 4.0f);
    p.pelvisRoll = 1.0f;
    levelFeet(p);
    settle(p);
    return p;
}

Pose workPose(WorkKind kind, float t) {
    Pose p;
    switch (kind) {
        case WorkKind::Wrench: {   // bent over the vehicle, ratcheting with the right hand
            const float r = std::sin(kTwoPi * 1.2f * t);
            p.spinePitch = 32.0f;
            p.hip[0] = p.hip[1] = 10.0f;
            p.knee[0] = p.knee[1] = 14.0f;
            p.shoulder[1] = 72.0f + 14.0f * r; p.elbow[1] = 55.0f + 28.0f * r;
            p.shoulder[0] = 62.0f; p.elbow[0] = 28.0f;   // left hand braced on the hull
            p.headPitch = 18.0f;
            p.headYaw = 6.0f * std::sin(kTwoPi * t / 5.0f);
            break;
        }
        case WorkKind::CarryCrate: {   // lifting / setting down a crate (squat and rise, 3 s cycle)
            const float b = 0.5f - 0.5f * std::cos(kTwoPi * t / 3.0f);
            p.spinePitch = 10.0f + 40.0f * b;
            p.hip[0] = p.hip[1] = 12.0f + 38.0f * b;
            p.knee[0] = p.knee[1] = 16.0f + 62.0f * b;
            p.shoulder[0] = p.shoulder[1] = 38.0f + 32.0f * b;
            p.elbow[0] = p.elbow[1] = 72.0f - 34.0f * b;
            p.headPitch = 10.0f + 15.0f * b;
            break;
        }
        case WorkKind::Inspect: {   // checking equipment: head scanning up and down, notepad in the right hand
            p.headPitch = 8.0f + 18.0f * std::sin(kTwoPi * t / 6.0f);
            p.headYaw = 22.0f * std::sin(kTwoPi * t / 9.0f);
            p.spineYaw = 0.3f * p.headYaw;
            p.shoulder[1] = 58.0f; p.elbow[1] = 72.0f + 6.0f * std::sin(kTwoPi * t / 3.0f);
            p.shoulder[0] = 6.0f; p.elbow[0] = 15.0f;
            p.spinePitch = 8.0f + 4.0f * std::sin(kTwoPi * t / 6.0f);
            const float shift = std::sin(kTwoPi * t / 8.0f);
            p.hip[0] = 2.0f + shift; p.hip[1] = 2.0f - shift; p.knee[0] = 5.0f; p.knee[1] = 5.0f;
            break;
        }
    }
    levelFeet(p);
    settle(p);
    return p;
}

Pose sitPose() {
    Pose p;
    p.hip[0] = p.hip[1] = 90.0f;        // thighs horizontal
    p.knee[0] = p.knee[1] = 90.0f;      // shins vertical
    levelFeet(p);
    const GaitParams g;
    p.pelvisY = 0.45f + 0.07f - g.hipHeight;   // hip joint on a 0.45 m seat (+ thigh radius)
    p.spinePitch = 6.0f;
    p.shoulder[0] = p.shoulder[1] = 28.0f;     // hands resting on the thighs
    p.elbow[0] = p.elbow[1] = 55.0f;
    p.headPitch = 8.0f;
    return p;
}

Pose duckPose(float t) {
    Pose p;
    const float w = smooth(t / 0.25f);        // drops into the crouch within a quarter second
    p.hip[0] = p.hip[1] = 75.0f * w;
    p.knee[0] = p.knee[1] = 125.0f * w;
    p.spinePitch = 38.0f * w;
    p.headPitch = 30.0f * w;
    p.shoulder[0] = p.shoulder[1] = 140.0f * w;   // forearms over the helmet
    p.elbow[0] = p.elbow[1] = 115.0f * w;
    levelFeet(p);
    settle(p);
    return p;
}

Pose crouchPose(float t) {
    Pose p;
    p.hip[0] = p.hip[1] = 58.0f;
    p.knee[0] = p.knee[1] = 100.0f;
    p.spinePitch = 18.0f;
    p.headPitch = -10.0f;
    p.headYaw = 25.0f * std::sin(kTwoPi * t / 7.0f);
    p.shoulder[1] = 48.0f; p.elbow[1] = 80.0f;
    p.shoulder[0] = 62.0f; p.elbow[0] = 50.0f;
    levelFeet(p);
    settle(p);
    return p;
}

Pose wavePose(float t) {
    Pose p = idlePose(t);
    p.shoulder[1] = 165.0f;
    p.elbow[1] = 25.0f + 28.0f * std::sin(kTwoPi * 1.6f * t);
    p.headYaw = 0.0f;
    p.headPitch = -4.0f;
    return p;
}

Pose blendPose(const Pose& a, const Pose& b, float w) {
    w = clampf(w, 0.0f, 1.0f);
    Pose p;
    for (int i = 0; i < 2; ++i) {
        p.hip[i] = lerpf(a.hip[i], b.hip[i], w);
        p.knee[i] = lerpf(a.knee[i], b.knee[i], w);
        p.ankle[i] = lerpf(a.ankle[i], b.ankle[i], w);
        p.shoulder[i] = lerpf(a.shoulder[i], b.shoulder[i], w);
        p.elbow[i] = lerpf(a.elbow[i], b.elbow[i], w);
    }
    p.spinePitch = lerpf(a.spinePitch, b.spinePitch, w);
    p.spineYaw = lerpf(a.spineYaw, b.spineYaw, w);
    p.pelvisY = lerpf(a.pelvisY, b.pelvisY, w);
    p.pelvisRoll = lerpf(a.pelvisRoll, b.pelvisRoll, w);
    p.headYaw = lerpf(a.headYaw, b.headYaw, w);
    p.headPitch = lerpf(a.headPitch, b.headPitch, w);
    p.bodyPitch = lerpf(a.bodyPitch, b.bodyPitch, w);
    p.bodyRoll = lerpf(a.bodyRoll, b.bodyRoll, w);
    return p;
}

// ---------------------------------------------------------------- v8 barracks life

Pose sleepPose(float t) {
    Pose p;
    const float breathe = std::sin(kTwoPi * t / 4.8f);
    p.bodyPitch = -90.0f;                    // on the back, head towards local -z (the pillow), feet towards +z
    p.pelvisY = 0.12f - GaitParams{}.hipHeight;
    p.hip[0] = 4.0f;  p.knee[0] = 8.0f;  p.ankle[0] = -25.0f;
    p.hip[1] = 10.0f; p.knee[1] = 22.0f; p.ankle[1] = -20.0f;   // one knee a little up
    p.shoulder[0] = 5.0f; p.elbow[0] = 14.0f;                   // arms relaxed along the sides (the elbow only
    p.shoulder[1] = 9.0f; p.elbow[1] = 22.0f;                   // bends in the sagittal plane: no hand on the chest)
    p.spinePitch = 1.5f * breathe;
    p.headPitch = 14.0f;                                        // the pillow lifts the head
    p.headYaw = 12.0f * std::sin(kTwoPi * t / 37.0f);
    return p;
}

Pose pushUpPose(float t) {
    Pose p;
    const float e = 0.5f - 0.5f * std::cos(kTwoPi * t / 1.8f);   // 0 arms straight .. 1 chest near the floor
    const float hip = lerpf(0.42f, 0.27f, e);                    // pelvis height above the floor
    p.bodyPitch = lerpf(64.0f, 74.0f, e);                        // face down, toes on the floor
    p.pelvisY = hip - GaitParams{}.hipHeight;
    p.ankle[0] = p.ankle[1] = 48.0f;
    p.shoulder[0] = p.shoulder[1] = p.bodyPitch - 4.0f - 34.0f * e;   // hands under the shoulders
    p.elbow[0] = p.elbow[1] = lerpf(4.0f, 98.0f, e);
    p.headPitch = 18.0f;
    return p;
}

Pose pullUpPose(float t) {
    Pose p;
    const float e = 0.5f - 0.5f * std::cos(kTwoPi * t / 2.6f);   // 0 hanging .. 1 chin over the bar
    p.pelvisY = 0.15f + 0.33f * e;
    p.shoulder[0] = p.shoulder[1] = lerpf(176.0f, 150.0f, e);
    p.elbow[0] = p.elbow[1] = lerpf(4.0f, 122.0f, e);
    p.hip[0] = 14.0f; p.knee[0] = 48.0f;                         // feet crossed behind, off the floor
    p.hip[1] = 8.0f;  p.knee[1] = 58.0f;
    p.ankle[0] = p.ankle[1] = -20.0f;
    p.headPitch = -12.0f + 10.0f * e;
    p.spinePitch = -4.0f;
    return p;
}

Pose sitUpPose(float t) {
    Pose p;
    const float c = std::sin(kTwoPi * t / 2.2f), e = c > 0.0f ? c : 0.0f;   // crunch up, then rest on the mat
    p.bodyPitch = -90.0f;
    p.pelvisY = 0.11f - GaitParams{}.hipHeight;
    p.hip[0] = p.hip[1] = 62.0f;                                 // knees up, feet flat on the mat
    p.knee[0] = p.knee[1] = 118.0f;
    p.ankle[0] = p.ankle[1] = 40.0f;
    p.spinePitch = 68.0f * e;
    p.shoulder[0] = p.shoulder[1] = 34.0f;                       // arms crossed over the chest
    p.elbow[0] = p.elbow[1] = 125.0f;
    p.headPitch = 18.0f + 10.0f * e;
    return p;
}

Pose talkPose(float t, bool seated) {
    Pose p = seated ? sitPose() : idlePose(t);
    // Gestures come in bursts: the talker uses the right hand, sometimes both, between quieter listening spells.
    const float burst = smooth(0.5f + 0.5f * std::sin(kTwoPi * t / 7.0f) * 1.6f);
    const float g = std::sin(kTwoPi * t * 0.9f), g2 = std::sin(kTwoPi * t * 1.3f + 1.0f);
    p.shoulder[1] = lerpf(p.shoulder[1], 34.0f + 14.0f * g, burst);
    p.elbow[1] = lerpf(p.elbow[1], 78.0f + 22.0f * g2, burst);
    p.shoulder[0] = lerpf(p.shoulder[0], p.shoulder[0] + 10.0f * g2, 0.5f * burst);
    p.headYaw = 22.0f * std::sin(kTwoPi * t / 9.0f) + 6.0f * g;
    p.headPitch = (seated ? 6.0f : 0.0f) + 5.0f * std::sin(kTwoPi * t * 0.7f) * burst;   // nods
    p.spineYaw = 0.2f * p.headYaw;
    return p;
}

// ---------------------------------------------------------------- v7 (WP-M) arm IK

Vec3 eulerYXZFromBasis(const Vec3& x, const Vec3& y, const Vec3& z) {
    // M = Ry Rx Rz: M[1][2] = -sin(pitch), M[0][2] / M[2][2] = tan(yaw), M[1][0] / M[1][1] = tan(roll).
    const float pitch = std::asin(clampf(-z.y, -1.0f, 1.0f));
    return {degrees(pitch), degrees(std::atan2(z.x, z.z)), degrees(std::atan2(x.y, y.y))};
}

ArmIk armIK(const Vec3& S, const Vec3& T, const Vec3& pole, float upper, float lower) {
    ArmIk r;
    const Vec3 st = T - S;
    float d = length(st);
    const Vec3 e = d > 1e-6f ? st * (1.0f / d) : Vec3{0.0f, -1.0f, 0.0f};
    const float dMax = upper + lower - 1e-4f, dMin = std::fabs(upper - lower) + 1e-4f;
    if (d > dMax) { d = dMax; r.reached = false; }
    if (d < dMin) { d = dMin; r.reached = false; }
    // Elbow side: the pole's component perpendicular to the shoulder -> hand line.
    Vec3 pp = pole - e * dot(pole, e);
    if (length(pp) < 1e-5f) pp = Vec3{0.0f, -1.0f, 0.0f} - e * (-e.y);
    if (length(pp) < 1e-5f) pp = Vec3{0.0f, 0.0f, -1.0f} - e * (-e.z);
    pp = normalize(pp);
    const float cosA = clampf((upper * upper + d * d - lower * lower) / (2.0f * upper * d), -1.0f, 1.0f);
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    const Vec3 u = normalize(e * cosA + pp * sinA);   // upper arm direction
    const Vec3 elbow = S + u * upper;
    const Vec3 v = normalize(S + e * d - elbow);      // forearm direction
    const float c = clampf(dot(u, v), -1.0f, 1.0f);
    r.elbowDeg = degrees(std::acos(c));
    Vec3 zAxis = v - u * c;                            // the forearm bends towards the shoulder's +Z
    zAxis = length(zAxis) > 1e-5f ? normalize(zAxis) : normalize(pp * -1.0f - u * dot(pp * -1.0f, u));
    const Vec3 yAxis = u * -1.0f;
    const Vec3 xAxis = cross(yAxis, zAxis);
    r.shoulderDeg = eulerYXZFromBasis(xAxis, yAxis, zAxis);
    return r;
}

}  // namespace ts
