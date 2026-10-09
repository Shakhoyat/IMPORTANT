#include "scene/Scene.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "objects/Backdrop.h"
#include "objects/BarracksHall.h"
#include "objects/BaseLayout.h"
#include "objects/Environment.h"
#include "objects/Terrain.h"
#include "objects/UavPad.h"
#include "scene/EyeAdapt.h"
#include "scene/TickInterp.h"
#if __has_include("objects/Base.h")
#include "objects/Base.h"
#define TS_SCENE_HAS_BASE 1
#endif
#if __has_include("objects/Tank.h")
#include "objects/Tank.h"
#define TS_SCENE_HAS_TANK 1
#endif
#if __has_include("objects/Helicopter.h")
#include "objects/Helicopter.h"
#define TS_SCENE_HAS_HELI 1
#endif
#if __has_include("objects/AaGun.h")
#include "objects/AaGun.h"
#define TS_SCENE_HAS_AAGUN 1
#endif
#if __has_include("objects/Launchers.h") && __has_include("objects/Launchers.cpp")  // header + implementation
#include "objects/Launchers.h"
#define TS_SCENE_HAS_LAUNCHERS 1
#endif
#if __has_include("objects/Radar.h")
#include "objects/Radar.h"
#define TS_SCENE_HAS_RADAR 1
#endif

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

void markDynamic(SceneNode& node) {
    node.dynamic = true;
    for (auto& c : node.children) markDynamic(*c);
}

void setVisible(SceneNode& node, bool v) {
    node.visible = v;
    for (auto& c : node.children) setVisible(*c, v);
}

void renameIds(SceneNode& node, const std::string& tag) {  // "UAV-07" -> "UAV2-07"
    if (node.id.size() > 3) node.id = node.id.substr(0, 3) + tag + node.id.substr(3);
    for (auto& c : node.children) renameIds(*c, tag);
}

constexpr float kBeamMaxLength = 25.0f;  // searchlight ray march range (m)
constexpr float kBeamCoarseStep = 0.2f;
constexpr float kBlinkPeriod = 1.5f, kBlinkOn = 0.5f;  // aviation lights: 0.5 s on every 1.5 s
constexpr int kUavOwner[2] = {0, 10};                  // Combat owner ids of the two UAVs

void collectBounds(const SceneNode& node, std::vector<Box>& out) {
    if (node.isLeaf() && node.obstacle && !node.dynamic) out.push_back(worldBounds(node.world));
    for (const auto& c : node.children) collectBounds(*c, out);
}

// Rotation by 'angle' radians about the unit vector 'a' (Rodrigues' formula).
Mat4 axisRotation(const Vec3& a, float angle) {
    const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
    Mat4 r;
    r.at(0, 0) = t * a.x * a.x + c;       r.at(0, 1) = t * a.x * a.y - s * a.z; r.at(0, 2) = t * a.x * a.z + s * a.y;
    r.at(1, 0) = t * a.x * a.y + s * a.z; r.at(1, 1) = t * a.y * a.y + c;       r.at(1, 2) = t * a.y * a.z - s * a.x;
    r.at(2, 0) = t * a.x * a.z - s * a.y; r.at(2, 1) = t * a.y * a.z + s * a.x; r.at(2, 2) = t * a.z * a.z + c;
    return r;
}

// Deterministic pseudo-random numbers for debris (same blast -> same pieces).
float hash01(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFF) / 16777216.0f;
}

bool isWreckCore(const SceneNode& n) {  // parts that stay together as the burning hull
    return n.name == "Chassis" || n.name == "Upper hull" || n.name == "Motor + battery box";
}

Vec3 flat(const Vec3& v) { return {v.x, 0.0f, v.z}; }

}  // namespace

// ---------------------------------------------------------------- UAV 0 as a controllable object
// Manual flight (key 0): W/S forward / back, A/D yaw, PgUp/PgDn climb, arrows / mouse aim the gimbal,
// ENTER releases a rail round at the camera's aim point, BACKSLASH toggles gimbal assist (the gimbal keeps
// following the tracked target while you fly). UAV 1 keeps flying with the squad.
class DroneControl : public Controllable {
public:
    explicit DroneControl(Scene& s) : scene_(s) {}
    const char* name() const override { return "UAV-1 DRONE"; }
    const char* controls() const override {
        return "W/S fly  A/D yaw  PGUP/PGDN climb  ARROWS gimbal  ENTER launch  \\ gimbal assist";
    }
    SceneNode* root() override { return scene_.uavs_[0].root.get(); }
    Vec3 position() const override { return scene_.drone.position; }

    void update(const WorldContext& w, const ControlInput* in) override {
        if (!in) return;  // autonomous: the squad flies
        DroneState& d = scene_.drone;
        const float dt = w.dt;
        // v9 M5: taken over on its pad: the rotors spool up first, the airframe stays put until they are at speed.
        if (scene_.squad_.hasPads() && !scene_.squad_.airborne(0) && scene_.squad_.rotorSpool(0) < 1.0f) {
            d.velocity = {0.0f, 0.0f, 0.0f};
            return;
        }
        const float prevY = d.position.y;
        if (in->actionPressed) assist_ = !assist_;
        d.yawDeg -= in->turn * 70.0f * dt;   // v9 M5: D yaws right (yaw grows towards +x = the UAV's left)
        const float yaw = radians(d.yawDeg);
        const Vec3 fwd{std::sin(yaw), 0, std::cos(yaw)};
        const float top = in->boost ? 9.0f : 6.0f;
        const Vec3 desired = fwd * (in->forward * top) + Vec3{0, in->lift * 3.0f, 0};
        Vec3 dv = desired - d.velocity;
        const float maxDv = 4.0f * dt;
        if (length(dv) > maxDv) dv = normalize(dv) * maxDv;
        const Vec3 accel = dv / std::max(dt, 1e-3f);
        d.velocity = d.velocity + dv;
        d.position = d.position + d.velocity * dt;
        const float ground = terrainHeight(d.position.x, d.position.z);
        // At least 1 m above the ground; v9 M5: lifting off a pad it climbs there at kMaxClimb (no jump up).
        if (d.position.y < ground + 1.0f) {
            d.position.y = std::min(ground + 1.0f, std::max(d.position.y, prevY) + kMaxClimb * dt);
            if (d.velocity.y < 0.0f) d.velocity.y = 0.0f;
        }
        d.position.y = std::min(d.position.y, ground + 40.0f);
        const Vec3 fl{d.position.x, 0, d.position.z};
        if (length(fl) > layout::kPerimeterRadius) {
            const Vec3 c = normalize(fl) * layout::kPerimeterRadius;
            d.position.x = c.x;
            d.position.z = c.z;
        }
        const Vec3 left{std::cos(yaw), 0, -std::sin(yaw)};
        const float tp = clampf(4.0f * dot(d.velocity, fwd) + 3.0f * dot(accel, fwd), -18.0f, 18.0f);
        const float tr = clampf(-4.0f * dot(d.velocity, left) - 3.0f * dot(accel, left), -18.0f, 18.0f);
        d.pitchDeg = lerpf(d.pitchDeg, tp, std::min(1.0f, 4.0f * dt));
        d.rollDeg = lerpf(d.rollDeg, tr, std::min(1.0f, 4.0f * dt));
        const Track* t = scene_.squad_.tracker().find(scene_.squad_.targetTrack());
        if (assist_ && t) {
            scene_.manualCtl_.aimGimbalAt(dt, t->position(), d);
        } else {
            d.gimbalYawDeg -= in->aimYaw * 60.0f * dt + in->aimYawDelta;   // v9 M5: RIGHT / mouse right pan right
            d.gimbalPitchDeg = clampf(d.gimbalPitchDeg - in->aimPitch * 40.0f * dt - in->aimPitchDelta, 5.0f, 88.0f);
        }
        if (in->firePressed) scene_.launchRound();
    }

    CameraHint camera(bool firstPerson) const override {
        CameraHint h;
        const Mat4 cam = scene_.cameraWorld(0);
        if (firstPerson) {
            h.eye = transformPoint(cam, {0, 0, 0.06f});
            h.target = h.eye + normalize(transformDirection(cam, {0, 0, 1})) * 10.0f;
            h.fovDeg = 40.0f;
            h.sight = true;
            return h;
        }
        const DroneState& d = scene_.drone;
        const float yaw = radians(d.yawDeg);
        const Vec3 fwd{std::sin(yaw), 0, std::cos(yaw)};
        h.eye = d.position - fwd * 3.2f + Vec3{0, 1.3f, 0};
        h.target = d.position + fwd * 3.0f - Vec3{0, 0.8f, 0};
        h.fovDeg = 55.0f;
        return h;
    }

    std::string status() const override {
        const DroneState& d = scene_.drone;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "ALT %.1f M  SPD %.1f M/S  AMMO %d  LOCK %.1f S  %s  KILLS %d",
                      d.position.y - terrainHeight(d.position.x, d.position.z), length(d.velocity),
                      scene_.roundsReady(0), scene_.squad_.lock[0], assist_ ? "ASSIST" : "MANUAL", scene_.kills_);
        return buf;
    }

private:
    Scene& scene_;
    bool assist_ = true;
};

// ---------------------------------------------------------------- construction

namespace {

std::vector<std::unique_ptr<Intruder>> makeIntruders() {
    std::vector<std::unique_ptr<Intruder>> v;
    for (int i = 0; i < layout::kMaxRovers; ++i) {
        auto in = std::make_unique<Intruder>();
        in->rig = buildRover();
        in->spawnIndex = i;
        if (i > 0) renameIds(*in->rig.root, std::to_string(i + 1));  // distinct part ids, same object name
        markDynamic(*in->rig.root);
        v.push_back(std::move(in));
    }
    return v;
}

}  // namespace

uint32_t deriveSeed(uint32_t base, uint32_t stream) {
    if (base == 0) return 0;
    uint32_t z = base ^ (stream * 0x9E3779B9u);  // splitmix32
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    z ^= z >> 16;
    return z ? z : 1u;
}

Scene::Scene(const SceneOptions& options)
    : intruders_(makeIntruders()),
      drone(squad_.drones[0]),
      rover(intruders_[0]->state),
      terrain_(buildTerrain()),
      environment_(buildEnvironment()),
      backdrop_(buildBackdrop()),
      bunker_(buildBunker()),
      options_(options),
      dayCycle_(options.startHour, options.autoCycle),
      mission_(options.seed ? deriveSeed(options.seed, 1) : 0x5eed1234u) {
    // v6 random streams: 1 mission, 2 combat effects, 3 squad patrol, 4 garrison (seed 0 = the legacy constants).
    combat_.setSeed(deriveSeed(options.seed, 2));
    squad_.setPatrolSeed(deriveSeed(options.seed, 3));
    const uint32_t garrisonSeed = deriveSeed(options.seed, 4);
    for (int i = 0; i < kUavCount; ++i) {
        uavs_[i] = buildUav();
        if (i > 0) renameIds(*uavs_[i].root, std::to_string(i + 1));
        markDynamic(*uavs_[i].root);
    }

    // Controllable objects, in key order: 0 UAV, then the vehicles, doors, launchers, gate, animated scenery.
    auto ctl = std::make_unique<DroneControl>(*this);
    droneControl_ = ctl.get();
    controllables_.push_back(std::move(ctl));
#ifdef TS_SCENE_HAS_TANK
    controllables_.push_back(makeTank());
#endif
#ifdef TS_SCENE_HAS_HELI
    controllables_.push_back(makeHelicopter());
#endif
#ifdef TS_SCENE_HAS_AAGUN
    controllables_.push_back(makeAaGun());
#endif
#ifdef TS_SCENE_HAS_RADAR
    controllables_.push_back(makeRadar());
#endif
    if (bunker_.doors) controllables_.push_back(std::move(bunker_.doors));
#ifdef TS_SCENE_HAS_LAUNCHERS
    for (auto& o : makeLaunchers()) controllables_.push_back(std::move(o));
#endif
#ifdef TS_SCENE_HAS_BASE
    {
        BaseRig base = buildBase();
        base_ = std::move(base.root);
        for (auto& o : base.objects)
            if (o->possessable()) controllables_.push_back(std::move(o));
        for (auto& o : base.objects)
            if (o) controllables_.push_back(std::move(o));
    }
#endif
    // v8: the enterable barracks hall on the east apron (walk floors, indoor light volume in the camp's WalkSpace).
    // v9 M4: the indoor volume carries a local exposure bias (scene/EyeAdapt.h): inside, the eye may open up further.
    std::unique_ptr<HingedDoor> hallDoor;
    {
        HallRig hall = buildBarracksHall();
        hall_ = std::move(hall.root);
        hallDoor = std::move(hall.door);
        for (const WalkFloor& f : hall.floors) bunker_.walk.addFloor(f);
        bunker_.walk.addIndoor(hall.indoor, IndoorLight{0.24f, 1.0f, kHallExposureBiasEv});
    }
    // v9 M5: the UAVs' landing pads over the bunker (flush: no obstacle, no occluder).
    {
        UavPadRig padRig = buildUavPads();
        uavPads_ = std::move(padRig.root);
        UavPad pads[kUavCount];
        for (int i = 0; i < kUavCount && i < kUavPadCount; ++i) {
            pads[i].touchdown = padRig.touchdown[i];
            pads[i].yawDeg = kUavPadYawDeg;
            for (int k = 0; k < 4; ++k) padLights_[i][k] = padRig.lights[i][k];
        }
        squad_.setPads(pads);
    }
    dayOps_ = options.dayOps;
    // v6: the soldiers and the sky clock come after every v5 object (the v5 key order / indices stay).
    const size_t garrisonStart = controllables_.size();
    if (options.garrison) {
        garrison_ = std::make_unique<Garrison>();
        for (auto& o : garrison_->build(garrisonSeed, std::max(0, options.residents)))
            if (o) controllables_.push_back(std::move(o));
        if (auto clock = makeSkyClock(dayCycle_)) controllables_.push_back(std::move(clock));
    }
    // v9 M4: the hinged doors come last, so every earlier key / index stays as it was.
    if (hallDoor) {
        hallDoor_ = hallDoor.get();
        controllables_.push_back(std::move(hallDoor));
    }
    if (bunker_.generatorDoor) {
        generatorDoor_ = dynamic_cast<DoorPortal*>(bunker_.generatorDoor.get());
        controllables_.push_back(std::move(bunker_.generatorDoor));
    }
    for (size_t i = 1; i < controllables_.size(); ++i)
        if (controllables_[i]->moves() && controllables_[i]->root()) markDynamic(*controllables_[i]->root());

    collectSceneLists();
    lights.moon.direction = moonLightDirection();
    dayCycle_.applyTo(lights);
    refreshDayRules();
    nightRules_ = rules_.wavesAllowed;
    // v9 M5 duty at the start: by day on the pads (rotors still); at dusk, or by day with day ops, on the pads with the
    // engines starting; at night on patrol where the v5 squad starts (the legacy scenes stay exactly as they were).
    squad_.setDutyWanted(dutyWanted());
    if (!squad_.dutyWanted()) squad_.dockAll(false);
    else if (!rules_.wavesAllowed || !rules_.beamsOn) squad_.dockAll(true);

    WorldContext w;
    w.dt = 0.0f;
    w.walk = &bunker_.walk;
    w.hour = dayCycle_.hour();
    w.daylight = rules_.daylight;
    w.beamsOn = rules_.beamsOn;
    for (size_t i = 1; i < garrisonStart; ++i) controllables_[i]->update(w, nullptr);
    updateWorld();

    // Static geometry: occluders for the rovers' planner, solids for UAV avoidance / projectiles / vehicles.
    std::vector<Occluder> occluders;
    collectOccluders(*bunker_.root, occluders);
    collectOccluders(*environment_, occluders);
    collectBounds(*bunker_.root, solids_);
    collectBounds(*environment_, solids_);
    if (base_) collectBounds(*base_, solids_);
    if (hall_) collectBounds(*hall_, solids_);
    squad_.setObstacles(solids_);
    manualCtl_.setObstacles(solids_);
    combat_.setObstacles(solids_);
    squad_.setAssets(Mission::objectives());
    // No rounds at targets on the bunker's entrance ramp (the camp's own doorway).
    squad_.setKeepOut({{-layout::kRampHalfWidth - 3.0f, -10.0f, layout::kBlastDoorZ - 3.0f},
                       {layout::kRampHalfWidth + 3.0f, 10.0f, layout::kRampTopZ + 3.0f}});
    HideController planner;  // the free-cell grid is built once and shared by every slot
    planner.setStaticOccluders(occluders);
    for (auto& in : intruders_) {
        in->hide = planner;
        setVisible(*in->rig.root, false);
    }

    // v6: the garrison plans its routes on the static solids; the soldiers take their posts for startHour.
    if (garrison_) {
        GarrisonContext gc;
        gc.solids = &solids_;
        gc.walk = &bunker_.walk;
        gc.seed = garrisonSeed;
        gc.startHour = options.startHour;
        gc.controllables = &controllables_;  // vehicles, launchers, doors: routes plan around them, soldiers wait at closed doors
        gc.leaves = &leaves_;                // world matrices are current (updateWorld above): exact oriented footprints
        garrison_->init(gc);
    }
    for (size_t i = garrisonStart; i < controllables_.size(); ++i) controllables_[i]->update(w, nullptr);

    // Mission: random routes (v6) draw their via points on the planner's free cells.
    MissionConfig mc;
    mc.randomRoutes = options.randomRoutes;
    mc.legacyFirst = !options.randomRoutes && rules_.wavesAllowed;  // no v3 rover appearing mid-compound at dusk
    mc.freeCell = [this](const Vec3& p) { return intruders_[0]->hide.freeAt(p); };
    mission_.configure(mc);

    applyAgentStates();
    updateWorld();
    updateLightsAndBeam();
    // The first wave's first rover is on the field from the start (where the v3 / v4 rover started) when the
    // scene starts at night; by day the first wave waits for nightfall.
    if (rules_.wavesAllowed) {
        std::vector<SpawnRequest> spawns;
        mission_.update(0.0f, 0, layout::kMaxRovers, spawns, events_, 0.0f);  // starts wave 1
        mission_.update(0.0f, 0, layout::kMaxRovers, spawns, events_, 0.0f);  // its first rover
        for (const SpawnRequest& r : spawns) spawnIntruder(r);
    }
    events_.clear();
    applyAgentStates();
    updateWorld();
}

Scene::~Scene() = default;

void Scene::collectSceneLists() {
    leaves_.clear();
    practicals_.clear();
    aviationLights_.clear();
    for (SceneNode* r : roots()) collectLeaves(*r, leaves_);
    for (SceneNode* n : leaves_) {
        if (n->name == "Aviation light") aviationLights_.push_back(n);
        if (n->material.lightIntensity > 0.0f) practicals_.push_back(n);
    }
}

std::vector<SceneNode*> Scene::roots() const {
    std::vector<SceneNode*> r = {terrain_.get(), backdrop_.get(), environment_.get(), bunker_.root.get()};
    for (const UavRig& u : uavs_) r.push_back(u.root.get());
    if (base_) r.push_back(base_.get());
    if (hall_) r.push_back(hall_.get());
    if (uavPads_) r.push_back(uavPads_.get());
    for (const auto& in : intruders_) r.push_back(in->rig.root.get());
    for (const auto& c : controllables_) {
        SceneNode* n = c->root();
        if (n && !n->parent && std::find(r.begin(), r.end(), n) == r.end()) r.push_back(n);
    }
    return r;
}

std::vector<RoverState> Scene::rovers() const {
    std::vector<RoverState> v;
    for (const auto& in : intruders_) v.push_back(in->state);
    return v;
}

int Scene::aliveIntruders() const {
    int n = 0;
    for (const auto& in : intruders_) n += in->life == Intruder::Life::Alive ? 1 : 0;
    return n;
}

// v9 M5: UAV duty from the squad's state machine (ai/SquadDuty.cpp); a hand-flown UAV reports MANUAL.
bool Scene::uavAirborne(int i) const { return i >= 0 && i < kUavCount && squad_.airborne(i); }

const char* Scene::uavDutyState(int i) const {
    if (i < 0 || i >= kUavCount) return "DOCKED";
    return squad_.drones[i].manual ? "MANUAL" : uavDutyName(squad_.duty(i));
}

bool Scene::uavVisionActive(int i) const {
    if (i < 0 || i >= kUavCount) return false;
    return squad_.drones[i].manual ? squad_.airborne(i) : squad_.visionActive(i);
}

bool Scene::dutyWanted() const { return rules_.beamsOn || dayOps_ || aliveIntruders() > 0; }

void Scene::setDayOps(bool on) {
    dayOps_ = on;
    refreshDayRules();
}

void Scene::updateWorld() {
    for (SceneNode* r : roots()) {
        const bool composite = r == bunker_.root.get() || r->object == "Autonomous UAV" || r->object == "Target Rover" ||
                               r->object.rfind("Soldier ", 0) == 0;
        updateNode(*r, Mat4::identity(), composite ? explode : 0.0f);
    }
    // Pieces of destroyed rovers move on their own (rigid bodies), not with the hierarchy.
    for (auto& in : intruders_) {
        for (const DebrisPiece& p : in->debris) {
            p.node->world = translate(p.position) * p.rotation * translate(p.centre0 * -1.0f) * p.world0;
        }
    }
}

void Scene::captureTick() {
    restoreTick();
    ++tick_;
    for (SceneNode* n : leaves_) {
        n->prevTickWorld = n->tickWorld;
        n->prevTickStamp = n->tickStamp;
        n->tickWorld = n->world;
        n->tickStamp = tick_;
    }
}

int Scene::interpolate(float alpha) {
    restoreTick();
    if (tick_ < 2) return 0;
    for (SceneNode* n : leaves_) {
        if (!n->visible || n->tickStamp != tick_ || n->prevTickStamp + 1 != tick_) continue;
        if (std::memcmp(&n->prevTickWorld, &n->tickWorld, sizeof(Mat4)) == 0) continue;
        if (blendTickWorld(n->prevTickWorld, n->tickWorld, alpha, n->world)) blended_.push_back(n);
    }
    return static_cast<int>(blended_.size());
}

void Scene::restoreTick() {
    for (SceneNode* n : blended_) n->world = n->tickWorld;
    blended_.clear();
}

void Scene::submitVision(int uav, const std::vector<Measurement2D>& blobs, const Mat4& camera) {
    if (uav < 0 || uav >= kUavCount) return;
    vision_.blobs[uav] = blobs;
    vision_.camera[uav] = camera;
    vision_.valid[uav] = true;
}

// ---------------------------------------------------------------- per frame

void Scene::update(float dt, const Detection& det, const ControlInput* input, int possessed) {
    events_.clear();
    time_ += dt;
    lastDetection = det;

    // v6 time of day: clock, phase events, lights / sky; at dawn the wave ends and the rovers withdraw.
    dayCycle_.update(dt, events_, time_);
    dayCycle_.applyTo(lights);
    refreshDayRules();
    if (nightRules_ && !rules_.wavesAllowed) {
        mission_.endNight(events_, time_);
        withdrawIntruders();
    }
    nightRules_ = rules_.wavesAllowed;
    squad_.setDutyWanted(dutyWanted());   // v9 M5: the UAVs launch at dusk / with day ops and land once the day is clear

    // Vision: frames submitted by the app (both UAVs' thermal images); unit tests feed UAV 0's v4 detection.
    if (!vision_.valid[0] && !vision_.valid[1] && det.found) {
        Measurement2D m;
        m.u = det.u;
        m.v = det.v;
        m.area = det.area;
        m.peak = 255;
        vision_.blobs[0] = {m};
        vision_.camera[0] = cameraWorld(0);
        vision_.valid[0] = true;
    }

    // Squad: friendly vehicles, rounds on the rails, the two UAVs' flight, tracking and strikes.
    const bool manualUav0 = possessed >= 0 && possessed < static_cast<int>(controllables_.size()) &&
                            controllables_[possessed].get() == droneControl_;
    std::vector<FriendlyMark> friendly;
    for (const auto& c : controllables_) {  // every camp vehicle / installation (parked ones too), the soldiers
        if (c.get() == droneControl_) continue;
        const float r = c->friendlyRadius();
        if (r > 0.0f) friendly.push_back({c->position(), r});
    }
    squad_.setFriendly(friendly);
    const bool manual[kUavCount] = {manualUav0, false};
    int ready[kUavCount];
    for (int i = 0; i < kUavCount; ++i) ready[i] = roundsReady(i);
    std::vector<LaunchOrder> orders;
    squad_.update(dt, time_, vision_, manual, ready, orders, events_);
    for (const LaunchOrder& o : orders) launchRound(o.uav, o.aim, o.designator);
    vision_ = SquadFrame{};
    for (int i = 0; i < kUavCount; ++i) {
        squad_.drones[i].manual = manual[i];
        if (squad_.designatorValid(i)) combat_.setDesignator(1 + i, squad_.designatorPoint(i), true);
    }

    // Controllable objects (the possessed one gets the user's input).
    WorldContext w;
    w.time = time_;
    w.dt = dt;
    w.combat = &combat_;
    w.obstacles = &solids_;
    w.wetness = wetness;
    w.walk = &bunker_.walk;
    w.hour = dayCycle_.hour();
    w.daylight = rules_.daylight;
    w.beamsOn = rules_.beamsOn;
    for (size_t i = 0; i < controllables_.size(); ++i) {
        controllables_[i]->update(w, static_cast<int>(i) == possessed ? input : nullptr);
    }

    // Rail reload; manual laser spot (id 0) = UAV 0's camera centre.
    for (int u = 0; u < kUavCount; ++u) {
        for (int i = 0; i < kUavMissiles; ++i) {
            if (railReload_[u][i] <= 0.0f) continue;
            railReload_[u][i] -= dt;
            if (railReload_[u][i] <= 0.0f) setVisible(*uavs_[u].missiles[i], true);  // a new round is on the rail
        }
    }
    Vec3 spot;
    const bool spotOk = designatorPoint(spot, 0);
    combat_.setDesignator(0, spot, spotOk);

    // Mission: waves of intruders (night only).
    if (missionOn_ && rules_.wavesAllowed) {
        int freeSlots = 0;
        for (const auto& in : intruders_) freeSlots += in->life == Intruder::Life::Waiting ? 1 : 0;
        std::vector<SpawnRequest> spawns;
        mission_.update(dt, aliveIntruders(), freeSlots, spawns, events_, time_);
        for (const SpawnRequest& r : spawns) spawnIntruder(r);
    }
    updateIntruders(dt);

    std::vector<Target> targets;
    for (size_t i = 0; i < intruders_.size(); ++i) {
        const Intruder& in = *intruders_[i];
        targets.push_back({static_cast<int>(i), in.state.position + Vec3{0, 0.15f, 0}, 0.4f,
                           in.life == Intruder::Life::Alive});
    }
    combat_.setTargets(targets);
    // UAV 1's searchlight is a dynamic spot light (UAV 0's is the shadowed light L2); off by day.
    if (!rules_.beamsOn || !squad_.airborne(1)) {
        combat_.setExtraLights({});
    } else {
        const Mat4& lamp = uavs_[1].gimbalPitch->world;
        DynamicLight l;
        l.position = transformPoint(lamp, {0, 0, kLampFrontOffset});
        l.direction = normalize(transformDirection(lamp, {0, 0, 1}));
        l.coneCos = std::cos(radians(kSpotOuterDeg));
        l.color = lights.spot.color * (lights.spot.intensity * 0.6f);
        l.range = 26.0f;
        combat_.setExtraLights({l});
    }
    combat_.update(dt, time_);
    applyBlasts();

    updateHeatTrail(dt);
    for (int u = 0; u < kUavCount; ++u)   // v9 M5: spooled-down rotors fall behind the full-speed angle
        rotorLag_[u] = std::fmod(rotorLag_[u] + kRotorFullRps * 360.0f * (1.0f - squad_.rotorSpool(u)) * dt, 360.0f);
    applyAgentStates();
    updateWorld();
    updateLightsAndBeam();
}

int Scene::roundsReady(int uav) const {
    int n = 0;
    for (float r : railReload_[uav]) n += r <= 0.0f ? 1 : 0;
    return n;
}

bool Scene::designatorPoint(Vec3& p, int uav) const {
    return DroneController::imagePointToGround(0.0f, 0.0f, cameraWorld(uav), p);
}

bool Scene::launchRound() {
    Vec3 aim;
    if (!designatorPoint(aim, 0)) return false;
    return launchRound(0, aim, 0);
}

bool Scene::launchRound(int uav, const Vec3& aim, int designator) {
    for (int i = 0; i < kUavMissiles; ++i) {
        if (railReload_[uav][i] > 0.0f) continue;
        const Vec3 pos = transformPoint(uavs_[uav].missiles[i]->world, {0, 0, 0.12f});
        if (designator != 0) combat_.setDesignator(designator, aim, true);
        combat_.fireMissile(pos, normalize(aim - pos), designator, kUavOwner[uav]);
        setVisible(*uavs_[uav].missiles[i], false);
        railReload_[uav][i] = kUavReloadTime;
        ++squad_.drones[uav].launched;
        if (designator == 0) {  // manual fire (the squad reports its own launches)
            GameEvent ev;
            ev.type = GameEventType::RoundLaunched;
            ev.position = pos;
            ev.uav = uav;
            pushEvent(ev);
        }
        return true;
    }
    return false;
}

void Scene::applyBlasts() {
    for (const BlastEvent& b : combat_.takeBlasts()) {
        GameEvent ev;
        ev.type = GameEventType::Explosion;
        ev.position = b.position;
        ev.value = static_cast<int>(b.power * 100.0f);
        pushEvent(ev);
        const float radius = Combat::kDamageRadiusPerPower * b.power;
        for (size_t i = 0; i < intruders_.size(); ++i) {
            Intruder& in = *intruders_[i];
            if (in.life != Intruder::Life::Alive) continue;
            const float d = length(in.state.position + Vec3{0, 0.15f, 0} - b.position);
            if (d > radius && b.directHit != static_cast<int>(i)) continue;
            // Damage falls off from the full value at the centre to half at the edge of the blast radius;
            // the armoured rover survives the first hits (sparks, smoke, slower) and blows up on the last.
            const float falloff = b.directHit == static_cast<int>(i) ? 1.0f : 1.0f - 0.5f * clampf(d / radius, 0.0f, 1.0f);
            in.health -= kRoundDamage * b.power * falloff;
            ++in.hits;
            if (in.health <= 0.0f) {
                destroyIntruder(static_cast<int>(i), b.position, b.power);
            } else {
                combat_.muzzleFlash(in.state.position + Vec3{0, 0.3f, 0}, {0, 1, 0}, 0.4f);
                combat_.emitDust(in.state.position, 0.5f, 6);
                GameEvent hit;
                hit.type = GameEventType::EnemyHit;
                hit.position = in.state.position;
                hit.value = static_cast<int>(in.health);
                pushEvent(hit);
            }
        }
        for (auto& c : controllables_) {
            if (length(c->position() - b.position) < 6.0f * b.power) c->onBlast(b.position, b.power);
        }
    }
}

int Scene::spawnIntruder(const Vec3& at, float yaw, const Vec3& objective, uint32_t seed) {
    for (size_t i = 0; i < intruders_.size(); ++i) {
        Intruder& in = *intruders_[i];
        if (in.life != Intruder::Life::Waiting) continue;
        // Snap to the nearest free planner cell (the spawn ring may cross a bush or a fence post).
        int cx, cz;
        HideController::worldToCell(at, cx, cz);
        Vec3 p = at;
        if (!in.hide.cellFree(cx, cz)) {
            float best = 1e9f;
            for (int dz = -6; dz <= 6; ++dz)
                for (int dx = -6; dx <= 6; ++dx)
                    if (in.hide.cellFree(cx + dx, cz + dz) && dx * dx + dz * dz < best) {
                        best = static_cast<float>(dx * dx + dz * dz);
                        p = HideController::cellCentre(cx + dx, cz + dz);
                    }
        }
        RoverState fresh;
        fresh.position = {p.x, terrainHeight(p.x, p.z), p.z};
        fresh.yaw = yaw;
        in.state = fresh;
        in.objective = objective;
        in.health = kRoverHealth;
        in.hits = 0;
        in.age = 0.0f;
        in.switchAt = -1.0f;
        in.retreatTime = 0.0f;
        in.hide.setObjective(objective, seed);
        in.life = Intruder::Life::Alive;
        in.timer = 0.0f;
        in.debris.clear();
        setVisible(*in.rig.root, true);
        return static_cast<int>(i);
    }
    return -1;
}

int Scene::spawnIntruder(const SpawnRequest& r) {
    const int slot = spawnIntruder(r.position, r.yaw, r.objective, r.seed);
    if (slot < 0 || !r.randomRoute) return slot;  // v5 request: exactly the legacy rover
    Intruder& in = *intruders_[slot];
    in.hide.setRoute(r.via, r.objective, r.seed, r.personality);
    in.switchAt = r.switchAfter;
    in.altObjective = r.altObjective;
    return slot;
}

int Scene::withdrawIntruders() {
    int n = 0;
    for (auto& ptr : intruders_) {
        Intruder& in = *ptr;
        if (in.life != Intruder::Life::Alive || in.hide.retreating()) continue;
        // Out the way it came: the free cell on its own bearing near the fence ring.
        const float a = std::atan2(in.state.position.z, in.state.position.x);
        in.hide.setRetreat({kExitRadius * std::cos(a), 0.0f, kExitRadius * std::sin(a)});
        in.retreatTime = 0.0f;
        in.switchAt = -1.0f;
        ++n;
    }
    return n;
}

void Scene::refreshDayRules() {
    if (pinned_) {
        rules_ = pinnedRules_;
    } else {
        rules_.wavesAllowed = dayCycle_.wavesAllowed();
        rules_.beamsOn = dayCycle_.beamsOn();
        rules_.practicalScale = lights.atmo.practicalScale;
        rules_.daylight = dayCycle_.daylight();
    }
    if (dayOps_) rules_.wavesAllowed = true;   // v9 M5: daytime enemies
}

void Scene::applyDayRules() {
    // Searchlights (L2 + UAV 1's dynamic light + both visible cones) only at night; the beacon goes out by day.
    lights.atmo.beamsOn = rules_.beamsOn;
    lights.atmo.practicalScale = rules_.practicalScale;
    lights.spot.enabled = rules_.beamsOn && squad_.airborne(0);   // v9 M5: a docked UAV's searchlight is off
    lights.beacon.enabled = rules_.practicalScale >= 0.5f;
    for (int u = 0; u < kUavCount; ++u) {
        const bool beam = rules_.beamsOn && squad_.airborne(u);
        setVisible(*uavs_[u].beam, beam);
        if (uavs_[u].lens) uavs_[u].lens->material.emissive = beam ? 1.0f : 0.0f;   // v8: no lit lens by day
    }
}

void Scene::destroyIntruder(int idx, const Vec3& blastPos, float power) {
    Intruder& in = *intruders_[idx];
    if (in.life != Intruder::Life::Alive) return;
    in.life = Intruder::Life::Wreck;
    in.timer = 0.0f;
    in.wreckPosition = in.state.position;
    in.state.leftSpeed = in.state.rightSpeed = 0.0f;
    in.state.moving = false;
    ++kills_;
    // A kill while both UAVs' rounds were committed to this target counts as a combined strike.
    const StrikeState& st = squad_.strike();
    const Track* t = squad_.tracker().find(st.track);
    const bool combined = st.combined && st.launched[0] && st.launched[1] && t &&
                          length(flat(t->position() - in.state.position)) < 3.0f;
    squad_.onKill(in.state.position);
    mission_.onKill(combined, in.state.position, events_, time_);

    std::vector<SceneNode*> parts;
    collectLeaves(*in.rig.root, parts);
    in.savedMaterials.clear();
    in.debris.clear();
    uint32_t seed = static_cast<uint32_t>(idx * 7919 + kills_ * 104729);
    for (SceneNode* n : parts) {
        in.savedMaterials.push_back({n, n->material});
        Material& m = n->material;  // charred: dark, rough, hot (cools over the wreck's life)
        m.baseColor = m.baseColor * 0.10f + Vec3{0.015f, 0.013f, 0.012f};
        m.roughness = 0.95f;
        m.metalness = 0.0f;
        m.heat = 0.95f;
        m.emissive = 0.0f;
        m.lightIntensity = 0.0f;
        if (isWreckCore(*n)) continue;
        DebrisPiece p;
        p.node = n;
        p.world0 = n->world;
        p.centre0 = transformPoint(n->world, {0, 0, 0});
        p.position = p.centre0;
        Vec3 away = p.centre0 - blastPos;
        away.y = std::max(away.y, 0.0f) + 0.6f;
        const float r1 = hash01(seed++), r2 = hash01(seed++), r3 = hash01(seed++), r4 = hash01(seed++);
        p.velocity = normalize(away + Vec3{r1 - 0.5f, 0, r2 - 0.5f}) * ((2.5f + 3.5f * r3) * std::sqrt(power)) +
                     Vec3{0, 1.5f + 3.0f * r4, 0};
        const float ax = hash01(seed++) - 0.5f, ay = hash01(seed++) - 0.5f, az = hash01(seed++) - 0.5f;  // left to right, as before
        p.spinAxis = normalize(Vec3{ax, ay, az} + Vec3{0, 1e-3f, 0});
        p.spinRate = 4.0f + 10.0f * hash01(seed++);
        in.debris.push_back(p);
    }
    combat_.addScorch(in.state.position, 0.9f * power);
}

void Scene::updateDebris(Intruder& in, float dt) {
    // Sub-steps of at most 1/120 s: gravity, ground contact with restitution 0.3, sliding friction.
    const int steps = std::max(1, static_cast<int>(std::ceil(dt * 120.0f)));
    const float h = dt / static_cast<float>(steps);
    for (DebrisPiece& p : in.debris) {
        if (p.resting) continue;
        for (int k = 0; k < steps; ++k) {
            p.velocity.y -= 9.81f * h;
            p.position = p.position + p.velocity * h;
            p.rotation = axisRotation(p.spinAxis, p.spinRate * h) * p.rotation;
            const float ground = terrainHeight(p.position.x, p.position.z) + 0.03f;
            if (p.position.y < ground) {
                p.position.y = ground;
                if (p.velocity.y < 0.0f) p.velocity.y = -0.3f * p.velocity.y;
                p.velocity.x *= 0.7f;
                p.velocity.z *= 0.7f;
                p.spinRate *= 0.6f;
                if (length(p.velocity) < 0.25f) {
                    p.velocity = {0, 0, 0};
                    p.spinRate = 0.0f;
                    p.resting = true;
                    break;
                }
            }
        }
    }
}

void Scene::releaseSlot(Intruder& in) {
    for (auto& [node, mat] : in.savedMaterials) node->material = mat;
    in.savedMaterials.clear();
    in.debris.clear();
    in.hide.clearObjective();
    in.life = Intruder::Life::Waiting;
    in.timer = 0.0f;
    setVisible(*in.rig.root, false);
}

void Scene::updateIntruders(float dt) {
    std::vector<Vec3> observers;
    for (int i = 0; i < kUavCount; ++i) observers.push_back(transformPoint(cameraWorld(i), {0, 0, 0}));
    for (auto& ptr : intruders_) {
        Intruder& in = *ptr;
        switch (in.life) {
            case Intruder::Life::Waiting:
                break;
            case Intruder::Life::Alive:
                // v6: some rovers change their mind part-way (a different camp asset).
                in.age += dt;
                if (in.switchAt >= 0.0f && in.age >= in.switchAt && !in.hide.retreating()) {
                    in.objective = in.altObjective;
                    in.hide.retarget(in.altObjective);
                    in.switchAt = -1.0f;
                }
                // The rovers know where the UAVs are (they hear the rotors), not what the cameras see.
                // v7: the rover the player drives skips its planner (the squad still hunts it).
                if (playerSlot_ >= 0 && playerSlot_ < static_cast<int>(intruders_.size()) && intruders_[playerSlot_].get() == &in)
                    driveRover(in.state, playerIn_, dt, &in.hide);
                else
                    in.hide.update(dt, observers, in.state);
                if (in.health < kRoverHealth) {
                    // Damaged: limps (the planner's wheel speeds scaled back) and trails smoke.
                    const float k = 0.45f + 0.55f * in.health / kRoverHealth;
                    const Vec3 back = in.state.position - Vec3{std::sin(in.state.yaw), 0, std::cos(in.state.yaw)} * (0.6f * (1.0f - k) *
                                      0.5f * (std::fabs(in.state.leftSpeed) + std::fabs(in.state.rightSpeed)) * dt);
                    in.state.position = Vec3{back.x, terrainHeight(back.x, back.z), back.z};
                    if (in.health < 60.0f) combat_.emitBurn(in.state.position + Vec3{0, 0.3f, 0}, 0.25f * (1.0f - in.health / 60.0f), dt);
                }
                if (in.hide.retreating()) {
                    // Dawn: it left the field (no breach, no score); a stuck one is withdrawn after a while.
                    in.retreatTime += dt;
                    if (in.hide.reachedExit(in.state) || in.retreatTime > kRetreatTimeout) {
                        mission_.onWithdraw(in.state.position, events_, time_);
                        releaseSlot(in);
                    }
                    break;
                }
                if (length(flat(in.state.position - in.objective)) < layout::kBreachRadius ||
                    in.hide.reachedObjective(in.state)) {
                    // Breach: the rover reached the asset (marks it with a flare and withdraws from the game).
                    combat_.muzzleFlash(in.state.position + Vec3{0, 0.4f, 0}, {0, 1, 0}, 0.6f);
                    combat_.emitDust(in.state.position, 0.8f, 10);
                    mission_.onBreach(in.state.position, events_, time_);
                    releaseSlot(in);
                }
                break;
            case Intruder::Life::Wreck: {
                in.timer += dt;
                updateDebris(in, dt);
                const float burn = 1.0f - in.timer / kWreckBurnTime;
                if (burn > 0.0f) combat_.emitBurn(in.wreckPosition + Vec3{0, 0.2f, 0}, burn, dt);
                // The charred hull cools: apparent temperature 0.95 -> 0.3 with a 40 s time constant.
                const float heat = 0.3f + 0.65f * std::exp(-in.timer / 40.0f);
                for (auto& sm : in.savedMaterials) sm.first->material.heat = heat;
                if (in.timer >= kWreckClearTime) {
                    in.life = Intruder::Life::Gone;
                    in.timer = 0.0f;
                    setVisible(*in.rig.root, false);
                }
                break;
            }
            case Intruder::Life::Gone:
                in.timer += dt;
                if (in.timer >= kRespawnDelay) releaseSlot(in);
                break;
        }
    }
    // Base lost: every rover still on the field withdraws; the mission restarts at wave 1.
    for (const GameEvent& e : events_) {
        if (e.type != GameEventType::BaseLost) continue;
        for (auto& ptr : intruders_)
            if (ptr->life == Intruder::Life::Alive) releaseSlot(*ptr);
    }
}

void Scene::setPlayerIntruder(int slot, const ControlInput* in) {
    const bool ok = in && slot >= 0 && slot < static_cast<int>(intruders_.size()) && intruders_[slot]->life == Intruder::Life::Alive;
    playerSlot_ = ok ? slot : -1;
    playerIn_ = ok ? *in : ControlInput{};
}

void driveRover(RoverState& rover, const ControlInput& in, float dt, const HideController* grid) {
    if (dt <= 0.0f) return;
    // Wheel speed targets of a differential drive: v +- w * track / 2 (turn > 0 = right = yaw decreasing).
    const float top = in.boost ? kRoverBoostSpeed : kRoverDriveSpeed;
    const float v = clampf(in.forward, -1.0f, 1.0f) * (in.forward >= 0.0f ? top : 0.6f * kRoverDriveSpeed);
    const float w = -clampf(in.turn, -1.0f, 1.0f) * kRoverTurnRate * (std::fabs(v) > 0.05f ? 0.8f : 1.0f);
    const float targetLeft = v - w * kRoverTrackWidth * 0.5f, targetRight = v + w * kRoverTrackWidth * 0.5f;
    const float accel = 3.5f * dt;   // m/s^2 per wheel
    rover.leftSpeed += clampf(targetLeft - rover.leftSpeed, -accel, accel);
    rover.rightSpeed += clampf(targetRight - rover.rightSpeed, -accel, accel);
    const float vv = 0.5f * (rover.leftSpeed + rover.rightSpeed);
    const float ww = (rover.rightSpeed - rover.leftSpeed) / kRoverTrackWidth;
    rover.yaw += ww * dt;
    const Vec3 fwd{std::sin(rover.yaw), 0.0f, std::cos(rover.yaw)};
    Vec3 next = rover.position + fwd * (vv * dt);
    const auto blocked = [&](const Vec3& p) {
        if (std::sqrt(p.x * p.x + p.z * p.z) > kTerrainRadius - 1.0f) return true;   // the edge of the field
        return grid && !grid->freeAt(p);
    };
    if (blocked(next) && !blocked(rover.position)) {
        // Slide along the obstacle (keep the free axis), else stop against it.
        const Vec3 sx{next.x, 0.0f, rover.position.z}, sz{rover.position.x, 0.0f, next.z};
        if (!blocked(sx)) next = sx;
        else if (!blocked(sz)) next = sz;
        else {
            next = rover.position;
            rover.leftSpeed *= 0.3f;
            rover.rightSpeed *= 0.3f;
        }
    }
    rover.position = Vec3{next.x, terrainHeight(next.x, next.z), next.z};
    rover.leftWheelAngle += rover.leftSpeed / kRoverWheelRadius * dt;
    rover.rightWheelAngle += rover.rightSpeed / kRoverWheelRadius * dt;
    rover.moving = std::fabs(vv) > 0.01f || std::fabs(ww) > 0.05f;
    for (bool& s : rover.sensors) s = false;   // the line follower is off
}

void Scene::updateHeatTrail(float dt) {
    // Cooling: tau = 45 s on dry ground, 15 s when soaked (evaporation carries the heat away).
    heatTrail_.decay(dt, lerpf(45.0f, 15.0f, clampf(wetness, 0.0f, 1.0f)));
    for (const auto& ptr : intruders_) {
        const Intruder& in = *ptr;
        const RoverState& rv = in.state;
        if (in.life == Intruder::Life::Wreck) {  // the burning wreck heats the ground around it
            if (in.timer < kWreckBurnTime) heatTrail_.deposit(in.wreckPosition, 0.5f, 0.25f * dt);
            continue;
        }
        if (in.life != Intruder::Life::Alive) continue;
        const Vec3 f{std::sin(rv.yaw), 0.0f, std::cos(rv.yaw)}, l{std::cos(rv.yaw), 0.0f, -std::sin(rv.yaw)};
        const Vec3 p = rv.position;
        // Tyre friction: heat proportional to the distance each wheel rolls this step.
        const float wheelZ[3] = {-0.22f, 0.0f, 0.22f};
        for (float z : wheelZ) {
            heatTrail_.deposit(p + f * z + l * (kRoverTrackWidth * 0.5f), 0.05f, 2.2f * std::fabs(rv.leftSpeed) * dt);
            heatTrail_.deposit(p + f * z - l * (kRoverTrackWidth * 0.5f), 0.05f, 2.2f * std::fabs(rv.rightSpeed) * dt);
        }
        // A parked rover's hot motor box warms the ground under it: a warm "parking spot" stays behind.
        if (!rv.moving) heatTrail_.deposit(p - f * 0.14f, 0.22f, 0.06f * dt);
    }
    // v6: the soldiers' boots leave faint warm footprints.
    if (garrison_)
        for (const Vec3& f : garrison_->footfalls()) heatTrail_.deposit(f, 0.06f, 0.015f);
}

void Scene::applyAgentStates() {
    // UAV bodies, rotors and gimbals (hierarchy: body -> gimbal yaw -> gimbal pitch -> lamp).
    for (int u = 0; u < kUavCount; ++u) {
        const DroneState& d = squad_.drones[u];
        SceneNode& body = *uavs_[u].root;
        // Hover bob (v9 M5: none on the pad, growing over the first half metre of the climb).
        const float bob = d.manual ? 0.0f : 0.03f * (squad_.hasPads() ? clampf(squad_.heightAbovePad(u) / 0.5f, 0.0f, 1.0f) : 1.0f);
        body.local.translation = d.position + Vec3{0.0f, bob * std::sin(1.7f * time_ + 2.0f * u), 0.0f};
        body.local.rotationDeg = {d.pitchDeg, d.yawDeg, d.rollDeg};
        for (int i = 0; i < 4; ++i) {
            const float dir = (i == 0 || i == 3) ? 1.0f : -1.0f;  // diagonal pairs counter-rotate
            uavs_[u].rotors[i]->local.rotationDeg.y = std::fmod(dir * (2400.0f * time_ - rotorLag_[u]) + 37.0f * i + 11.0f * u, 360.0f);
        }
        uavs_[u].gimbalYaw->local.rotationDeg.y = d.gimbalYawDeg;
        uavs_[u].gimbalPitch->local.rotationDeg.x = d.gimbalPitchDeg;
    }

    applyRotorBlur();

    // Rovers: body follows the terrain slope under its wheels (wrecks stay where they died).
    for (auto& ptr : intruders_) {
        Intruder& in = *ptr;
        if (in.life != Intruder::Life::Alive) continue;
        const RoverState& rs = in.state;
        RoverRig& rig = in.rig;
        SceneNode& rv = *rig.root;
        const float yaw = rs.yaw;
        const Vec3 f{std::sin(yaw), 0, std::cos(yaw)}, l{std::cos(yaw), 0, -std::sin(yaw)};
        const Vec3 p = rs.position;
        const float hF = terrainHeight(p.x + f.x * 0.25f, p.z + f.z * 0.25f), hB = terrainHeight(p.x - f.x * 0.25f, p.z - f.z * 0.25f);
        const float hL = terrainHeight(p.x + l.x * 0.25f, p.z + l.z * 0.25f), hR = terrainHeight(p.x - l.x * 0.25f, p.z - l.z * 0.25f);
        rv.local.translation = p;
        rv.local.rotationDeg = {-degrees(std::atan2(hF - hB, 0.5f)), degrees(yaw), degrees(std::atan2(hL - hR, 0.5f))};
        for (int i = 0; i < 3; ++i) {
            rig.wheelsLeft[i]->local.rotationDeg.z = degrees(rs.leftWheelAngle);
            rig.wheelsRight[i]->local.rotationDeg.z = degrees(rs.rightWheelAngle);
        }
        rig.scannerPivot->local.rotationDeg.y = std::fmod(90.0f * time_, 360.0f);
        for (int i = 0; i < kIrSensorCount; ++i) rig.irSensors[i]->material.emissive = rs.sensors[i] ? 1.0f : 0.1f;
    }

    // Bunker: 1 Hz beacon pulse and spinning vent fans.
    const float pulse = 0.5f + 0.5f * std::sin(2.0f * PI * time_);
    bunker_.beaconBulb->material.emissive = 0.35f + 0.65f * pulse;
    lights.beacon.intensity = (0.6f + 1.4f * pulse) * rules_.practicalScale;
    for (SceneNode* fan : bunker_.fanCaps) fan->local.rotationDeg.z = std::fmod(300.0f * time_, 360.0f);

    // Radio masts: red obstruction lights blink in sync.
    const bool lit = std::fmod(time_, kBlinkPeriod) < kBlinkOn;
    for (SceneNode* l : aviationLights_) l->material.emissive = lit ? 1.0f : 0.08f;
    // Fire: two incommensurate sines + a faster flutter give a non-repeating flicker.
    const float flicker = 0.78f + 0.12f * std::sin(9.1f * time_) + 0.07f * std::sin(23.7f * time_ + 1.3f) +
                          0.03f * std::sin(61.0f * time_);
    for (SceneNode* p : practicals_) {
        if (p->name == "Fire embers") p->material.emissive = flicker;
        if (p->name == "Strobe" && p->object == "Autonomous UAV") {
            // UAV strobes: two 60 ms flashes per second, the second UAV half a second later.
            const float ph = std::fmod(time_ + (p->id.find("UAV2") == 0 ? 0.5f : 0.0f), 1.0f);
            p->material.emissive = (ph < 0.06f || (ph > 0.18f && ph < 0.24f)) ? 1.5f : 0.05f;
        }
    }
}

// v9 M5: spinning-propeller look. Above kRotorBlurStartRps the blade fades out and the translucent blur disc (radial
// streaks in the object shader, Renderer::drawTranslucent) fades in; at full speed only the disc is left. The pad
// lights blink while a UAV takes off from / lands on its pad, glow steadily under a docked one and dim when it is away.
void Scene::applyRotorBlur() {
    for (int u = 0; u < kUavCount; ++u) {
        const float blur = rotorBlur(kRotorFullRps * squad_.rotorSpool(u));
        for (int i = 0; i < 4; ++i) {
            if (SceneNode* disc = uavs_[u].blurDiscs[i]) {
                disc->material.opacity = kRotorDiscOpacity * blur;
                disc->visible = blur > 0.01f;
            }
            if (SceneNode* blade = uavs_[u].blades[i]) {
                blade->material.opacity = blur > 1e-3f ? 1.0f - blur : 1.0f;
                blade->visible = blur < 0.995f;
            }
        }
        if (!squad_.hasPads()) continue;
        const UavDuty duty = squad_.duty(u);
        const bool busy = duty == UavDuty::TakeOff || duty == UavDuty::Landing || duty == UavDuty::Return;
        const float glow = squad_.drones[u].manual ? 0.3f
                           : duty == UavDuty::Docked ? 1.2f
                           : busy ? (std::fmod(time_ + 0.25f * static_cast<float>(u), 0.5f) < 0.25f ? 1.6f : 0.15f) : 0.3f;
        for (SceneNode* l : padLights_[u])
            if (l) l->material.emissive = glow;
    }
}

void Scene::updateLightsAndBeam() {
    applyDayRules();
    lights.beacon.position = transformPoint(bunker_.beaconBulb->world, {0, 0, 0});
    for (int u = 0; u < kUavCount; ++u) {
        const Mat4& lamp = uavs_[u].gimbalPitch->world;
        const Vec3 pos = transformPoint(lamp, {0, 0, kLampFrontOffset});
        const Vec3 dir = normalize(transformDirection(lamp, {0, 0, 1}));
        if (u == 0) {  // UAV 0 = the shadowed searchlight L2
            lights.spot.position = pos;
            lights.spot.direction = dir;
            lights.spot.innerDeg = kSpotInnerDeg;
            lights.spot.outerDeg = kSpotOuterDeg;
        }
        // Beam length = distance from the lens to the ground along the light axis: ray march in 20 cm steps,
        // then refine the crossing by bisection (about 1 cm accuracy).
        auto below = [&](float t) {
            const Vec3 q = pos + dir * t;
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
        beamLength_[u] = len;
        const float width = 2.0f * len * std::tan(radians(kSpotOuterDeg));
        uavs_[u].beam->local.translation = {0, 0, kLampFrontOffset + 0.5f * len};
        uavs_[u].beam->local.scale = {width, width, len};
        uavs_[u].beam->world = lamp * uavs_[u].beam->local.matrix();
    }
}

}  // namespace ts
