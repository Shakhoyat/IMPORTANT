// v9 M5: the UAVs' day / night duty cycle (declared in Squad.h, v7.1 WP-D). A finite-state machine per UAV:
//   DOCKED   on its pad, rotors spooling down / still, camera stowed, no vision
//   TAKE-OFF wait for the stagger slot, spool up (kRotorSpoolUp), climb vertically to kPadClimbOut over the pad
//   PATROL   the squad flies it (patrol / track / strike, Squad::update)
//   RETURN   flyTo the hover point kPadApproachAlt over the pad (obstacle avoidance of the squad's controller)
//   LANDING  hold over the pad, descend with a speed that shrinks with the height (touch-down kTouchdownSpeed)
// The player flying a UAV (manual) suspends the machine; on release it resumes with PATROL (duty wanted) or RETURN.
// Everything is a pure function of the previous state, dt and the duty flag: deterministic per tick.
#include <algorithm>
#include <cmath>

#include "ai/Squad.h"

namespace ts {

namespace {

constexpr float kPadCapture = 0.35f;      // m: horizontal distance to the pad that starts the descent
constexpr float kHoverCapture = 0.5f;     // m: height error at the hover point that starts the descent
constexpr float kSettleSpeed = 0.6f;      // m/s: the UAV has to be nearly still over the pad to descend
constexpr float kStowPitchDeg = 12.0f;    // camera stowed (looking slightly down) on the pad
constexpr float kClimbAccel = 1.5f;       // m/s^2 vertical acceleration of the climb-out
constexpr float kLandYawRate = 40.0f;     // deg/s: turn to the parked heading during the descent

float wrapDeg(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

float flatDistance(const Vec3& a, const Vec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

// Level the airframe (pitch / roll back to 0) with the controller's 4 / s rate.
void level(DroneState& s, float dt) {
    s.pitchDeg = lerpf(s.pitchDeg, 0.0f, std::min(1.0f, 4.0f * dt));
    s.rollDeg = lerpf(s.rollDeg, 0.0f, std::min(1.0f, 4.0f * dt));
}

void stowGimbal(DroneState& s, float dt) {
    const float k = std::min(1.0f, 2.0f * dt);
    s.gimbalYawDeg = wrapDeg(s.gimbalYawDeg - wrapDeg(s.gimbalYawDeg) * k);
    s.gimbalPitchDeg = lerpf(s.gimbalPitchDeg, kStowPitchDeg, k);
}

}  // namespace

const char* uavDutyName(UavDuty d) {
    switch (d) {
        case UavDuty::Docked: return "DOCKED";
        case UavDuty::TakeOff: return "TAKE-OFF";
        case UavDuty::Patrol: return "PATROL";
        case UavDuty::Return: return "RETURN";
        case UavDuty::Landing: return "LANDING";
    }
    return "?";
}

void Squad::setPads(const UavPad pads[kCount]) {
    for (int i = 0; i < kCount; ++i) pads_[i] = pads[i];
    hasPads_ = true;
}

void Squad::dockAll(bool takingOff) {
    if (!hasPads_) return;
    for (int i = 0; i < kCount; ++i) {
        DroneState& s = drones[i];
        s.position = pads_[i].touchdown;
        s.velocity = {0.0f, 0.0f, 0.0f};
        s.yawDeg = pads_[i].yawDeg;
        s.pitchDeg = s.rollDeg = 0.0f;
        s.gimbalYawDeg = 0.0f;
        s.gimbalPitchDeg = kStowPitchDeg;
        s.mode = DroneMode::Search;
        duty_[i] = takingOff ? UavDuty::TakeOff : UavDuty::Docked;
        spool_[i] = 0.0f;
        takeOffAt_[i] = lastTime_ + kTakeOffStagger * static_cast<float>(i);
        wasManual_[i] = false;
    }
}

float Squad::heightAbovePad(int i) const { return hasPads_ ? drones[i].position.y - pads_[i].touchdown.y : 1e3f; }

bool Squad::airborne(int i) const { return !hasPads_ || heightAbovePad(i) > 0.02f; }

void Squad::dutyTransitions(float time, const bool manual[kCount]) {
    if (!hasPads_) return;
    for (int i = 0; i < kCount; ++i) {
        const DroneState& s = drones[i];
        const UavPad& pad = pads_[i];
        const float h = heightAbovePad(i), flat = flatDistance(s.position, pad.touchdown);
        if (manual[i]) {   // the player flies it: the machine waits
            wasManual_[i] = true;
            continue;
        }
        if (wasManual_[i]) {   // released: back on duty from wherever the player left it
            wasManual_[i] = false;
            if (dutyWanted_) {
                duty_[i] = (h < kPadClimbOut && flat < 1.0f) ? UavDuty::TakeOff : UavDuty::Patrol;
                takeOffAt_[i] = time;
            } else {
                duty_[i] = (h <= 1e-3f && flat < 0.05f) ? UavDuty::Docked : UavDuty::Return;
            }
            continue;
        }
        switch (duty_[i]) {
            case UavDuty::Docked:
                if (dutyWanted_) {
                    duty_[i] = UavDuty::TakeOff;
                    takeOffAt_[i] = time + kTakeOffStagger * static_cast<float>(i);   // UAV-1 first, UAV-2 after it
                }
                break;
            case UavDuty::TakeOff:
                if (!dutyWanted_) duty_[i] = h <= 1e-3f ? UavDuty::Docked : UavDuty::Landing;
                else if (h >= kPadClimbOut) duty_[i] = UavDuty::Patrol;
                break;
            case UavDuty::Patrol:
                if (!dutyWanted_) duty_[i] = UavDuty::Return;
                break;
            case UavDuty::Return:
                if (dutyWanted_) duty_[i] = UavDuty::Patrol;
                else if (flat < kPadCapture && std::fabs(h - kPadApproachAlt) < kHoverCapture && length(s.velocity) < kSettleSpeed)
                    duty_[i] = UavDuty::Landing;
                break;
            case UavDuty::Landing:
                if (dutyWanted_) {
                    duty_[i] = UavDuty::TakeOff;   // climb again from where it is (already spun up)
                    takeOffAt_[i] = time;
                } else if (h <= 1e-4f && flat < 0.05f) {
                    duty_[i] = UavDuty::Docked;
                }
                break;
        }
    }
}

void Squad::dutyFlight(float dt, int i) {
    if (!hasPads_ || dt <= 0.0f) return;
    DroneState& s = drones[i];
    const UavPad& pad = pads_[i];
    s.mode = DroneMode::Search;
    s.hasEstimate = false;
    roles[i] = SquadRole::Patrol;
    lock[i] = 0.0f;
    switch (duty_[i]) {
        case UavDuty::Docked:
            spool_[i] = std::max(0.0f, spool_[i] - dt / kRotorSpoolDown);
            s.position = pad.touchdown;
            s.velocity = {0.0f, 0.0f, 0.0f};
            s.yawDeg = pad.yawDeg;
            s.pitchDeg = s.rollDeg = 0.0f;
            stowGimbal(s, dt);
            break;
        case UavDuty::TakeOff: {
            const bool onPad = heightAbovePad(i) <= 1e-3f;
            if (lastTime_ < takeOffAt_[i]) {   // waiting for its slot (the other UAV goes first)
                if (onPad) {
                    s.position = pad.touchdown;
                    s.velocity = {0.0f, 0.0f, 0.0f};
                }
                break;
            }
            spool_[i] = std::min(1.0f, spool_[i] + dt / kRotorSpoolUp);
            if (spool_[i] < 1.0f) {   // spinning up: still on the pad (or hovering where a landing was aborted)
                s.velocity = s.velocity * std::exp(-4.0f * dt);
                if (onPad) s.velocity = {0.0f, 0.0f, 0.0f};
                s.position = s.position + s.velocity * dt;
                level(s, dt);
                break;
            }
            // Vertical climb-out: accelerate to kMaxClimb, ease off before the hand-over height.
            const float top = pad.touchdown.y + kPadClimbOut + 0.05f;
            float vy = std::min(kMaxClimb, s.velocity.y + kClimbAccel * dt);
            vy = std::min(vy, 0.3f + 0.8f * std::max(0.0f, top - s.position.y));
            const float damp = std::exp(-3.0f * dt);
            s.velocity = {s.velocity.x * damp, std::max(vy, 0.05f), s.velocity.z * damp};
            s.position = s.position + s.velocity * dt;
            level(s, dt);
            s.gimbalPitchDeg = lerpf(s.gimbalPitchDeg, 45.0f, std::min(1.0f, 1.5f * dt));
            break;
        }
        case UavDuty::Return: {
            spool_[i] = std::min(1.0f, spool_[i] + dt / kRotorSpoolUp);
            const Vec3 hover = pad.touchdown + Vec3{0.0f, kPadApproachAlt, 0.0f};
            ctl_[i].flyTo(dt, hover, 3.2f, s);
            if (flatDistance(s.position, hover) > 1.5f) ctl_[i].faceTowards(dt, hover, 60.0f, s);
            else s.yawDeg = wrapDeg(s.yawDeg + clampf(wrapDeg(pad.yawDeg - s.yawDeg), -kLandYawRate * dt, kLandYawRate * dt));
            ctl_[i].aimGimbalAt(dt, pad.touchdown, s);
            break;
        }
        case UavDuty::Landing: {
            spool_[i] = std::min(1.0f, spool_[i] + dt / kRotorSpoolUp);
            // Hold over the pad (P on the horizontal error), descend at 0.5 m/s per metre of height, at least the
            // touch-down speed and at most kMaxDescent: a smooth flare onto the pad.
            Vec3 err = pad.touchdown - s.position;
            err.y = 0.0f;
            Vec3 vh = err * 1.5f;
            if (length(vh) > 0.5f) vh = normalize(vh) * 0.5f;
            const float h = heightAbovePad(i);
            const float vd = clampf(0.5f * h, kTouchdownSpeed, kMaxDescent);
            s.velocity = {vh.x, -vd, vh.z};
            s.position = s.position + s.velocity * dt;
            if (s.position.y <= pad.touchdown.y) {
                s.position.y = pad.touchdown.y;
                s.velocity.y = 0.0f;
            }
            if (h < 0.3f && flatDistance(s.position, pad.touchdown) < 0.05f) {   // last centimetres: snap onto the pad
                s.position.x = pad.touchdown.x;
                s.position.z = pad.touchdown.z;
            }
            s.yawDeg = wrapDeg(s.yawDeg + clampf(wrapDeg(pad.yawDeg - s.yawDeg), -kLandYawRate * dt, kLandYawRate * dt));
            level(s, dt);
            stowGimbal(s, dt);
            break;
        }
        case UavDuty::Patrol: break;
    }
}

}  // namespace ts
