#include "scene/Scene.h"

#include "objects/Backdrop.h"
#include "objects/Environment.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

void collectLeaves(SceneNode& node, std::vector<SceneNode*>& out) {
    if (node.isLeaf()) out.push_back(&node);
    for (auto& c : node.children) collectLeaves(*c, out);
}

void updateNode(SceneNode& node, const Mat4& parentWorld, float explode) {
    Transform t = node.local;
    if (explode > 0.0f && node.explodable) t.translation = t.translation * (1.0f + 0.7f * explode);
    node.world = parentWorld * t.matrix();
    for (auto& c : node.children) updateNode(*c, node.world, explode);
}

constexpr float kBeamMaxLength = 25.0f;  // searchlight ray march range (m)
constexpr float kBeamCoarseStep = 0.2f;
constexpr float kBlinkPeriod = 1.5f, kBlinkOn = 0.5f;  // aviation lights: 0.5 s on every 1.5 s

void collectBounds(const SceneNode& node, std::vector<Box>& out) {
    if (node.isLeaf() && node.obstacle) out.push_back(worldBounds(node.world));
    for (const auto& c : node.children) collectBounds(*c, out);
}

}  // namespace

Scene::Scene()
    : terrain_(buildTerrain()),
      environment_(buildEnvironment()),
      backdrop_(buildBackdrop()),
      bunker_(buildBunker()),
      rover_(buildRover()),
      uav_(buildUav()) {
    for (SceneNode* r : roots()) collectLeaves(*r, leaves_);
    for (SceneNode* n : leaves_) {
        if (n->name == "Aviation light") aviationLights_.push_back(n);
        if (n->material.lightIntensity > 0.0f) practicals_.push_back(n);
    }
    lights.moon.direction = moonLightDirection();
    updateWorld();

    // Static geometry used by both agents: occluders for the rover, all solid parts for UAV collision avoidance.
    std::vector<Occluder> occluders;
    collectOccluders(*bunker_.root, occluders);
    collectOccluders(*environment_, occluders);
    hide_.setStaticOccluders(occluders);
    std::vector<Box> solids;
    collectBounds(*bunker_.root, solids);
    collectBounds(*environment_, solids);
    droneCtl_.setObstacles(solids);

    rover.position.y = terrainHeight(rover.position.x, rover.position.z);
    applyAgentStates();
    updateWorld();
    updateLightsAndBeam();
}

std::vector<SceneNode*> Scene::roots() const {
    return {terrain_.get(), backdrop_.get(), environment_.get(), bunker_.root.get(), rover_.root.get(), uav_.root.get()};
}

void Scene::updateWorld() {
    const Mat4 id = Mat4::identity();
    updateNode(*terrain_, id, 0.0f);
    updateNode(*backdrop_, id, 0.0f);
    updateNode(*environment_, id, 0.0f);  // props are not composites, they are never exploded
    updateNode(*bunker_.root, id, explode);
    updateNode(*rover_.root, id, explode);
    updateNode(*uav_.root, id, explode);
}

void Scene::update(float dt, const Detection& det) {
    time_ += dt;
    lastDetection = det;
    droneCtl_.update(dt, time_, det, cameraWorld(), drone);
    // The rover only knows where the UAV is (it can hear the rotors), not what the camera sees.
    hide_.update(dt, transformPoint(cameraWorld(), {0, 0, 0}), rover);
    updateHeatTrail(dt);
    applyAgentStates();
    updateWorld();
    updateLightsAndBeam();
}

void Scene::updateHeatTrail(float dt) {
    // Cooling: tau = 45 s on dry ground, 15 s when soaked (evaporation carries the heat away).
    heatTrail_.decay(dt, lerpf(45.0f, 15.0f, clampf(wetness, 0.0f, 1.0f)));
    const Vec3 f{std::sin(rover.yaw), 0.0f, std::cos(rover.yaw)}, l{std::cos(rover.yaw), 0.0f, -std::sin(rover.yaw)};
    const Vec3 p = rover.position;
    // Tyre friction: heat proportional to the distance each wheel rolls this step.
    const float wheelZ[3] = {-0.22f, 0.0f, 0.22f};
    for (float z : wheelZ) {
        heatTrail_.deposit(p + f * z + l * (kRoverTrackWidth * 0.5f), 0.05f, 2.2f * std::fabs(rover.leftSpeed) * dt);
        heatTrail_.deposit(p + f * z - l * (kRoverTrackWidth * 0.5f), 0.05f, 2.2f * std::fabs(rover.rightSpeed) * dt);
    }
    // A parked rover's hot motor box warms the ground under it: a warm "parking spot" stays behind.
    if (!rover.moving) heatTrail_.deposit(p - f * 0.14f, 0.22f, 0.06f * dt);
}

void Scene::applyAgentStates() {
    // UAV body, rotors and gimbal (hierarchy: body -> gimbal yaw -> gimbal pitch -> lamp).
    SceneNode& body = *uav_.root;
    body.local.translation = drone.position + Vec3{0.0f, 0.03f * std::sin(1.7f * time_), 0.0f};
    body.local.rotationDeg = {drone.pitchDeg, drone.yawDeg, drone.rollDeg};
    for (int i = 0; i < 4; ++i) {
        const float dir = (i == 0 || i == 3) ? 1.0f : -1.0f;  // diagonal pairs counter-rotate
        uav_.rotors[i]->local.rotationDeg.y = std::fmod(dir * 2400.0f * time_ + 37.0f * i, 360.0f);
    }
    uav_.gimbalYaw->local.rotationDeg.y = drone.gimbalYawDeg;
    uav_.gimbalPitch->local.rotationDeg.x = drone.gimbalPitchDeg;

    // Rover body follows the terrain slope under its wheels.
    SceneNode& rv = *rover_.root;
    const float yaw = rover.yaw;
    const Vec3 f{std::sin(yaw), 0, std::cos(yaw)}, l{std::cos(yaw), 0, -std::sin(yaw)};
    const Vec3 p = rover.position;
    const float hF = terrainHeight(p.x + f.x * 0.25f, p.z + f.z * 0.25f), hB = terrainHeight(p.x - f.x * 0.25f, p.z - f.z * 0.25f);
    const float hL = terrainHeight(p.x + l.x * 0.25f, p.z + l.z * 0.25f), hR = terrainHeight(p.x - l.x * 0.25f, p.z - l.z * 0.25f);
    rv.local.translation = p;
    rv.local.rotationDeg = {-degrees(std::atan2(hF - hB, 0.5f)), degrees(yaw), degrees(std::atan2(hL - hR, 0.5f))};
    for (int i = 0; i < 3; ++i) {
        rover_.wheelsLeft[i]->local.rotationDeg.z = degrees(rover.leftWheelAngle);
        rover_.wheelsRight[i]->local.rotationDeg.z = degrees(rover.rightWheelAngle);
    }
    rover_.scannerPivot->local.rotationDeg.y = std::fmod(90.0f * time_, 360.0f);
    for (int i = 0; i < kIrSensorCount; ++i) rover_.irSensors[i]->material.emissive = rover.sensors[i] ? 1.0f : 0.1f;

    // Bunker: 1 Hz beacon pulse and spinning vent fans.
    const float pulse = 0.5f + 0.5f * std::sin(2.0f * PI * time_);
    bunker_.beaconBulb->material.emissive = 0.35f + 0.65f * pulse;
    lights.beacon.intensity = 0.6f + 1.4f * pulse;
    for (SceneNode* fan : bunker_.fanCaps) fan->local.rotationDeg.z = std::fmod(300.0f * time_, 360.0f);

    // Radio masts: red obstruction lights blink in sync.
    const bool lit = std::fmod(time_, kBlinkPeriod) < kBlinkOn;
    for (SceneNode* l : aviationLights_) l->material.emissive = lit ? 1.0f : 0.08f;
    // Fire: two incommensurate sines + a faster flutter give a non-repeating flicker.
    const float flicker = 0.78f + 0.12f * std::sin(9.1f * time_) + 0.07f * std::sin(23.7f * time_ + 1.3f) +
                          0.03f * std::sin(61.0f * time_);
    const float ph = std::fmod(time_, 1.0f);  // UAV strobe: two 60 ms flashes per second
    const bool flash = ph < 0.06f || (ph > 0.18f && ph < 0.24f);
    for (SceneNode* p : practicals_) {
        if (p->name == "Fire embers") p->material.emissive = flicker;
        if (p->name == "Strobe") p->material.emissive = flash ? 1.5f : 0.05f;
    }
}

void Scene::updateLightsAndBeam() {
    lights.beacon.position = transformPoint(bunker_.beaconBulb->world, {0, 0, 0});

    const Mat4& lamp = uav_.gimbalPitch->world;
    lights.spot.position = transformPoint(lamp, {0, 0, kLampFrontOffset});
    lights.spot.direction = normalize(transformDirection(lamp, {0, 0, 1}));
    lights.spot.innerDeg = kSpotInnerDeg;
    lights.spot.outerDeg = kSpotOuterDeg;

    // Beam length = distance from the lens to the ground along the light axis: ray march in 20 cm steps,
    // then refine the crossing by bisection (about 1 cm accuracy).
    auto below = [this](float t) {
        const Vec3 q = lights.spot.position + lights.spot.direction * t;
        return q.y <= terrainHeight(q.x, q.z);
    };
    float len = kBeamMaxLength;
    for (float t = 0.1f; t < kBeamMaxLength; t += kBeamCoarseStep) {
        if (!below(t)) continue;
        float lo = std::max(0.1f, t - kBeamCoarseStep), hi = t;
        for (int i = 0; i < 5; ++i) {
            const float mid = 0.5f * (lo + hi);
            (below(mid) ? hi : lo) = mid;
        }
        len = hi;
        break;
    }
    beamLength_ = len;
    const float width = 2.0f * len * std::tan(radians(kSpotOuterDeg));
    uav_.beam->local.translation = {0, 0, kLampFrontOffset + 0.5f * len};
    uav_.beam->local.scale = {width, width, len};
    uav_.beam->world = lamp * uav_.beam->local.matrix();
}

}  // namespace ts
