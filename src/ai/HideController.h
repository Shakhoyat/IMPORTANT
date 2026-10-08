// Rover "hide from the drone" behaviour.
//
// Planning (MicroMouse style): the 40 m x 40 m square around the base is divided into 80 x 80 cells of 0.5 m. Every cell whose line of sight
// to the UAV is blocked by an occluder is a goal. A breadth-first flood fill spreads distance values out
// from the goal cells; the robot then walks "downhill" (to the neighbour with the smaller value) exactly as a
// MicroMouse walks down its flood-fill maze towards the centre.
//
// Driving (Line Follower Robot): the planned cell path is smoothed into a virtual guide line. Five IR
// reflectance sensors in front of the robot report whether they are over the line; the weighted sensor error
// feeds a PD controller that sets the left/right wheel speeds of a differential drive.
#pragma once

#include <cstdint>

#include <vector>

#include "ai/Occlusion.h"

namespace ts {

constexpr float kCellSize = 0.5f;
constexpr int kGridCells = 80;          // 80 x 80 cells over [-20, 20] m
constexpr int kIrSensorCount = 5;
constexpr float kEvadeRadius = 5.0f;     // a UAV this close (horizontal) makes an infiltrating rover hide
constexpr float kEvadeTime = 4.0f;       // s of hiding before infiltrating again

// v6 per-rover character (random routes). The defaults are the v5 rover exactly.
struct RoverPersonality {
    float noiseWeight = 24.0f;     // route cost of the random noise field (x16 fixed point per cell)
    float exposureWeight = 24.0f;  // route cost of a cell a UAV can see
    float speedScale = 1.0f;       // top speed factor (0.8 .. 1.25)
    float pauseInCover = 0.0f;     // probability of stopping for 1.5-4 s on entering cover
    int noiseCells = 4;            // noise lattice spacing in cells (3 .. 7)
    // v8 natural motion (all 0 = the v6 rover): bounded randomness, so no two approaches look alike and a tracker
    // cannot extrapolate one straight, constant-speed line for long.
    float speedWander = 0.0f;      // slow random speed modulation: x (1 +- speedWander), time constant ~3 s
    float weave = 0.0f;            // m: lateral weave of the guide line (kept clear of obstacles)
    float weaveWavelength = 5.0f;  // m along the route
    float hesitate = 0.0f;         // 1/s: chance of a short stop (0.4-1.6 s) in the open
    float detour = 0.0f;           // per 10 s: chance of a fresh side via point (a change of direction), at most 2
};

struct RoverState {
    Vec3 position{6.0f, 0.0f, 7.5f};  // open ground east of the track
    float yaw = 0.0f;               // radians, 0 = facing +Z
    float leftSpeed = 0.0f, rightSpeed = 0.0f;   // m/s
    float leftWheelAngle = 0.0f, rightWheelAngle = 0.0f;
    bool sensors[kIrSensorCount] = {false, false, false, false, false};
    bool hidden = false;            // current cell is out of the UAV's sight
    bool moving = false;
};

class HideController {
public:
    void setStaticOccluders(const std::vector<Occluder>& occluders);  // builds the free-cell grid

    // 'observer' = where the rover believes the UAV camera is (it can hear the rotors).
    void update(float dt, const Vec3& observer, RoverState& rover);
    // v5: several observers (both UAVs). With an objective set the rover INFILTRATES: weighted MicroMouse flood
    // fill from the objective (cost = 1 per cell + exposure to any UAV + a per-rover random noise field, so every
    // rover takes its own route); when a UAV comes within kEvadeRadius it switches to HIDING for a while.
    void update(float dt, const std::vector<Vec3>& observers, RoverState& rover);
    void setObjective(const Vec3& objective, uint32_t seed);
    // v6 random route: drive through the via points in order (each one the goal of the flood fill until the
    // rover is on it), then infiltrate to the objective, with the rover's own personality.
    void setRoute(const std::vector<Vec3>& via, const Vec3& objective, uint32_t seed, const RoverPersonality& p);
    // New objective (the mission switched it): the remaining via points are dropped.
    void retarget(const Vec3& objective);
    // Dawn: leave the field through 'exit' (snapped to a free cell); no hiding, no pauses, never a breach.
    void setRetreat(const Vec3& exit);
    bool retreating() const { return retreat_; }
    // Retreating and at the exit cell.
    bool reachedExit(const RoverState& rover) const;
    void clearObjective() { infiltrate_ = false; retreat_ = false; }
    bool evading() const { return evadeTime_ > 0.0f; }
    bool pausing() const { return pauseTime_ > 0.0f; }
    bool hesitating() const { return hesitateTime_ > 0.0f; }   // v8: a short stop in the open
    float speedFactor() const { return speedFactor_; }          // v8: current speed modulation
    int detours() const { return detours_; }                    // v8: side via points inserted so far
    const Vec3& objective() const { return objective_; }
    const Vec3& currentGoal() const;              // the next via point, the objective or the exit
    const std::vector<Vec3>& via() const { return via_; }
    int viaReached() const { return viaIndex_; }  // via points passed so far
    const RoverPersonality& personality() const { return personality_; }
    // Infiltrating and at the end of the route (the free cell nearest the objective): the rover has arrived.
    bool reachedObjective(const RoverState& rover) const;

    // --- read-only access for tests, diagnostics and the report ---
    bool cellFree(int cx, int cz) const;
    bool cellHidden(int cx, int cz) const { return hidden_[index(cx, cz)] != 0; }
    int floodValue(int cx, int cz) const { return flood_[index(cx, cz)]; }
    const std::vector<Vec3>& guideLine() const { return line_; }
    static void worldToCell(const Vec3& p, int& cx, int& cz);
    static Vec3 cellCentre(int cx, int cz);

    // Individual steps, public so they can be unit-tested in isolation.
    void computeVisibility(const Vec3& observer);
    void floodFill();                                         // BFS distance from hidden cells
    void floodFillToObjective(const std::vector<Vec3>& observers);  // weighted (Dijkstra) field from the objective
    float noiseAt(int cx, int cz) const;                      // per-rover random cost field, 0..1
    bool freeAt(const Vec3& p) const;                         // the cell under p is free
    std::vector<Vec3> descend(int startX, int startZ) const;  // walk to decreasing flood values

private:
    static int index(int cx, int cz) { return cz * kGridCells + cx; }
    void replan(const Vec3& observer, const RoverState& rover);
    void nearestFreeCell(int& cx, int& cz) const;
    void setGoalCell();
    void followLine(float dt, RoverState& rover);
    float distanceToLine(const Vec3& p) const;
    float sideOfLine(const Vec3& p, const Vec3& left) const;
    bool collides(const Vec3& p) const;
    bool allBlocked(const Vec3* points, int count, const Vec3& observer) const;

    std::vector<Occluder> occluders_;
    OccluderGrid grid_;  // accelerated line-of-sight queries (same answers as the brute-force test)
    std::vector<unsigned char> free_ = std::vector<unsigned char>(kGridCells * kGridCells, 0);
    std::vector<unsigned char> hidden_ = std::vector<unsigned char>(kGridCells * kGridCells, 0);
    std::vector<int> flood_ = std::vector<int>(kGridCells * kGridCells, -1);
    std::vector<Vec3> line_;
    float replanTimer_ = 0.0f;
    float lastError_ = 0.0f;
    float lostTime_ = 0.0f;
    float blockedTime_ = 0.0f;  // seconds the way ahead has been blocked
    bool infiltrate_ = false;
    Vec3 objective_;
    uint32_t seed_ = 1;
    float evadeTime_ = 0.0f;    // > 0: hiding from a close UAV
    std::vector<Vec3> observers_;
    Vec3 goalCell_;
    // v6 random routes / dawn retreat
    std::vector<Vec3> via_;
    int viaIndex_ = 0;
    RoverPersonality personality_;
    bool retreat_ = false;
    Vec3 exit_;
    float pauseTime_ = 0.0f;    // > 0: stopped in cover
    bool wasHidden_ = false;
    uint32_t pauseRng_ = 1;
    // v8 natural motion
    void naturalMotion(float dt, const RoverState& rover);
    void weaveLine();
    float motionRnd();
    uint32_t motionRng_ = 1;
    float wander_ = 0.0f, speedFactor_ = 1.0f;
    float hesitateTime_ = 0.0f, detourTimer_ = 0.0f;
    float odometer_ = 0.0f, lineOdo_ = 0.0f;   // distance driven; odometer at the start of the current guide line
    int detours_ = 0;
    float weaveOff_ = 0.0f;              // s left with the weave switched off (after the rover got stuck)
    float stuckT_ = 0.0f;
    Vec3 stuckRef_;
};

}  // namespace ts
