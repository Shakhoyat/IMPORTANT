#pragma once

#include "scene/Scene.h"

namespace ts {

struct CameraView {
    Mat4 view, proj;
    Vec3 position;
    float nearZ = 0.05f, farZ = 600.0f;
};

enum class CameraMode { Orbit, Free, Chase, UavCamera, Cinematic, Walk };
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
    static constexpr float kWalkEyeHeight = 1.7f, kWalkFovDeg = 60.0f, kWalkMaxRadius = 45.0f;
    static constexpr float kWalkSpeed = 1.6f, kWalkRunSpeed = 4.5f, kWalkBunkerRadius = 3.3f;

    CameraView compute(const Scene& scene, float aspect) const;
    void drag(float dxPixels, float dyPixels);
    void zoom(float wheelSteps);
    void moveFree(float forward, float right, float up);  // metres
    void cycle();
    // Walk: forward/right in [-1, 1]; moves on the ground, keeps out of the bunker, clamps to the world radius.
    void moveWalk(float forward, float right, float dt, bool run);
    void placeWalk(float x, float z);  // teleport (snaps the eye to the ground)
    float walkGroundY(float x, float z) const;
};

// The UAV's own thermal camera (also used by the tracker).
CameraView uavCameraView(const Scene& scene, float aspect, float fovYDeg);

}  // namespace ts
