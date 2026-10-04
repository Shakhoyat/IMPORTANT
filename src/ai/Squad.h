// Two-UAV patrol squad (v5): the camp's air defence against intruder rovers.
//   PATROL   the two UAVs fly the compound 180 degrees apart on a breathing orbit, cameras sweeping the ground
//   TRACK    every frame each UAV's thermal blobs become ground measurements; one shared MultiTracker fuses them
//            (Kalman filter + Hungarian assignment + two-view triangulation when both see the same rover)
//   ENGAGE   the most threatening confirmed track (closest to a camp asset) is the target. The UAV that sees it
//            best is LEAD, the other WING flies ~90 degrees around the target (good triangulation geometry and a
//            second attack axis); both gimbals stay on the predicted target position
//   STRIKE   when both have held the target (lead >= 1.0 s, wing >= 0.6 s) they make a COMBINED strike: each
//            round's flight time is predicted, the farther UAV launches first and the nearer one later so both
//            rounds arrive together (time on target); each round homes on its own laser spot = the predicted
//            target position at its arrival time (lead from the Kalman velocity)
// The squad only sees what the cameras see; it never reads rover positions.
#pragma once

#include <vector>

#include "ai/DroneController.h"
#include "scene/GameEvents.h"
#include "vision/CvPipeline.h"
#include "vision/MultiTracker.h"

namespace ts {

enum class SquadRole { Patrol, Lead, Wing, Manual };
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

constexpr float kSquadLeadLock = 1.0f, kSquadWingLock = 0.6f, kSquadSoloLock = 1.8f;
constexpr float kSquadWingWait = 4.0f;     // s the lead waits for the wing's lock before a solo strike
constexpr float kSquadStandOff = 6.5f, kSquadEngageAlt = 5.0f;
constexpr float kSquadPatrolAlt = 7.5f;

// Predicted flight time of a rail round over a straight distance d: launch at 8 m/s, 130 m/s^2 for 0.4 s,
// then 60 m/s (Combat's missile profile), plus 0.08 s of turning.
float roundFlightTime(float d);

class Squad {
public:
    static constexpr int kCount = 2;
    DroneState drones[kCount];
    SquadRole roles[kCount] = {SquadRole::Patrol, SquadRole::Patrol};
    float lock[kCount] = {0.0f, 0.0f};  // s each UAV has held the current target

    Squad();
    void setObstacles(const std::vector<Box>& b);
    void setKeepOut(const Box& b) { keepOut_ = b; hasKeepOut_ = true; }
    void setFriendly(const std::vector<Vec3>& p) { friendly_ = p; }
    void setAssets(const std::vector<Vec3>& p) { assets_ = p; }   // what the squad protects (threat ranking)
    void ignoreNear(const Vec3& p, float seconds) { ignore_.push_back({p, seconds}); }

    // One simulation step. manual[i]: UAV i is flown by the user (only its camera contributes).
    // roundsReady[i]: rail rounds UAV i can release. Launch orders and game events are appended.
    void update(float dt, float time, const SquadFrame& frame, const bool manual[kCount], const int roundsReady[kCount],
                std::vector<LaunchOrder>& orders, std::vector<GameEvent>& events);

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
    void engage(float dt, const Track& t, int lead, int wing, const bool manual[kCount]);
    bool ignored(const Vec3& p) const;
    float threat(const Track& t) const;
    void planStrike(float time, const Track& t, int lead, int wing, const bool manual[kCount], const int ready[kCount],
                    std::vector<GameEvent>& events);

    DroneController ctl_[kCount];
    MultiTracker tracker_;
    StrikeState strike_;
    Vec3 designator_[kCount];
    Vec3 camPos_[kCount];
    std::vector<Vec3> friendly_, assets_;
    struct Ignore { Vec3 p; float t; };
    std::vector<Ignore> ignore_;
    Box keepOut_;
    bool hasKeepOut_ = false;
    int confirmedSeen_ = 0;  // highest track id already announced (EnemySpotted)
};

}  // namespace ts
