#include "scene/Camera.h"

#include "objects/Terrain.h"
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
        case CameraMode::Walk: return "WALK";
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
    if (mode == CameraMode::Walk) {
        const float bob = std::sin(walkBobPhase) * 0.025f * clampf(walkSpeed / kWalkSpeed, 0.0f, 2.0f);
        const float yaw = radians(walkYawDeg), pitch = radians(walkPitchDeg);
        eye = {walkX, walkEyeY + bob, walkZ};
        center = eye + Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
        v.position = eye;
        v.view = lookAt(eye, center, {0, 1, 0});
        v.proj = perspective(radians(kWalkFovDeg), aspect, v.nearZ, v.farZ);
        return v;
    }
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
    } else if (mode == CameraMode::Walk) {
        walkYawDeg -= dx * 0.15f;
        walkPitchDeg = clampf(walkPitchDeg - dy * 0.15f, -80.0f, 80.0f);
    }
}

void Camera::zoom(float steps) {
    if (mode == CameraMode::Orbit) distance = clampf(distance * std::pow(0.9f, steps), kOrbitMinDistance, kOrbitMaxDistance);
    else if (mode == CameraMode::Free) moveFree(steps * kFreeZoomStep, 0, 0);
    else if (mode == CameraMode::Walk) return;
    else fovDeg = clampf(fovDeg - steps * 2.0f, 20.0f, 80.0f);
}

void Camera::moveFree(float forward, float right, float up) {
    const float yaw = radians(freeYawDeg), pitch = radians(freePitchDeg);
    const Vec3 f{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
    const Vec3 r = normalize(cross(f, {0, 1, 0}));
    freePos = freePos + f * forward + r * right + Vec3{0, up, 0};
}

float Camera::walkGroundY(float x, float z) const {
    // The terrain disc ends at kTerrainRadius; beyond it the backdrop ground continues at the rim height.
    return std::sqrt(x * x + z * z) <= kTerrainRadius ? terrainHeight(x, z) : terrainRimHeight();
}

void Camera::placeWalk(float x, float z) {
    walkX = x;
    walkZ = z;
    walkEyeY = walkGroundY(x, z) + kWalkEyeHeight;
    walkSpeed = 0.0f;
}

void Camera::moveWalk(float forward, float right, float dt, bool run) {
    const float yaw = radians(walkYawDeg);
    const Vec3 f{std::sin(yaw), 0.0f, std::cos(yaw)};
    const Vec3 r{-std::cos(yaw), 0.0f, std::sin(yaw)};  // to the right of f (same as cross(f, up))
    Vec3 dir = f * forward + r * right;
    const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len > 1.0f) dir = dir * (1.0f / len);
    const float speed = run ? kWalkRunSpeed : kWalkSpeed;
    const float ox = walkX, oz = walkZ;
    walkX += dir.x * speed * dt;
    walkZ += dir.z * speed * dt;
    // Keep out of the bunker.
    float d = std::sqrt(walkX * walkX + walkZ * walkZ);
    if (d < kWalkBunkerRadius) {
        if (d < 1e-4f) { walkX = 0.0f; walkZ = kWalkBunkerRadius; }
        else { walkX *= kWalkBunkerRadius / d; walkZ *= kWalkBunkerRadius / d; }
    }
    d = std::sqrt(walkX * walkX + walkZ * walkZ);
    if (d > kWalkMaxRadius) { walkX *= kWalkMaxRadius / d; walkZ *= kWalkMaxRadius / d; }
    // Smoothed vertical follow and head bob.
    if (dt > 0.0f) {
        const float moved = std::sqrt((walkX - ox) * (walkX - ox) + (walkZ - oz) * (walkZ - oz));
        walkSpeed = lerpf(walkSpeed, moved / dt, std::min(1.0f, dt * 8.0f));
        walkBobPhase += walkSpeed * dt * (run ? 2.6f : 3.4f);
        if (walkBobPhase > 6.2831853f * 100.0f) walkBobPhase -= 6.2831853f * 100.0f;
        const float goal = walkGroundY(walkX, walkZ) + kWalkEyeHeight;
        walkEyeY = lerpf(walkEyeY, goal, std::min(1.0f, dt * 30.0f));
    }
}

void Camera::cycle() {
    // Orbit -> Free -> Chase -> UAV CAM -> Walk -> Orbit (K selects the cinematic camera).
    switch (mode) {
        case CameraMode::Orbit: mode = CameraMode::Free; break;
        case CameraMode::Free: mode = CameraMode::Chase; break;
        case CameraMode::Chase: mode = CameraMode::UavCamera; break;
        case CameraMode::UavCamera:
            mode = CameraMode::Walk;
            walkEyeY = walkGroundY(walkX, walkZ) + kWalkEyeHeight;
            walkSpeed = 0.0f;
            break;
        default: mode = CameraMode::Orbit; break;
    }
}

}  // namespace ts
