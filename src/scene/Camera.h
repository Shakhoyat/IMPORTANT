#pragma once

#include "scene/Scene.h"

namespace ts {

struct CameraView {
    Mat4 view, proj;
    Vec3 position;
    float nearZ = 0.05f, farZ = 600.0f;
};

enum class CameraMode { Orbit, Free, Chase, UavCamera, Cinematic };
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

    CameraView compute(const Scene& scene, float aspect) const;
    void drag(float dxPixels, float dyPixels);
    void zoom(float wheelSteps);
    void moveFree(float forward, float right, float up);  // metres
    void cycle();
};

// The UAV's own thermal camera (also used by the tracker).
CameraView uavCameraView(const Scene& scene, float aspect, float fovYDeg);

}  // namespace ts
