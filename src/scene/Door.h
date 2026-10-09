// v9 M4: hinged doors (pure CPU, tests/test_v9_m4.cpp). A door leaf turns about a vertical hinge axis; its angle follows
// a critically damped spring (zeta = 1, scene/CharacterMotor.h critDampStep) towards 0 (closed) or the open angle, so
// it starts and stops without a jerk and never swings past the stop it is heading for. The leaf can be blocked by a
// body standing in its path (it waits there, like a real door against a person), it is a set of dynamic collision
// boxes for walkers and the camera arm, and it is a trigger: AI walkers whose route crosses the doorway ask it to open
// (request) and it closes on its own a few seconds after the last of them went through.
//
// State machine (target = open / closed, angle theta in [0, openDeg]):
//
//   Closed --toggle / request--> Opening --theta within kSettleDeg of the open stop--> Open (latched on the holder)
//   Open --toggle / auto close--> Closing --theta within kSettleDeg of 0--> Closed (latched)
//   Opening --toggle--> Closing, Closing --toggle / request--> Opening (reversal mid-swing: the spring takes the
//   current angular velocity along, so the leaf decelerates and comes back without a jump)
//
// Plan geometry: the hinge axis stands at 'hinge'; at angle theta the leaf points along u = (sin phi, 0, cos phi) with
// phi = closedYawDeg + swing * theta (heading convention of the scene: 0 = +z, 90 = +x), and its thickness spans
// n in [n0, n1] along n = (cos phi, 0, -sin phi). The leaf occupies u in [u0, u0 + width].
#pragma once

#include <vector>

#include "ai/Occlusion.h"
#include "math/Math3D.h"

namespace ts {

struct HingeParams {
    Vec3 hinge;                    // hinge axis at the bottom of the leaf (x, floor y, z)
    float closedYawDeg = 0.0f;     // heading of the leaf (hinge -> latch edge) when closed
    int swing = 1;                 // +1: opening increases the heading, -1: decreases it
    float openDeg = 90.0f;         // the open stop (door holder), degrees from closed
    float width = 1.0f;            // leaf width (along u)
    float height = 2.0f;           // leaf height
    float bottom = 0.01f;          // gap under the leaf
    float u0 = 0.01f;              // gap between the hinge axis and the leaf's hinge edge
    float n0 = -0.05f, n1 = 0.0f;  // leaf thickness band (n0 < n1) across the axis
    float omega = 4.0f;            // 1/s: natural frequency of the hinge spring (95 % of a swing after 4.74 / omega)
    float autoRadius = 4.5f;       // m: an AI walker whose route crosses the doorway within this distance asks to open
    float closeDelay = 2.0f;       // s after the last request before an auto-opened door closes again
    int segments = 4;              // collision boxes along the leaf (axis-aligned bounds of each piece)
};

// A body that can stand in the leaf's way (vertical cylinder from feet.y up to feet.y + height).
struct DoorBody {
    Vec3 feet;
    float radius = 0.3f;
    float height = 1.8f;
    int id = -1;                   // caller's handle (soldier index); blockedBy() reports it
};

class DoorKinematics {
public:
    enum class State { Closed, Opening, Open, Closing };
    static constexpr float kSettleDeg = 0.6f;     // within this of a stop the leaf latches there
    static constexpr float kSettleSpeed = 8.0f;   // deg/s: ... and moves slower than this
    static constexpr float kReverseAfter = 1.0f;  // s: an auto-opened leaf held up this long by a body turns back

    explicit DoorKinematics(const HingeParams& p = HingeParams{});
    const HingeParams& params() const { return p_; }

    // --- commands ---
    void toggle();                                   // the player (E): open <-> close; a manual open stays open
    void setTarget(bool open, bool manual = true);
    // An AI walker wants through (auto open, auto close later). 'id' = the walker's DoorBody id: an auto-opened door
    // that has not started to move waits while anybody else stands in its sweep (a passer-by in front of the door).
    void request(float time, int id = -1);
    void setBodies(std::vector<DoorBody> bodies) { bodies_ = std::move(bodies); }
    // One tick: spring towards the target, stops, blocking bodies, auto close, state. A second call for the same time
    // is ignored (the garrison steps the doors of its walkers' routes, the scene steps every controllable: whichever
    // comes first in a tick moves the leaf). Time going backwards (a new run) forgets the old requests.
    void step(float time, float dt);

    // --- state ---
    State state() const { return state_; }
    const char* stateName() const;
    float angleDeg() const { return theta_; }
    float velocityDeg() const { return vel_; }        // deg/s
    float openFraction() const { return p_.openDeg > 0.0f ? theta_ / p_.openDeg : 0.0f; }
    bool targetOpen() const { return targetOpen_; }
    bool autoOpened() const { return autoOpened_; }
    bool blocked() const { return blocked_; }
    bool held() const { return held_; }                // auto open waiting for a passer-by to leave the sweep
    int blockedBy() const { return blockedBy_; }      // id of the body that stopped the leaf this tick, -1 none
    float lastRequest() const { return lastRequest_; }
    const std::vector<DoorBody>& bodies() const { return bodies_; }
    void snap(float angleDeg);                        // set the angle at rest (initial state, tests)

    // --- geometry ---
    float yawDeg(float angleDeg) const { return p_.closedYawDeg + static_cast<float>(p_.swing) * angleDeg; }
    float yawDeg() const { return yawDeg(theta_); }
    // Leaf corners in plan (x, z) at an angle: [0] hinge-side n0, [1] latch-side n0, [2] latch-side n1, [3] hinge-side n1.
    void leafCorners(float angleDeg, Vec3 out[4]) const;
    // Axis-aligned bounds of the leaf cut into params().segments pieces (the dynamic collision boxes).
    std::vector<Box> panelBoxes(float angleDeg) const;
    std::vector<Box> panelBoxes() const { return panelBoxes(theta_); }
    std::vector<Box> restOpenBoxes() const { return panelBoxes(p_.openDeg); }   // the leaf on its holder (route planning)
    // Does the leaf at this angle touch the body (plan rectangle vs circle, height bands)?
    bool hits(const DoorBody& b, float angleDeg) const;
    // How deep the leaf at this angle cuts into the body's cylinder (m; <= 0: no contact).
    float penetration(const DoorBody& b, float angleDeg) const;
    // Is a body of 'radius' at p anywhere in the zone the leaf sweeps between its stops?
    bool inSweep(const Vec3& p, float radius) const;
    bool moving() const { return state_ == State::Opening || state_ == State::Closing; }
    // Any body inside the swept zone (the disc the leaf sweeps between its stops) on the swing side?
    bool sweepOccupied() const;
    // Doorway: the closed leaf's line. Centre at handle height, signed distance of a point from the closed leaf
    // (positive on the side the leaf opens to), and whether a plan segment passes through the opening.
    Vec3 doorwayCentre() const;
    float sideOf(const Vec3& p) const;
    bool crossesDoorway(const Vec3& a, const Vec3& b) const;
    bool inDoorway(const Vec3& p, float margin) const;   // within the opening's width and 'margin' of its plane
    // The handle on the face that looks towards 'from' (where the player reaches for the door), at the current angle.
    Vec3 handlePoint(const Vec3& from) const;

private:
    HingeParams p_;
    float theta_ = 0.0f, vel_ = 0.0f;
    bool targetOpen_ = false, autoOpened_ = false, autoMotion_ = false, blocked_ = false, held_ = false;
    int blockedBy_ = -1, reversedFor_ = -1;   // reversedFor_: the body a safety reverse made way for
    float lastRequest_ = -1e30f, lastStep_ = -1e30f, blockedT_ = 0.0f;
    std::vector<int> requesters_;   // ids that asked since the last step
    State state_ = State::Closed;
    std::vector<DoorBody> bodies_;
};

// Something in the scene that owns a hinged door (objects/HingedDoor.h): the garrison finds the doors on the soldiers'
// routes through this interface (dynamic_cast on the scene's controllables).
class DoorPortal {
public:
    virtual ~DoorPortal() = default;
    virtual DoorKinematics& door() = 0;
    virtual const DoorKinematics& door() const = 0;
};

}  // namespace ts
