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
#include "scene/GameEvents.h"
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
    std::vector<DebrisPiece> debris;
    std::vector<std::pair<SceneNode*, Material>> savedMaterials;  // restored on respawn
    Vec3 wreckPosition;
};

constexpr float kWreckBurnTime = 15.0f;     // s of fire and smoke from the wreck
constexpr float kWreckClearTime = 25.0f;    // s until the wreck is removed
constexpr float kRespawnDelay = 3.0f;       // s between removal and the slot being free again
constexpr float kUavReloadTime = 10.0f;     // s until a launched rail round is replaced
constexpr int kDroneOwner = 0;              // Combat owner id of UAV 0 (UAV 1 = owner 10)
constexpr int kUavCount = Squad::kCount;

class DroneControl;

class Scene {
public:
    Scene();
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
    // Ground point at the centre of UAV i's camera image.
    bool designatorPoint(Vec3& p, int uav = 0) const;
    const std::vector<Box>& staticSolids() const { return solids_; }
    // Game events of the latest update() (HUD banners, sounds). Cleared at the start of every update().
    const std::vector<GameEvent>& events() const { return events_; }
    void pushEvent(GameEvent e) { e.time = time_; events_.push_back(e); }
    // Mission waves on/off (tests switch them off to place intruders by hand).
    void setMissionEnabled(bool on) { missionOn_ = on; }

private:
    friend class DroneControl;
    void applyAgentStates();
    void updateLightsAndBeam();
    void updateIntruders(float dt);
    void updateDebris(Intruder& in, float dt);
    void releaseSlot(Intruder& in);
    void applyBlasts();
    void collectSceneLists();

    std::unique_ptr<SceneNode> terrain_, environment_, backdrop_, base_;
    std::vector<SceneNode*> aviationLights_;  // blinking red obstruction lights (radio masts)
    BunkerRig bunker_;
    UavRig uavs_[kUavCount];
    std::vector<SceneNode*> leaves_;
    std::vector<SceneNode*> practicals_;  // emissive parts that are also light sources
    HeatTrail heatTrail_;
    void updateHeatTrail(float dt);

    DroneController manualCtl_;          // gimbal assist for manual flight
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
};

}  // namespace ts
