// UAV autonomy: flight + searchlight gimbal, driven only by what the thermal camera sees.
//   SEARCH    : orbit the compound, sweep the gimbal.
//   TRACK     : visual servoing keeps the hot blob at the image centre; the blob's ground position is
//               estimated by casting the pixel ray onto the terrain; the UAV holds a stand-off orbit around it.
//   REACQUIRE : target lost (e.g. hidden behind the bunker): circle the last estimate to regain line of sight.
#pragma once

#include <vector>

#include "ai/Occlusion.h"
#include "vision/ThermalTracker.h"

namespace ts {

constexpr int kCvWidth = 320;   // 320 x 240 LWIR core (e.g. FLIR Boson 320 class)
constexpr int kCvHeight = 240;
constexpr float kCvFovYDeg = 40.0f;

enum class DroneMode { Search, Track, Reacquire };
const char* droneModeName(DroneMode m);

struct DroneState {
    Vec3 position{0.0f, 5.5f, 11.0f};
    Vec3 velocity{0.0f, 0.0f, 0.0f};
    float yawDeg = 180.0f, pitchDeg = 0.0f, rollDeg = 0.0f;  // body attitude
    float gimbalYawDeg = 0.0f, gimbalPitchDeg = 50.0f;      // relative to the body
    DroneMode mode = DroneMode::Search;
    Vec3 targetEstimate{0.0f, 0.0f, 0.0f};
    bool hasEstimate = false;
    float timeSinceSeen = 1e3f;
    int framesSeen = 0, framesLost = 0;
    // Launcher (game logic): a round is released once the target has stayed in the camera for kLockTime.
    float lockTime = 0.0f;       // seconds the current target has been held continuously
    float launchCooldown = 0.0f; // seconds until the next round may be released
    bool launchRequest = false;  // set by the controller, consumed (and cleared) by the scene
    bool manual = false;         // flown by the user: only the vision bookkeeping runs
    int launched = 0;
};

constexpr float kLockTime = 1.5f;        // s of continuous track before a launch
constexpr float kLaunchInterval = 4.0f;  // s between rounds
constexpr float kWreckIgnoreRadius = 2.5f, kWreckIgnoreTime = 30.0f;
constexpr float kFriendlyRadius = 4.5f;

class DroneController {
public:
    void setObstacles(const std::vector<Box>& boxes) { obstacles_ = boxes; }

    // cameraWorld: world matrix of the gimbal pitch node for the frame the detection was computed from.
    void update(float dt, float time, const Detection& det, const Mat4& cameraWorld, DroneState& s);

    // Ray through a normalised image point (u right, v up) of the gimbal camera, intersected with the terrain.
    // heightAbove: stop the ray that far above the terrain (a hot body's centre, not the ground under it).
    static bool imagePointToGround(float u, float v, const Mat4& cameraWorld, Vec3& hit, float heightAbove = 0.0f);
    // A destroyed target stays hot for a while: detections whose ground point lies near it are ignored.
    void ignoreNear(const Vec3& point, float seconds) { ignore_.push_back({point, seconds}); }
    // Friend-or-foe: hot blobs within kFriendlyRadius of a friendly vehicle (tank, helicopter, AA gun...) are
    // never targets. Refreshed by the scene every frame.
    void setFriendly(const std::vector<Vec3>& positions) { friendly_ = positions; }
    // Rounds are not released at targets inside this keep-out box (the bunker entrance ramp).
    void setKeepOut(const Box& b) { keepOut_ = b; hasKeepOut_ = true; }
    // Flight primitives for the squad (v5): fly towards a goal with obstacle avoidance and the boundary clamp,
    // turn the body towards a point, and the world direction of a camera pixel ray.
    void flyTo(float dt, const Vec3& goal, float maxSpeed, DroneState& s) { flyTowards(dt, goal, maxSpeed, s); }
    void faceTowards(float dt, const Vec3& target, float rateDeg, DroneState& s) const;
    static Vec3 pixelRay(float u, float v, const Mat4& cameraWorld);
    // Smoothly turns the gimbal towards a world point (used by manual flight with gimbal assist).
    void aimGimbalAt(float dt, const Vec3& target, DroneState& s) const { pointGimbalAt(dt, target, s); }

private:
    void flyTowards(float dt, const Vec3& goal, float maxSpeed, DroneState& s);
    void pointGimbalAt(float dt, const Vec3& target, DroneState& s) const;
    void yawTowards(float dt, float desiredDeg, float rateDeg, DroneState& s) const;

    std::vector<Box> obstacles_;
    float orbitAngle_ = 1.57f;
    struct Ignore { Vec3 point; float timeLeft; };
    std::vector<Ignore> ignore_;
    std::vector<Vec3> friendly_;
    Box keepOut_;
    bool hasKeepOut_ = false;
};

}  // namespace ts
