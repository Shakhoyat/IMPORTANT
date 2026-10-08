// Two-UAV patrol squad (v5): the camp's air defence against intruder rovers.
//   PATROL   the two UAVs fly the compound 180 degrees apart on a breathing orbit, cameras sweeping the ground
//   TRACK    every frame each UAV's thermal blobs become ground measurements; one shared MultiTracker fuses them
//            (Kalman filter + Hungarian assignment + two-view triangulation when both see the same rover)
//   ENGAGE   the most threatening confirmed track (closest to a camp asset) is the target. The UAV that sees it
//            best is LEAD, the other WING flies ~90 degrees around the target (good triangulation geometry and a
//            second attack axis); both gimbals stay on the predicted target position
//   STRIKE   when both have held the target (lead >= 0.8 s, wing >= 0.5 s) they make a COMBINED strike: each
//            round's flight time is predicted, the farther UAV launches first and the nearer one later so both
//            rounds arrive together (time on target); each round homes on its own laser spot = the predicted
//            target position at its arrival time (lead from the Kalman velocity)
// The squad only sees what the cameras see; it never reads rover positions.
// v6: friendly marks carry their own radius (vehicles 4.5 m, soldiers ~1.2 m); a patrol seed randomises the
// orbit (radius, breathing amplitude / rate, sweep) and adds random investigate detours (seed 0 = the v5 orbit).
#pragma once

#include <vector>

#include "ai/DroneController.h"
#include "scene/GameEvents.h"
#include "vision/CvPipeline.h"
#include "vision/MultiTracker.h"

namespace ts {

enum class SquadRole { Patrol, Lead, Wing, Manual, Search };
const char* squadRoleName(SquadRole r);
enum class StrikePhase { None, Tracking, Planned, Inbound };
const char* strikePhaseName(StrikePhase p);

struct SquadFrame {
    std::vector<Measurement2D> blobs[2];  // per UAV, from vision/CvPipeline
    Mat4 camera[2];                       // gimbal camera world matrix of the frame the blobs came from
    bool valid[2] = {false, false};
};

struct LaunchOrder {
    int uav = 0;
    Vec3 aim;            // predicted intercept point
    int designator = 1;  // laser spot id the round homes on
};

struct StrikeState {
    int track = -1;
    StrikePhase phase = StrikePhase::None;
    bool combined = false;
    float started = 0.0f;              // time tracking of this target began
    float launchAt[2] = {-1.0f, -1.0f};
    float flightTime[2] = {0.0f, 0.0f};
    bool launched[2] = {false, false};
    float launchedAt[2] = {0.0f, 0.0f};
    float plannedSpread = 0.0f;        // s between the two planned arrivals
    float cooldown = 0.0f;             // s before the next strike may be planned
};

constexpr float kSquadLeadLock = 0.8f, kSquadWingLock = 0.5f, kSquadSoloLock = 1.3f;
constexpr float kSquadWingWait = 2.5f;     // s the lead waits for the wing's lock before a solo strike
constexpr float kSquadStandOff = 6.5f, kSquadEngageAlt = 5.0f;
constexpr float kSquadPatrolAlt = 7.5f;
constexpr float kSquadReacquireTime = 12.0f;  // s of searching round the last known position
constexpr float kSquadBlobHeight = 0.2f;      // m above the terrain of a rover's hot body (blob ray stops there)

// A friendly object for identification: measurements within r (horizontal) of p are never engaged.
struct FriendlyMark {
    Vec3 p;
    float r = kFriendlyRadius;
};

// Randomised patrol of one UAV (setPatrolSeed); the v5 orbit is baseRadius 11, amplitude 4, breathing 0.11 rad/s.
struct PatrolParams {
    float baseRadius = 11.0f, amplitude = 4.0f, breathing = 0.11f, breathPhase = 0.0f;
    float angularSpeed = 2.6f / 12.5f;  // rad/s round the compound
    float aimSwing = 6.0f, aimRate = 0.6f;
};
constexpr float kDetourMinGap = 15.0f, kDetourMaxGap = 35.0f;   // s between investigate detours
constexpr float kDetourMinTime = 6.0f, kDetourMaxTime = 9.0f;   // s spent on one

// v7.1 (WP-D) day / night duty of each UAV. By day both UAVs sit on their pads in the camp (rotors still, no vision);
// at dusk they spool up and climb out to the patrol; at dawn, once the last intruder has withdrawn, they fly back,
// hover over their pads and descend smoothly (touch-down speed 0.25 m/s).
//   DOCKED -> TAKE-OFF (spool-up 2 s, vertical climb to kPadClimbOut) -> PATROL (the squad: patrol / track / strike)
//   PATROL -> RETURN (flyTo the hover point kPadApproachAlt over the pad) -> LANDING (descent) -> DOCKED (spool-down)
//   duty wanted again during RETURN -> PATROL, during LANDING -> TAKE-OFF (climb from where it is)
enum class UavDuty { Docked, TakeOff, Patrol, Return, Landing };
const char* uavDutyName(UavDuty d);   // "DOCKED", "TAKE-OFF", "PATROL", "RETURN", "LANDING"
struct UavPad {
    Vec3 touchdown;       // UAV root position when landed (pad surface + gear height)
    float yawDeg = 0.0f;  // parked heading
};
constexpr float kPadClimbOut = 4.0f;      // m above the pad: the climb-out hands over to the patrol here
constexpr float kPadApproachAlt = 5.0f;   // m above the pad: hover point before the descent
constexpr float kRotorSpoolUp = 2.0f, kRotorSpoolDown = 3.0f;   // s from still to full speed and back
constexpr float kTakeOffStagger = 2.5f;   // s between the two UAVs' take-offs
constexpr float kTouchdownSpeed = 0.25f, kMaxDescent = 1.4f, kMaxClimb = 1.6f;   // m/s

// Predicted flight time of a rail round over a straight distance d: launch at 8 m/s, 130 m/s^2 for 0.4 s,
// then 60 m/s (Combat's missile profile), detonating 0.5 m short (proximity fuse).
float roundFlightTime(float d);

class Squad {
public:
    static constexpr int kCount = 2;
    DroneState drones[kCount];
    SquadRole roles[kCount] = {SquadRole::Patrol, SquadRole::Patrol};
    float lock[kCount] = {0.0f, 0.0f};  // s each UAV has held the current target

    Squad();
    void setObstacles(const std::vector<Box>& b);
    // True if a round flying straight from 'from' to 'to' clears every static solid (trees, towers, huts).
    bool clearShot(const Vec3& from, const Vec3& to) const;
    void setKeepOut(const Box& b) { keepOut_ = b; hasKeepOut_ = true; }
    void setFriendly(const std::vector<FriendlyMark>& marks) { friendly_ = marks; }
    void setFriendly(const std::vector<Vec3>& p) {  // v5: every mark kFriendlyRadius
        friendly_.clear();
        for (const Vec3& q : p) friendly_.push_back({q, kFriendlyRadius});
    }
    const std::vector<FriendlyMark>& friendlyMarks() const { return friendly_; }
    bool friendlyAt(const Vec3& p) const;  // p lies within a friendly mark's radius
    // 0 = the v5 orbit; otherwise randomised orbits and investigate detours from this seed.
    void setPatrolSeed(uint32_t seed);
    const PatrolParams& patrolParams(int uav) const { return patrol_[uav]; }
    bool investigating(int uav) const { return detourUntil_[uav] > lastTime_; }
    int detours() const { return detours_; }   // investigate detours started so far
    void setAssets(const std::vector<Vec3>& p) { assets_ = p; }   // what the squad protects (threat ranking)
    void ignoreNear(const Vec3& p, float seconds) { ignore_.push_back({p, seconds}); }

    // One simulation step. manual[i]: UAV i is flown by the user (only its camera contributes).
    // roundsReady[i]: rail rounds UAV i can release. Launch orders and game events are appended.
    void update(float dt, float time, const SquadFrame& frame, const bool manual[kCount], const int roundsReady[kCount],
                std::vector<LaunchOrder>& orders, std::vector<GameEvent>& events);

    // --- v7.1 (WP-D) duty ---
    void setPads(const UavPad pads[kCount]);
    bool hasPads() const { return hasPads_; }
    const UavPad& pad(int i) const { return pads_[i]; }
    // Night (and dusk / a withdrawal still going on): the UAVs are wanted in the air. Default true (= v6 behaviour).
    void setDutyWanted(bool on) { dutyWanted_ = on; }
    bool dutyWanted() const { return dutyWanted_; }
    // Puts both UAVs on their pads (DOCKED, rotors still) - scene start by day - or on the pads with the engines
    // starting (TAKE-OFF) - scene start at dusk.
    void dockAll(bool takingOff);
    UavDuty duty(int i) const { return duty_[i]; }
    float rotorSpool(int i) const { return spool_[i]; }   // 0 still .. 1 full speed
    float heightAbovePad(int i) const;                     // m above the touchdown point (pads set), else 1e3
    bool airborne(int i) const;                            // off the pad (or flown by hand above it)
    bool visionActive(int i) const { return duty_[i] == UavDuty::Patrol; }   // only a patrolling UAV detects

    // A rover was destroyed at 'where': drop its track, ignore the hot wreck, end the strike.
    void onKill(const Vec3& where);

    const MultiTracker& tracker() const { return tracker_; }
    const StrikeState& strike() const { return strike_; }
    int targetTrack() const { return strike_.track; }
    bool designatorValid(int uav) const;
    Vec3 designatorPoint(int uav) const { return designator_[uav]; }
    Vec3 cameraPosition(int uav) const;  // gimbal camera position used for the last frame

private:
    void patrol(float dt, float time, int i);
    void reacquire(float dt, float time, int i);  // circle the last known position (target behind cover)
    void engage(float dt, const Track& t, int lead, int wing, const bool manual[kCount]);
    bool ignored(const Vec3& p) const;
    float threat(const Track& t) const;
    void planStrike(float time, const Track& t, int lead, int wing, const bool manual[kCount], const int ready[kCount],
                    std::vector<GameEvent>& events);

    DroneController ctl_[kCount];
    std::vector<Box> obstacles_;
    float detour_[kCount] = {0.0f, 0.0f};  // extra orbit angle / height while a UAV has no clear shot
    MultiTracker tracker_;
    StrikeState strike_;
    Vec3 designator_[kCount];
    Vec3 camPos_[kCount];
    std::vector<Vec3> assets_;
    std::vector<FriendlyMark> friendly_;
    // v6 seeded patrol
    float prnd();
    uint32_t patrolSeed_ = 0, prng_ = 1;
    PatrolParams patrol_[kCount];
    float nextDetour_[kCount] = {-1.0f, -1.0f}, detourUntil_[kCount] = {-1.0f, -1.0f};
    Vec3 detourPoint_[kCount];
    int detours_ = 0;
    float lastTime_ = 0.0f;
    struct Ignore { Vec3 p; float t; };
    std::vector<Ignore> ignore_;
    Box keepOut_;
    bool hasKeepOut_ = false;
    int confirmedSeen_ = 0;  // highest track id already announced (EnemySpotted)
    Vec3 lastKnown_;
    float lastKnownTime_ = -1e9f;
    // v7.1 duty
    void dutyTransitions(float time, const bool manual[kCount]);
    void dutyFlight(float dt, int i);
    UavPad pads_[kCount];
    bool hasPads_ = false, dutyWanted_ = true;
    UavDuty duty_[kCount] = {UavDuty::Patrol, UavDuty::Patrol};
    float spool_[kCount] = {1.0f, 1.0f};
    float takeOffAt_[kCount] = {0.0f, 0.0f};
    bool wasManual_[kCount] = {false, false};
};

}  // namespace ts
