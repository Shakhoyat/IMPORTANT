// v6 garrison (WP-B): the 8 on-duty soldiers. Each soldier is a possessable Controllable with a duty roster
// (day: guard / work / walk routes between camp sites; night: off duty in the bunker or on a guard post), routes
// planned on a 1 m site grid (A* -> string pulling -> centripetal Catmull-Rom spline) and gait-driven animation.
// Soldiers are friendly personnel: friendlyRadius() marks them for the UAVs; blasts only make them duck.
//
// Site planner: 72 x 72 cells of 1 m over the whole camp. A cell is free when its centre keeps kPlanClearance
// from every static solid that reaches into the body height band (0.15 .. 1.8 m above the ground), from the
// inner fence (posts + wire chords, open only in the gate bay), the entrance trench and the HESCO perimeter.
// A* (8-connected, octile heuristic, no corner cutting) -> greedy string pulling with sampled clearance checks
// -> centripetal Catmull-Rom spline, validated every 0.1 m (kBodyClearance); a failed sample re-inserts the
// grid path points around it. The bunker is reached by a hand-authored leg (ramp, blast doors, tunnel, rooms)
// whose heights come from the WalkSpace floors; soldiers wait in front of closed doors.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ai/Occlusion.h"
#include "math/Spline.h"
#include "objects/Soldier.h"
#include "scene/CharacterMotor.h"
#include "scene/Controllable.h"
#include "scene/Gait.h"

namespace ts {

class WalkSpace;

struct GarrisonContext {
    const std::vector<Box>* solids = nullptr;   // static solid parts (Scene::staticSolids())
    const WalkSpace* walk = nullptr;            // bunker floors / walls
    uint32_t seed = 0;                          // 0 = fixed legacy variety
    float startHour = 23.0f;
    // Every controllable of the scene (world matrices up to date): vehicles / launchers are planned around, closed
    // doors and moving vehicles are waited for. May be null (then only 'solids' count).
    const std::vector<std::unique_ptr<Controllable>>* controllables = nullptr;
    // Scene leaves (Scene::leaves(), world matrices up to date): if given, the planner uses the exact oriented plan
    // footprint of every static obstacle part and of the vehicles instead of the axis-aligned 'solids' boxes.
    const std::vector<SceneNode*>* leaves = nullptr;
};

// Day shift = sun above about +4 deg (DayBreak .. Dusk crossings of the day cycle, Khulna, early October).
constexpr float kDayShiftStart = 6.4f, kDayShiftEnd = 17.6f;
inline bool dayShift(float hour) { return hour >= kDayShiftStart && hour < kDayShiftEnd; }

class SitePlanner {
public:
    static constexpr int kCells = 72;
    static constexpr float kHalf = 36.0f, kCell = 1.0f;
    static constexpr float kPlanClearance = 0.40f;   // free cell: centre this far from every obstacle
    static constexpr float kBodyClearance = 0.28f;   // every spline sample at least this far
    static constexpr float kEdgeClearance = 0.36f;   // A* edge (centre to centre) clearance
    static constexpr float kCap = 1.5f;              // clearance values are capped here

    // Plan footprint of an obstacle: convex polygon (counter-clockwise) + its world bounds (height band, buckets).
    struct Footprint {
        std::vector<Vec3> hull;   // (x, 0, z)
        Box bounds;
    };
    static Footprint footprintOf(const Box& b);
    static Footprint footprintOf(const Mat4& world);   // unit primitive under 'world'

    void build(const std::vector<Box>& solids);
    void build(const std::vector<Footprint>& parts);
    // Distance (plan view) from (x, z) to the nearest obstacle, capped at kCap; 'avoid' adds discs (x, z, radius).
    float clearance(float x, float z) const;
    float clearance(float x, float z, const std::vector<Vec3>& avoid) const;
    bool freeCell(int i, int j) const { return i >= 0 && j >= 0 && i < kCells && j < kCells && free_[idx(i, j)]; }
    static int cellOf(float v) { return static_cast<int>(std::floor((v + kHalf) / kCell)); }
    static float centreOf(int i) { return -kHalf + (static_cast<float>(i) + 0.5f) * kCell; }
    int freeCount() const;

    // A* over the free cells from 'a' to 'b' (snapped to the nearest free cells). Raw cell path in 'cells'
    // (world points, starting with a and ending with b). False if unreachable.
    bool gridPath(const Vec3& a, const Vec3& b, const std::vector<Vec3>& avoid, std::vector<Vec3>& cells) const;
    // Greedy string pulling of a point chain: indices of the points kept (first and last always).
    std::vector<int> stringPull(const std::vector<Vec3>& chain, const std::vector<Vec3>& avoid) const;
    // Spline through chain[keep]; where a sample loses clearance the chain points around it are put back
    // (ultimately the spline runs through the whole chain, whose grid edges are clear by construction).
    CatmullRomPath smooth(const std::vector<Vec3>& chain, std::vector<int> keep, bool closed,
                          const std::vector<Vec3>& avoid) const;
    // Smallest clearance along a path (samples every 0.1 m, surface only: not on the ramp / underground).
    float minClearance(const CatmullRomPath& p, const std::vector<Vec3>& avoid = {}) const;
    int lastExpanded() const { return expanded_; }
    const std::vector<Footprint>& parts() const { return parts_; }      // blocking footprints (+ the trench)
    static float distanceTo(const Footprint& f, float x, float z);     // plan distance, 0 inside

private:
    static int idx(int i, int j) { return j * kCells + i; }
    std::vector<Footprint> parts_;            // blocking footprints (body band)
    std::vector<std::vector<int>> buckets_;   // per cell: boxes within kCap of it
    std::vector<unsigned char> free_;
    std::vector<unsigned char> edges_;        // per cell: bit d set = the move to neighbour d is clear
    mutable int expanded_ = 0;
};

// v8: Resident = an off-duty soldier living in the barracks hall (unarmed; sleeps, trains, sits at the mess table).
enum class Duty { Sentry, Watch, Gunner, Mechanic, Logistics, Technician, Patrol, Runner, Resident };
enum class Activity { Idle, Guard, Work, Sit, Walk, Run, Wait, Duck, Crouch, Wave,
                      Sleep, PushUps, PullUps, SitUps, Talk };   // v8 barracks life
const char* dutyName(Duty d);
const char* activityName(Activity a);

struct GarrisonCore;   // shared state of the 8 soldiers (Garrison.cpp)

// One soldier: duty program state machine, route following, gait / pose animation, possession.
class Soldier : public Controllable {
public:
    Soldier(int index, Duty duty, const SoldierLook& look, GarrisonCore* g);
    ~Soldier() override;

    const char* name() const override { return name_.c_str(); }
    const char* controls() const override {
        return "WASD move  Shift sprint  Space jump  Alt crouch  mouse look  LMB fire  RMB aim  V 1st/3rd person";
    }
    SceneNode* root() override { return rig_.root; }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return {x_, feetY_, z_}; }
    float friendlyRadius() const override { return 1.2f; }
    bool onBlast(const Vec3& point, float power) override;

    // --- garrison / test access ---
    int index() const { return index_; }
    Duty duty() const { return duty_; }
    Activity activity() const { return activity_; }
    const SoldierRig& rig() const { return rig_; }
    float yawDeg() const { return yaw_; }
    float speed() const { return speed_; }
    bool travelling() const;
    bool underground() const { return feetY_ < -1.0f; }
    bool planted(int leg) const { return planted_[leg]; }
    Vec3 plantPoint(int leg) const { return lock_[leg]; }
    float gaitPhase() const { return phase_; }
    const Pose& pose() const { return pose_; }
    bool carrying() const { return carrying_; }
    bool unarmed() const { return duty_ == Duty::Resident; }   // v8: residents keep their rifles in the rack
    bool onDayShift() const { return day_; }
    int stance() const { return stance_; }          // possessed: 0 stand, 1 crouch, 2 wave
    // v7.1 WP-T: open-world movement of the possessed soldier (jump arc, strafe)
    bool airborne() const { return airborne_; }
    float verticalSpeed() const { return vy_; }
    float lastLandSpeed() const { return landSpeed_; }   // m/s at the latest touch-down
    int landings() const { return landings_; }           // touch-downs so far (the App plays the landing)
    // v9 M3: the possessed soldier's character motor (velocity springs, stance blends, coyote / buffer, step smoothing)
    const CharacterMotor& motor() const { return motor_; }
    float landDip() const { return landDip_; }           // 0..1 landing dip still showing
    float drawnFeetY() const;                            // feet height as drawn (smoothed over steps)
    float routeLeft() const;                         // m to the end of the current route (0 when holding)
    float routeCurvature() const;                    // 1/m at the current route position (0 when holding)
    // Teleport onto the surface (tests); feetY: start from this level instead (rooms below / above the terrain).
    void place(float x, float z, float yawDeg, float feetY = -1e9f);
    std::string debugState() const;                  // phase, route position, wait flags (tests / tools)

    struct State;
private:
    friend class Garrison;
    friend struct GarrisonCore;
    void animate(float dt, float time);
    void stepRoute(const WorldContext& w, float dt);
    void stepPossessed(const WorldContext& w, const ControlInput& in, float dt);
    void startShift(bool day, float time);
    void nextStop(float time);
    void beginHold(float time);
    void resume(float time);
    float floorAt(float x, float z) const;

    int index_;
    Duty duty_;
    std::string name_;
    SoldierRig rig_;
    GarrisonCore* g_;
    std::unique_ptr<State> st_;
    Activity activity_ = Activity::Idle;
    float x_ = 0.0f, z_ = 0.0f, feetY_ = 0.0f, yaw_ = 0.0f, viewPitch_ = 0.0f;
    float speed_ = 0.0f, phase_ = 0.0f;
    bool planted_[2] = {false, false};
    Vec3 lock_[2];
    Pose pose_;
    bool carrying_ = false, day_ = false, possessed_ = false;
    int stance_ = 0;
    // v7.1 WP-T (possessed only): jump state, landing dip; v9 M3: the motor drives them (cmdSpeed_ / strafe_ went
    // into its velocity spring)
    float vy_ = 0.0f, airT_ = 0.0f, landDip_ = 0.0f, landSpeed_ = 0.0f;
    CharacterMotor motor_;
    bool airborne_ = false;
    int landings_ = 0;
};

class Garrison {
public:
    static constexpr int kSoldiers = 8;   // the duty roster (indices 0..7); v8 residents follow from index 8
    static constexpr int kMaxResidents = 6;
    Garrison();
    ~Garrison();

    // The soldiers as controllables (Scene moves them into controllables_). Call once. v8: 'residents' (0..6)
    // off-duty soldiers of the barracks hall are added after the eight on duty.
    std::vector<std::unique_ptr<Controllable>> build(uint32_t seed, int residents = 0);
    // After the scene knows its static solids: plan the routes and put every soldier on its post for startHour.
    void init(const GarrisonContext& ctx);
    // Convenience for tests / tools that do not own a Scene: updates every soldier (none possessed).
    void update(const WorldContext& w);

    int count() const;
    const Controllable* soldier(int i) const;
    Soldier* unit(int i);
    const Soldier* unit(int i) const;
    std::string roster(int i, bool day) const;   // duty text for export / report
    std::vector<float> routeLengths() const;     // m, one per planned route
    std::vector<Vec3> footfalls() const;         // feet that touched the ground this frame (heat footprints)
    int takeShiftChanges();                      // soldiers that left for a new post since the last call (ShiftChange)
    const SitePlanner& planner() const;
    const std::vector<CatmullRomPath>& routes() const;   // every planned duty route (export / figures)
    bool dayActive() const;                      // roster currently on the day shift

    using Impl = GarrisonCore;
private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace ts
