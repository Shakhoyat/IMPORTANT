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
};

class DroneController {
public:
    void setObstacles(const std::vector<Box>& boxes) { obstacles_ = boxes; }

    // cameraWorld: world matrix of the gimbal pitch node for the frame the detection was computed from.
    void update(float dt, float time, const Detection& det, const Mat4& cameraWorld, DroneState& s);

    // Ray through a normalised image point (u right, v up) of the gimbal camera, intersected with the terrain.
    static bool imagePointToGround(float u, float v, const Mat4& cameraWorld, Vec3& hit);

private:
    void flyTowards(float dt, const Vec3& goal, float maxSpeed, DroneState& s);
    void pointGimbalAt(float dt, const Vec3& target, DroneState& s) const;
    void yawTowards(float dt, float desiredDeg, float rateDeg, DroneState& s) const;

    std::vector<Box> obstacles_;
    float orbitAngle_ = 1.57f;
};

}  // namespace ts
