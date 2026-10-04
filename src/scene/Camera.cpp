#include "scene/Camera.h"

#include "objects/Terrain.h"
#include "objects/Uav.h"
#include "scene/Director.h"
#include "scene/SceneApi.h"

namespace ts {

namespace {
// Floor under (x, z): the WalkSpace floors inside the terrain disc, the rim height outside it.
float floorAt(const WalkSpace* ws, float x, float z, float feetY) {
    if (std::sqrt(x * x + z * z) > kTerrainRadius) return terrainRimHeight();
    return ws ? ws->floorHeight(x, z, feetY) : terrainHeight(x, z);
}
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
        case CameraMode::Possess: return "CONTROL";
    }
    return "?";
}

CameraView uavCameraView(const Scene& scene, float aspect, float fovYDeg, int uav) {
    const Mat4 cam = scene.cameraWorld(uav);
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

void applyShake(CameraView& v, float amount, float time) {
    if (amount <= 0.0f) return;
    const float a = clampf(amount, 0.0f, 1.0f) * 0.021f;  // 1.2 degrees at full shake
    const float rx = a * (std::sin(time * 47.0f) + 0.5f * std::sin(time * 113.0f + 1.3f)) / 1.5f;
    const float ry = a * (std::sin(time * 59.0f + 0.7f) + 0.5f * std::sin(time * 97.0f)) / 1.5f;
    v.view = rotateZ(0.4f * rx) * rotateX(rx) * rotateY(ry) * v.view;
}

CameraView Camera::compute(const Scene& scene, float aspect) const {
    if (mode == CameraMode::Possess) {
        const std::vector<Controllable*> c = sa::controllables(scene);
        if (possessed >= 0 && possessed < static_cast<int>(c.size()) && c[possessed]) {
            const CameraHint h = c[possessed]->camera(possessSight);
            CameraView v;
            v.position = h.eye;
            v.nearZ = possessSight ? 0.08f : 0.15f;
            v.view = lookAt(h.eye, h.target, {0, 1, 0});
            v.proj = perspective(radians(h.fovDeg), aspect, v.nearZ, v.farZ);
            return v;
        }
    }
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
    return floorAt(walkSpace, x, z, walkFeetY);
}

void Camera::placeWalk(float x, float z, float feetY) {
    walkX = x;
    walkZ = z;
    if (feetY < -1e8f) {
        feetY = std::sqrt(x * x + z * z) <= kTerrainRadius ? terrainHeight(x, z) : terrainRimHeight();
        walkFeetY = feetY;
        feetY = floorAt(walkSpace, x, z, feetY);
    }
    walkFeetY = feetY;
    walkEyeY = feetY + kWalkEyeHeight;
    if (walkSpace) walkEyeY = std::min(walkEyeY, walkSpace->ceilingHeight(x, z, feetY) - kWalkHeadClearance);
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
    float nx = walkX + dir.x * speed * dt, nz = walkZ + dir.z * speed * dt;
    // Walls, closed doors and every other solid; split long steps so a fast frame cannot tunnel through a door.
    if (walkSpace) {
        const float head = walkFeetY + kWalkEyeHeight + 0.1f;
        const float step = std::sqrt((nx - ox) * (nx - ox) + (nz - oz) * (nz - oz));
        const int n = std::max(1, static_cast<int>(std::ceil(step / 0.15f)));
        float cx = ox, cz = oz;
        for (int i = 1; i <= n; ++i) {
            cx = ox + (nx - ox) * static_cast<float>(i) / static_cast<float>(n);
            cz = oz + (nz - oz) * static_cast<float>(i) / static_cast<float>(n);
            walkSpace->collide(cx, cz, walkFeetY, head, kWalkBodyRadius, walkExtra);
            // Feet follow the floor step by step (down the ramp).
            walkFeetY = floorAt(walkSpace, cx, cz, walkFeetY);
        }
        nx = cx;
        nz = cz;
    }
    walkX = nx;
    walkZ = nz;
    float d = std::sqrt(walkX * walkX + walkZ * walkZ);
    if (d > kWalkMaxRadius) { walkX *= kWalkMaxRadius / d; walkZ *= kWalkMaxRadius / d; }
    if (!walkSpace) walkFeetY = walkGroundY(walkX, walkZ);
    // Smoothed vertical follow and head bob.
    if (dt > 0.0f) {
        const float moved = std::sqrt((walkX - ox) * (walkX - ox) + (walkZ - oz) * (walkZ - oz));
        walkSpeed = lerpf(walkSpeed, moved / dt, std::min(1.0f, dt * 8.0f));
        walkBobPhase += walkSpeed * dt * (run ? 2.6f : 3.4f);
        if (walkBobPhase > 6.2831853f * 100.0f) walkBobPhase -= 6.2831853f * 100.0f;
        float goal = walkFeetY + kWalkEyeHeight;
        if (walkSpace) goal = std::min(goal, walkSpace->ceilingHeight(walkX, walkZ, walkFeetY) - kWalkHeadClearance);
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
            walkFeetY = walkGroundY(walkX, walkZ);
            walkEyeY = walkFeetY + kWalkEyeHeight;
            walkSpeed = 0.0f;
            break;
        default: mode = CameraMode::Orbit; break;
    }
}

}  // namespace ts
