#include "scene/Camera.h"

#include "objects/Uav.h"
#include "scene/Director.h"

namespace ts {

namespace {
constexpr float kOrbitMinDistance = 2.0f, kOrbitMaxDistance = 110.0f;  // up to a view of the whole backdrop
constexpr float kFreeZoomStep = 1.0f;                                 // metres per wheel step in free mode
}  // namespace

const char* cameraModeName(CameraMode m) {
    switch (m) {
        case CameraMode::Orbit: return "ORBIT";
        case CameraMode::Free: return "FREE";
        case CameraMode::Chase: return "CHASE";
        case CameraMode::UavCamera: return "UAV CAM";
        case CameraMode::Cinematic: return "CINEMATIC";
    }
    return "?";
}

CameraView uavCameraView(const Scene& scene, float aspect, float fovYDeg) {
    const Mat4& cam = scene.cameraWorld();
    CameraView v;
    v.position = transformPoint(cam, {0, 0, kLampFrontOffset + 0.03f});
    const Vec3 forward = normalize(transformDirection(cam, {0, 0, 1}));
    const Vec3 up = normalize(transformDirection(cam, {0, 1, 0}));
    v.nearZ = 0.05f;
    v.farZ = 60.0f;
    v.view = lookAt(v.position, v.position + forward, up);
    v.proj = perspective(radians(fovYDeg), aspect, v.nearZ, v.farZ);
    return v;
}

CameraView Camera::compute(const Scene& scene, float aspect) const {
    if (mode == CameraMode::UavCamera) return uavCameraView(scene, aspect, 40.0f);
    if (mode == CameraMode::Cinematic) {
        const DirectorShot shot = directorShot(scene, directorTime);
        if (shot.uavCamera) return uavCameraView(scene, aspect, 40.0f);
        CameraView v;
        v.position = shot.eye;
        v.view = lookAt(shot.eye, shot.target, {0, 1, 0});
        v.proj = perspective(radians(shot.fovDeg), aspect, v.nearZ, v.farZ);
        return v;
    }

    CameraView v;
    Vec3 eye, center;
    if (mode == CameraMode::Orbit) {
        const float yaw = radians(yawDeg), pitch = radians(pitchDeg);
        eye = target + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)} * distance;
        center = target;
    } else if (mode == CameraMode::Free) {
        const float yaw = radians(freeYawDeg), pitch = radians(freePitchDeg);
        eye = freePos;
        center = freePos + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    } else {  // Chase: behind and above the UAV, looking past it
        const Mat4& body = scene.uav().root->world;
        const Vec3 p = transformPoint(body, {0, 0, 0});
        const float yaw = radians(scene.drone.yawDeg);
        const Vec3 fwd{std::sin(yaw), 0, std::cos(yaw)};
        eye = p - fwd * 1.6f + Vec3{0, 0.55f, 0};
        center = p + fwd * 1.2f - Vec3{0, 0.45f, 0};
    }
    v.position = eye;
    v.view = lookAt(eye, center, {0, 1, 0});
    v.proj = perspective(radians(fovDeg), aspect, v.nearZ, v.farZ);
    return v;
}

void Camera::drag(float dx, float dy) {
    if (mode == CameraMode::Orbit) {
        yawDeg -= dx * 0.3f;
        pitchDeg = clampf(pitchDeg + dy * 0.3f, -5.0f, 85.0f);
    } else if (mode == CameraMode::Free) {
        freeYawDeg -= dx * 0.2f;
        freePitchDeg = clampf(freePitchDeg - dy * 0.2f, -85.0f, 85.0f);
    }
}

void Camera::zoom(float steps) {
    if (mode == CameraMode::Orbit) distance = clampf(distance * std::pow(0.9f, steps), kOrbitMinDistance, kOrbitMaxDistance);
    else if (mode == CameraMode::Free) moveFree(steps * kFreeZoomStep, 0, 0);
    else fovDeg = clampf(fovDeg - steps * 2.0f, 20.0f, 80.0f);
}

void Camera::moveFree(float forward, float right, float up) {
    const float yaw = radians(freeYawDeg), pitch = radians(freePitchDeg);
    const Vec3 f{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    const Vec3 r = normalize(cross(f, {0, 1, 0}));
    freePos = freePos + f * forward + r * right + Vec3{0, up, 0};
}

void Camera::cycle() {
    mode = static_cast<CameraMode>((static_cast<int>(mode) + 1) % 4);  // K selects the cinematic camera
}

}  // namespace ts
