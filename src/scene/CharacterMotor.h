// v9 M3: character motor of the possessed soldier (pure CPU, tests/test_v9_m3.cpp). One call per fixed 1/60 s tick:
//   input (forward / strafe axes, heading, sprint, crouch, jump edge)
//   -> stance blends (sprint, crouch: 0.2 s smoothstep ramps; the capsule height follows the crouch blend)
//   -> wish velocity -> critically damped velocity response (zeta = 1, separate accelerate / brake rates, ~20 % in the air)
//   -> slope limit (no walking up ground steeper than 40 deg; steps are filtered out of the slope estimate)
//   -> jump with coyote time (0.12 s after walking off a ledge) and a jump buffer (a press up to 0.15 s early fires on
//      the first grounded tick)
//   -> scene/WalkSpace stepFoot (walls, solids, step-up below 0.45 m, ground follow, gravity, landing)
//   -> step smoothing: a step up / down moves the collision feet at once but the drawn feet follow on a critically damped
//      spring (no popping).
// Everything is a function of the tick inputs and dt: no wall clock, so a replay reproduces a run bit for bit.
#pragma once

#include <vector>

#include "ai/Occlusion.h"
#include "math/Math3D.h"
#include "scene/WalkSpace.h"

namespace ts {

// Critically damped spring (zeta = 1) towards a constant goal: x'' = -2 w x' - w^2 (x - goal). Exact solution over dt
// (unconditionally stable, no overshoot from rest): with e = x - goal,
//   e(t) = (e0 + (v0 + w e0) t) e^(-w t),   v(t) = (v0 - w (v0 + w e0) t) e^(-w t).
void critDampStep(float& x, float& v, float goal, float omega, float dt);
// Time for the step response 1 - (1 + w t) e^(-w t) to reach 'fraction' (0..1) of the step (Newton on w t).
float critRiseTime(float omega, float fraction);

struct MotorParams {
    float walkSpeed = 1.3f, runSpeed = 3.4f, crouchSpeed = 0.8f, backSpeed = 0.9f;   // m/s (GaitParams walk / run)
    float strafeWalk = 1.17f, strafeRun = 2.6f, strafeCrouch = 0.6f;
    float omegaAccel = 17.0f;   // 1/s: 90 % of a speed step after 3.89 / w = 0.23 s
    float omegaStop = 30.0f;    // 1/s: braking (the wish opposes the velocity): walk -> below 5 cm/s in 0.17 s
    float airControl = 0.2f;    // in the air the response rate is this share of the ground rate (and no braking)
    float blendTime = 0.2f;     // s: sprint and crouch blends
    float coyoteTime = 0.12f;   // s after walking off a ledge that a jump still fires
    float jumpBuffer = 0.15f;   // s a jump press waits for the ground
    float slopeLimitDeg = 40.0f;
    float standHeight = 1.8f, crouchHeight = 1.25f;
    float stepOmega = 16.0f;    // 1/s: drawn feet follow a step up / down
    float stepDetect = 0.04f;   // m of height change beyond a 45 deg slope that counts as a step
};

struct MotorInput {
    float forward = 0.0f;       // -1..1 (W / S)
    float strafe = 0.0f;        // -1..1 (positive = right)
    float yawDeg = 0.0f;        // body heading (0 = +z, 90 = +x; Soldier::yawDeg)
    bool sprint = false, crouch = false;
    bool jumpPressed = false;   // the jump key went down this tick
};

class CharacterMotor {
public:
    MotorParams params;
    FootParams foot;            // body radius, gravity, jump speed, step height (height is set from the crouch blend)

    // Standing still on the ground at (x, feetY, z).
    void reset(float x, float z, float feetY);
    // One tick: 'solids' are the boxes around (low ones are steps, taller ones walls).
    void step(const MotorInput& in, const WalkSpace* ws, const std::vector<Box>& solids, float dt);

    const FootBody& body() const { return body_; }
    Vec3 position() const { return {body_.x, body_.feetY, body_.z}; }
    bool grounded() const { return body_.grounded; }
    Vec3 velocity() const { return {vx_, 0.0f, vz_}; }          // commanded horizontal velocity (m/s)
    Vec3 moved() const { return {mx_, 0.0f, mz_}; }             // actual horizontal displacement of the last tick (m)
    float groundSpeed() const { return speed_; }                 // |moved| / dt
    float distance() const { return distance_; }                 // horizontal path length so far (m)
    float sprintBlend() const;                                   // 0..1 (smoothstep of the ramp)
    float crouchBlend() const;
    float height() const;                                        // capsule height (stand -> crouch)
    float stepOffset() const { return stepOff_; }                // draw the feet at feetY + stepOffset (<= 0 after a step up)
    bool jumped() const { return jumped_; }                      // events of the last tick
    bool landed() const { return landed_; }
    float landSpeed() const { return landSpeed_; }
    bool slopeBlocked() const { return slopeBlocked_; }          // the slope limit removed part of the last step
    float bufferAge() const { return bufferAge_; }               // s since the pending jump press (-1 none)

private:
    FootBody body_;
    float vx_ = 0.0f, vz_ = 0.0f, ax_ = 0.0f, az_ = 0.0f;       // velocity and its rate (spring state)
    float sprintRamp_ = 0.0f, crouchRamp_ = 0.0f;
    float stepOff_ = 0.0f, stepVel_ = 0.0f;
    float mx_ = 0.0f, mz_ = 0.0f, speed_ = 0.0f, distance_ = 0.0f;
    float bufferAge_ = -1.0f;
    bool jumpedInAir_ = false;   // this airborne phase began with a jump (no coyote jump)
    bool jumped_ = false, landed_ = false, slopeBlocked_ = false;
    float landSpeed_ = 0.0f;
};

// Ground gradient (dh/dx, 0, dh/dz) under (x, z) for a walker at feetY: central differences at 0.1, 0.25 and 0.5 m,
// the smallest of the three per axis (0 when they disagree in sign). A true slope gives the same value at every scale;
// a step of height H gives H / (2 e), so the widest scale keeps a 0.45 m step at a 0.45 "slope" (24 deg) and the
// slope limit never mistakes a step (or a floor edge) for a cliff. Floors are looked up from 1 m above the feet.
Vec3 groundSlope(const WalkSpace* ws, float x, float z, float feetY);
// Head room: can a body of 'height' stand at (x, feetY, z) (WalkSpace ceilings, solids overhead)?
bool headroom(const WalkSpace* ws, const std::vector<Box>& solids, float x, float z, float feetY, float radius, float height);

// First-person head bob, camera-frame offset (x right, y up, metres) for the gait phase (cycles) and the actual ground
// speed: two vertical dips per stride (one per foot contact, like the pelvis) and one sideways sway, the amplitude
// growing with speed (1.2 cm at a walk, 3 cm at a sprint). Zero when standing.
Vec3 headBobOffset(float phase, float groundSpeed);

}  // namespace ts
