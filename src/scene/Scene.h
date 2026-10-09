// The ThermalSense world: scene graph, animation, the agents, the controllable objects and the game:
// a two-UAV squad (vision -> shared multi-target tracking -> coordinated time-on-target strikes) defends the
// camp against endless waves of intruder rovers (MicroMouse + line-follower infiltration on random routes).
#pragma once

#include <memory>
#include <vector>

#include "ai/DroneController.h"
#include "ai/HideController.h"
#include "ai/Squad.h"
#include "objects/Bunker.h"
#include "objects/Rover.h"
#include "objects/Uav.h"
#include "rendering/Lighting.h"
#include "scene/Combat.h"
#include "scene/Controllable.h"
#include "scene/DayCycle.h"
#include "scene/Door.h"
#include "scene/GameEvents.h"
#include "scene/Garrison.h"
#include "scene/HeatTrail.h"
#include "scene/Mission.h"
#include "scene/SceneNode.h"
#include "scene/WalkSpace.h"
#include "vision/CvPipeline.h"
#include "vision/ThermalTracker.h"

namespace ts {

// One piece of a destroyed rover: an independent rigid body (position, orientation, velocities).
struct DebrisPiece {
    SceneNode* node = nullptr;
    Mat4 world0;          // world matrix at the moment of the blast
    Vec3 centre0;         // its origin then
    Vec3 position, velocity, spinAxis;
    float spinRate = 0.0f;   // rad/s
    Mat4 rotation;           // accumulated rotation about the piece's origin
    bool resting = false;
};

// An intruder rover slot: driving state, node tree, its own planner and its life cycle.
//   Waiting (slot free, hidden) -> Alive (infiltrating) -> Wreck (destroyed, burning) -> Gone -> Waiting
//   Alive -> Waiting directly after a breach (the rover reached its objective)
struct Intruder {
    enum class Life { Waiting, Alive, Wreck, Gone };
    RoverState state;
    RoverRig rig;
    HideController hide;
    Life life = Life::Waiting;
    float timer = 0.0f;      // Wreck / Gone: seconds since that phase began
    int spawnIndex = 0;
    Vec3 objective;          // camp asset it is heading for
    float health = 100.0f;   // armoured rover: about 4-5 hits from the UAVs' rounds
    int hits = 0;
    std::vector<DebrisPiece> debris;
    std::vector<std::pair<SceneNode*, Material>> savedMaterials;  // restored on respawn
    Vec3 wreckPosition;
    // v6
    float age = 0.0f;          // s since spawning
    float switchAt = -1.0f;    // objective switch time (age), < 0 none
    Vec3 altObjective;
    float retreatTime = 0.0f;  // s since the dawn retreat began
};

// What the time of day allows (from the DayCycle each frame, or pinned by tests / captures).
struct DayRules {
    bool wavesAllowed = true;     // rover waves (night)
    bool beamsOn = true;          // UAV searchlights
    float practicalScale = 1.0f;  // lamps; the beacon is off below 0.5
    float daylight = 0.0f;
};

constexpr float kWreckBurnTime = 15.0f;     // s of fire and smoke from the wreck
constexpr float kWreckClearTime = 25.0f;    // s until the wreck is removed
constexpr float kRespawnDelay = 3.0f;       // s between removal and the slot being free again
constexpr float kUavReloadTime = 6.0f;      // s until a launched rail round is replaced
constexpr float kRoverHealth = 100.0f;      // armoured intruder
constexpr float kRoundDamage = 30.0f;       // damage of a power-1 blast at the centre, half at the edge of its radius
constexpr int kDroneOwner = 0;              // Combat owner id of UAV 0 (UAV 1 = owner 10)
constexpr int kUavCount = Squad::kCount;
constexpr float kExitRadius = 17.5f;        // v6: a withdrawing rover leaves through a free cell at this radius
constexpr float kRetreatTimeout = 90.0f;    // s: a rover that cannot reach its exit is withdrawn anyway

class DroneControl;

// v6 options. The defaults reproduce the v5 night scene exactly (fixed seeds, night, legacy rover routes), so the
// unit tests stay deterministic; the application switches the v6 behaviour on.
struct SceneOptions {
    uint32_t seed = 0;            // 0 = legacy fixed seeds; otherwise every random stream derives from it
    float startHour = 23.0f;      // time of day at start (0..24)
    bool autoCycle = false;       // let the day/night cycle run
    bool garrison = true;         // the 8 soldiers + sky clock (v6 controllables)
    bool randomRoutes = false;    // v6 rover via-point chains + personalities
    int residents = 0;            // v8: off-duty soldiers living in the barracks hall (sleep, PT, mess table); the app uses 6
    static SceneOptions v6(uint32_t seed, float startHour, bool autoCycle) {
        SceneOptions o;
        o.seed = seed;
        o.startHour = startHour;
        o.autoCycle = autoCycle;
        o.randomRoutes = true;
        return o;
    }
};
// Independent random stream 'stream' of a run seed (splitmix32); base 0 returns 0 (= legacy constants).
uint32_t deriveSeed(uint32_t base, uint32_t stream);

class Scene {
public:
    explicit Scene(const SceneOptions& options = {});
    const SceneOptions& options() const { return options_; }
    ~Scene();

    // Advances the simulation by dt seconds. 'det' is the v4 single-target tracker output of UAV 0, used only
    // when no vision frame was submitted (unit tests). 'input' drives controllables()[possessed].
    void update(float dt, const Detection& det, const ControlInput* input, int possessed);
    void update(float dt, const Detection& det) { update(dt, det, nullptr, -1); }
    // v5 vision: blobs from UAV 'uav''s thermal image (vision/CvPipeline) and the camera they were seen with.
    void submitVision(int uav, const std::vector<Measurement2D>& blobs, const Mat4& camera);
    // Recomputes all world matrices (explode > 0 pushes composite parts apart for the diagnostic view).
    void updateWorld();

    float time() const { return time_; }
    float explode = 0.0f;

    const std::vector<SceneNode*>& leaves() const { return leaves_; }
    std::vector<SceneNode*> roots() const;
    // v9 M1: render interpolation between the last two fixed simulation ticks (scene/TickInterp.h). captureTick after
    // every tick; interpolate(alpha) just before drawing (leaf worlds blended); restoreTick right after drawing, so the
    // simulation, the UAV cameras and every query see the tick state again. Returns the number of parts blended.
    void captureTick();
    int interpolate(float alpha);
    void restoreTick();

private:
    Squad squad_;                                       // declared first: 'drone' below refers into it
    std::vector<std::unique_ptr<Intruder>> intruders_;  // and 'rover' to the first slot

public:
    LightRig lights;
    DroneState& drone;  // UAV 0 (manual flight, single-UAV HUD / tests)
    RoverState& rover;  // intruder slot 0
    Detection lastDetection;

    const UavRig& uav() const { return uavs_[0]; }
    const UavRig& uav(int i) const { return uavs_[i]; }
    const DroneState& droneState(int i) const { return squad_.drones[i]; }
    const std::vector<SceneNode*>& practicalLights() const { return practicals_; }
    const HeatTrail& heatTrail() const { return heatTrail_; }
    float wetness = 0.0f;  // 0 dry .. 1 soaked (rain); wet ground cools the heat trail faster
    const RoverRig& roverRig() const { return intruders_[0]->rig; }
    const HideController& hideController() const { return intruders_[0]->hide; }
    Mat4 cameraWorld(int i = 0) const { return uavs_[i].gimbalPitch->world; }  // thermal camera = searchlight axis
    float beamLength(int i = 0) const { return beamLength_[i]; }

    // --- intruders, controllable objects, combat, game ---
    const std::vector<std::unique_ptr<Intruder>>& intruders() const { return intruders_; }
    std::vector<RoverState> rovers() const;  // state of every intruder slot
    bool roverAlive(int i) const { return intruders_[i]->life == Intruder::Life::Alive; }
    int aliveIntruders() const;
    const std::vector<std::unique_ptr<Controllable>>& controllables() const { return controllables_; }
    const Combat& combat() const { return combat_; }
    Combat& combat() { return combat_; }
    const WalkSpace& walkSpace() const { return bunker_.walk; }
    const Squad& squad() const { return squad_; }
    const Mission& mission() const { return mission_; }
    int kills() const { return kills_; }
    int roundsLaunched() const { return squad_.drones[0].launched + squad_.drones[1].launched; }
    int roundsReady(int uav = 0) const;
    // Releases one of UAV 0's rail rounds towards the point its camera looks at (manual fire). False if none.
    bool launchRound();
    // Releases one of UAV 'uav''s rounds homing on laser spot 'designator', initially aimed at 'aim'.
    bool launchRound(int uav, const Vec3& aim, int designator);
    // Destroys intruder i as if a blast of 'power' went off at 'blastPos' (also used by captures / tests).
    void destroyIntruder(int i, const Vec3& blastPos, float power);
    // Activates a free intruder slot at 'at' heading for 'objective' (mission spawns, tests). Returns slot or -1.
    int spawnIntruder(const Vec3& at, float yaw, const Vec3& objective, uint32_t seed);
    // v6: a mission spawn (random route, personality, objective switch when the request has them).
    int spawnIntruder(const SpawnRequest& r);
    // Ground point at the centre of UAV i's camera image.
    bool designatorPoint(Vec3& p, int uav = 0) const;
    const std::vector<Box>& staticSolids() const { return solids_; }
    // Game events of the latest update() (HUD banners, sounds). Cleared at the start of every update().
    const std::vector<GameEvent>& events() const { return events_; }
    void pushEvent(GameEvent e) { e.time = time_; events_.push_back(e); }
    // Mission waves on/off (tests switch them off to place intruders by hand).
    void setMissionEnabled(bool on) { missionOn_ = on; }

    // --- v6 time of day, garrison ---
    DayCycle& dayCycle() { return dayCycle_; }
    const DayCycle& dayCycle() const { return dayCycle_; }
    const Garrison* garrison() const { return garrison_.get(); }   // nullptr when options().garrison == false
    Garrison* garrison() { return garrison_.get(); }
    // v9 M4: the hinged doors (barracks hall entrance, bunker generator room); null when the scene has none.
    DoorPortal* hallDoor() const { return hallDoor_; }
    DoorPortal* generatorDoor() const { return generatorDoor_; }
    // The rules of the current frame (waves, searchlights, lamps). pinDayRules() overrides the clock's rules
    // (tests, captures) until unpinDayRules().
    const DayRules& dayRules() const { return rules_; }
    void pinDayRules(const DayRules& r) { pinned_ = true; pinnedRules_ = r; refreshDayRules(); }
    void unpinDayRules() { pinned_ = false; refreshDayRules(); }
    // Sends every alive rover home (dawn): exit cell on its bearing at kExitRadius. Returns how many.
    int withdrawIntruders();

    // --- v7 (WP-G): the player drives an intruder rover ---
    // Slot 'slot' takes 'in' (copied) for the next update() instead of its planner: the HideController pauses, the
    // rover drives from the input (differential drive, kRoverDriveSpeed), the UAVs keep hunting it, a breach still
    // counts. slot -1 (or a slot that is not alive) = every rover on its own planner again.
    void setPlayerIntruder(int slot, const ControlInput* in);
    int playerIntruder() const { return playerSlot_; }

    // --- v7.1 (WP-D): UAV day/night duty (docked on the pads by day, patrol by night) ---
    // uavAirborne(i): UAV i is off its pad (take-off, patrol, return, landing, or flown by the player).
    // uavDutyState(i): "DOCKED", "TAKE-OFF", "PATROL", "RETURN", "LANDING" or "MANUAL" (static strings).
    bool uavAirborne(int i) const;
    const char* uavDutyState(int i) const;

private:
    friend class DroneControl;
    void applyAgentStates();
    void updateLightsAndBeam();
    void updateIntruders(float dt);
    void updateDebris(Intruder& in, float dt);
    void releaseSlot(Intruder& in);
    void applyBlasts();
    void collectSceneLists();
    void refreshDayRules();
    void applyDayRules();

    std::unique_ptr<SceneNode> terrain_, environment_, backdrop_, base_, hall_;   // hall_: v8 barracks hall
    DoorPortal* hallDoor_ = nullptr;        // v9 M4: owned by controllables_ (after the garrison: v5-v8 indices stay)
    DoorPortal* generatorDoor_ = nullptr;
    std::vector<SceneNode*> aviationLights_;  // blinking red obstruction lights (radio masts)
    BunkerRig bunker_;
    UavRig uavs_[kUavCount];
    std::vector<SceneNode*> leaves_;
    uint32_t tick_ = 0;                      // v9 M1: ticks captured
    std::vector<SceneNode*> blended_;        // leaves whose world is an interpolated one right now
    std::vector<SceneNode*> practicals_;  // emissive parts that are also light sources
    HeatTrail heatTrail_;
    void updateHeatTrail(float dt);

    DroneController manualCtl_;          // gimbal assist for manual flight
    SceneOptions options_;
    DayCycle dayCycle_;
    std::unique_ptr<Garrison> garrison_;
    DayRules rules_, pinnedRules_;
    bool pinned_ = false;
    bool nightRules_ = true;             // waves were allowed last frame (dawn = this turning false)
    Mission mission_;
    bool missionOn_ = true;
    SquadFrame vision_;                  // frames submitted since the last update
    std::vector<std::unique_ptr<Controllable>> controllables_;
    DroneControl* droneControl_ = nullptr;
    Combat combat_;
    std::vector<Box> solids_;            // static solid parts (vehicle collision, projectile impacts, UAV avoidance)
    float railReload_[kUavCount][kUavMissiles] = {};
    int kills_ = 0;
    std::vector<GameEvent> events_;
    float time_ = 0.0f;
    float beamLength_[kUavCount] = {5.0f, 5.0f};
    int playerSlot_ = -1;                // v7: intruder driven by the player (-1 none)
    ControlInput playerIn_;
};

// v7 (WP-G): one step of a player-driven rover. W/S throttle, A/D steer (differential drive with the planner's
// track width and wheel kinematics), Shift = boost; 'freeAt' (may be null) blocks cells the rover may not enter
// (unless it is already stuck in one), the ground height follows the terrain.
constexpr float kRoverDriveSpeed = 1.1f, kRoverBoostSpeed = 1.8f, kRoverTurnRate = 2.2f;   // m/s, m/s, rad/s
void driveRover(RoverState& rover, const ControlInput& in, float dt, const HideController* freeAt);

}  // namespace ts
