// Rover "hide from the drone" behaviour.
//
// Planning (MicroMouse style): the 20 m disc is divided into square cells. Every cell whose line of sight
// to the UAV is blocked by an occluder is a goal. A breadth-first flood fill spreads distance values out
// from the goal cells; the robot then walks "downhill" (to the neighbour with the smaller value) exactly as a
// MicroMouse walks down its flood-fill maze towards the centre.
//
// Driving (Line Follower Robot): the planned cell path is smoothed into a virtual guide line. Five IR
// reflectance sensors in front of the robot report whether they are over the line; the weighted sensor error
// feeds a PD controller that sets the left/right wheel speeds of a differential drive.
#pragma once

#include <vector>

#include "ai/Occlusion.h"

namespace ts {

constexpr float kCellSize = 0.5f;
constexpr int kGridCells = 80;          // 80 x 80 cells over [-20, 20] m
constexpr int kIrSensorCount = 5;

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
    std::vector<Vec3> descend(int startX, int startZ) const;  // walk to decreasing flood values

private:
    static int index(int cx, int cz) { return cz * kGridCells + cx; }
    void replan(const Vec3& observer, const RoverState& rover);
    void nearestFreeCell(int& cx, int& cz) const;
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
};

}  // namespace ts
