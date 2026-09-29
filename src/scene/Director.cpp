#include "scene/Director.h"

#include "math/Frustum.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

float ease(float x) {  // smooth start and stop of camera moves
    x = clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

Vec3 onOrbit(const Vec3& centre, float angleDeg, float radius, float height) {
    const float a = radians(angleDeg);
    return centre + Vec3{std::sin(a) * radius, height, std::cos(a) * radius};
}

struct Segment {
    const char* name;
    float duration;
};

constexpr Segment kSegments[] = {
    {"ESTABLISHING", 10.0f}, {"GATE APPROACH", 8.0f}, {"UAV CHASE", 8.0f},  {"GROUND LEVEL", 8.0f},
    {"UAV THERMAL FEED", 6.0f}, {"WATCHTOWER", 7.0f}, {"THERMAL WIDE", 7.0f}, {"LOW ANGLE", 8.0f},
    {"PULL BACK", 9.0f},
};
constexpr int kSegmentCount = static_cast<int>(sizeof(kSegments) / sizeof(kSegments[0]));

// True if nothing solid (occluder parts, bounding spheres) lies within 0.25 m of the segment eye -> target.
bool clearView(const Scene& scene, const Vec3& eye, const Vec3& target) {
    const Vec3 d = target - eye;
    const float len2 = dot(d, d);
    for (const SceneNode* n : scene.leaves()) {
        if (!n->occluder || n->object == "Target Rover") continue;
        const Vec3 c = transformPoint(n->world, {0, 0, 0});
        const float r = unitPrimitiveRadius(n->world) + 0.25f;
        const float t = clampf(dot(c - eye, d) / std::max(len2, 1e-6f), 0.0f, 1.0f);
        if (length(eye + d * t - c) < r) return false;
    }
    return true;
}

}  // namespace

float directorLength() {
    float total = 0.0f;
    for (const Segment& s : kSegments) total += s.duration;
    return total;
}

DirectorShot directorShot(const Scene& scene, float t) {
    t = std::fmod(std::max(t, 0.0f), directorLength());
    int index = 0;
    while (index < kSegmentCount - 1 && t >= kSegments[index].duration) t -= kSegments[index++].duration;
    const float len = kSegments[index].duration;
    const float u = t / len;  // 0..1 inside the shot

    DirectorShot s;
    s.name = kSegments[index].name;
    s.fade = clampf(std::min(t, len - t) / 0.35f, 0.0f, 1.0f);
    const Vec3 uav = transformPoint(scene.uav().root->world, {0, 0, 0});
    const Vec3 rover = scene.rover.position + Vec3{0, 0.2f, 0};
    const float uavYaw = radians(scene.drone.yawDeg);
    const Vec3 uavFwd{std::sin(uavYaw), 0, std::cos(uavYaw)};
    const Vec3 bunker{0, 1.2f, 0};

    switch (index) {
        case 0:  // crane down onto the compound
            s.eye = onOrbit({0, 0, 0}, lerpf(215.0f, 160.0f, ease(u)), lerpf(36.0f, 25.0f, ease(u)), lerpf(15.0f, 7.5f, ease(u)));
            s.target = lerp(Vec3{0, 2.0f, 0}, bunker, ease(u));
            s.fovDeg = 42.0f;
            break;
        case 1: {  // dolly in along the vehicle track, past the lamp posts
            const float z = lerpf(21.0f, 10.5f, ease(u));
            s.eye = {trackCentreX(z) - 0.4f, terrainHeight(trackCentreX(z), z) + 1.25f, z};
            s.target = {0.0f, 1.1f, 1.8f};
            s.fovDeg = 48.0f;
            break;
        }
        case 2:  // behind and above the UAV, looking where its light points
            s.eye = uav - uavFwd * 2.4f + Vec3{0, 0.8f, 0};
            s.target = uav + uavFwd * 3.0f - Vec3{0, 2.2f, 0};
            s.fovDeg = 55.0f;
            break;
        case 3: {  // low camera beside the rover, the UAV's beam above it: first clear angle, then a slow swing
            const float toCentre = std::atan2(-rover.x, -rover.z);
            float base = toCentre;
            for (int k = 0; k < 12; ++k) {
                const float a = toCentre + radians(30.0f * static_cast<float>((k + 1) / 2) * (k % 2 ? 1.0f : -1.0f));
                const Vec3 e = rover + Vec3{std::sin(a) * 2.2f, 0.35f, std::cos(a) * 2.2f};
                if (clearView(scene, e, rover)) {
                    base = a;
                    break;
                }
            }
            const float a = base + radians(lerpf(-12.0f, 12.0f, u));
            s.eye = rover + Vec3{std::sin(a) * 2.2f, 0.35f, std::cos(a) * 2.2f};
            s.target = rover + (uav - rover) * 0.12f;
            s.fovDeg = 50.0f;
            break;
        }
        case 4:
            s.uavCamera = true;
            s.thermal = true;
            break;
        case 5: {  // from the watchtower platform over the compound
            s.eye = {8.5f, terrainHeight(10.5f, 10.0f) + 4.4f, 8.0f};  // just off the platform corner, facing the compound
            s.target = lerp(bunker, Vec3{0, 0.5f, 0}, 0.3f) + Vec3{(1.0f - ease(u)) * 2.0f, 0, 0};
            s.fovDeg = 46.0f;
            break;
        }
        case 6:  // wide thermal orbit: the heat picture of the whole scene
            s.eye = onOrbit({0, 0, 0}, lerpf(40.0f, 75.0f, u), 26.0f, 13.0f);
            s.target = lerp(bunker, rover, 0.5f);
            s.thermal = true;
            s.fovDeg = 45.0f;
            break;
        case 7: {  // low hero angle from the fire barrel towards the bunker, slow push in
            const Vec3 from{4.2f, 0.0f, 17.0f}, to{3.2f, 0.0f, 13.5f};
            s.eye = lerp(from, to, ease(u));
            s.eye.y = terrainHeight(s.eye.x, s.eye.z) + 0.7f;
            s.target = lerp(bunker + Vec3{0, 1.5f, 0}, uav, 0.25f);
            s.fovDeg = 40.0f;
            break;
        }
        default:  // pull back and up to the whole valley
            s.eye = onOrbit({0, 0, 0}, lerpf(95.0f, 140.0f, u), lerpf(12.0f, 44.0f, ease(u)), lerpf(3.0f, 22.0f, ease(u)));
            s.target = lerp(bunker, Vec3{0, 0.5f, 0}, ease(u));
            s.fovDeg = 45.0f;
            break;
    }
    if (!s.uavCamera) s.eye.y = std::max(s.eye.y, terrainHeight(s.eye.x, s.eye.z) + 0.15f);
    return s;
}

}  // namespace ts
