#pragma once

#include <vector>

#include "scene/Scene.h"
#include "scene/WalkSpace.h"

namespace ts {

struct CameraView {
    Mat4 view, proj;
    Vec3 position;
    float nearZ = 0.05f, farZ = 600.0f;
};

enum class CameraMode { Orbit, Free, Chase, UavCamera, Cinematic, Walk, Possess };
const char* cameraModeName(CameraMode m);

class Camera {
public:
    CameraMode mode = CameraMode::Orbit;

    // Orbit camera (default): spherical coordinates around a target point.
    // Defaults frame the whole 20 m disc with the fence, watchtower and forest patches.
    Vec3 target{0.0f, 0.8f, 0.0f};
    float yawDeg = 32.0f, pitchDeg = 27.0f, distance = 34.0f;
    // Free-fly camera.
    Vec3 freePos{0.0f, 4.5f, 27.0f};
    float freeYawDeg = 180.0f, freePitchDeg = -10.0f;
    float fovDeg = 50.0f;
    float directorTime = 0.0f;  // seconds into the cinematic sequence (Cinematic mode)
    // First-person walk camera: on the ground, eye 1.7 m above the terrain.
    float walkX = 0.0f, walkZ = 12.0f;
    float walkYawDeg = 180.0f, walkPitchDeg = 0.0f;  // yaw 180 = looking towards -Z (the bunker)
    float walkEyeY = 0.0f;       // smoothed eye height (world Y, without head bob)
    float walkBobPhase = 0.0f;   // radians
    float walkSpeed = 0.0f;      // measured ground speed, m/s (drives the head bob)
    float walkFeetY = 0.0f;      // floor height under the walker (ramp / bunker floors from the WalkSpace)
    // Walkable world, set by the App every frame: floors / walls / indoor rooms and the extra solids (closed doors,
    // lowered barrier). Null = open terrain only.
    const WalkSpace* walkSpace = nullptr;
    std::vector<Box> walkExtra;
    // Possessed controllable (CameraMode::Possess): index into Scene's controllables, framing from its CameraHint.
    int possessed = -1;
    bool possessSight = false;   // true: first-person sight / cockpit, false: chase view
    static constexpr float kWalkEyeHeight = 1.7f, kWalkFovDeg = 60.0f, kWalkMaxRadius = 52.0f;
    static constexpr float kWalkSpeed = 1.6f, kWalkRunSpeed = 4.5f, kWalkBodyRadius = 0.3f, kWalkHeadClearance = 0.15f;

    CameraView compute(const Scene& scene, float aspect) const;
    void drag(float dxPixels, float dyPixels);
    void zoom(float wheelSteps);
    void moveFree(float forward, float right, float up);  // metres
    void cycle();
    // Walk: forward/right in [-1, 1]; follows the WalkSpace floors (ramp, tunnel), collides with its walls and the
    // extra solids, clamps the eye under the ceiling and the position to the world radius (52 m).
    void moveWalk(float forward, float right, float dt, bool run);
    // Teleport; feetY < -1e8 (default) = the terrain height, otherwise the floor level to start from (bunker rooms).
    void placeWalk(float x, float z, float feetY = -1e9f);
    float walkGroundY(float x, float z) const;   // floor height under (x, z) for the walker's current feet level
};

// Camera shake: small rotation jitter (amount 0..1 from Combat::shake), deterministic in 'time'. At most ~1.2 deg.
void applyShake(CameraView& v, float amount, float time);

// The UAV's own thermal camera (also used by the tracker).
CameraView uavCameraView(const Scene& scene, float aspect, float fovYDeg, int uav = 0);

}  // namespace ts
