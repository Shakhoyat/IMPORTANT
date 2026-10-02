// Walkable space for the first-person walk camera (and later anything that moves on foot): floor regions
// that override the terrain height (bunker rooms, tunnel, entrance ramp), wall boxes to collide with, and
// "indoor" volumes (no sky light, no rain, little fog) that the renderer reads.
#pragma once

#include <vector>

#include "ai/Occlusion.h"

namespace ts {

struct WalkFloor {
    float x0, z0, x1, z1;      // footprint (x0 < x1, z0 < z1)
    float y0, y1;              // floor height: y0 at the low edge, y1 at the high edge of the slope axis
    int slopeAxis = 0;         // 0 flat (y0), 1 linear along x (x0 -> x1), 2 linear along z (z0 -> z1)
    float ceiling = 1e9f;      // y of the ceiling above this floor (limits the eye height)
};

class WalkSpace {
public:
    void addFloor(const WalkFloor& f) { floors_.push_back(f); }
    void addWall(const Box& b) { walls_.push_back(b); }
    void addIndoor(const Box& b) { indoor_.push_back(b); }

    // Height of the floor under (x, z) that a walker currently at height 'feetY' stands on: the highest floor
    // region at or below feetY + 0.45 (a step), otherwise the terrain height.
    float floorHeight(float x, float z, float feetY) const;
    // Ceiling above (x, z) for a walker at feetY (1e9 outside).
    float ceilingHeight(float x, float z, float feetY) const;
    // Pushes a vertical cylinder (radius r, feet to head) out of every wall box and every extra box
    // (dynamic obstacles such as closed doors). Returns true if it touched something.
    bool collide(float& x, float& z, float feetY, float headY, float radius, const std::vector<Box>& extra) const;
    bool indoor(const Vec3& p) const;

    const std::vector<WalkFloor>& floors() const { return floors_; }
    const std::vector<Box>& walls() const { return walls_; }
    const std::vector<Box>& indoorVolumes() const { return indoor_; }  // the renderer uses the first 8

private:
    std::vector<WalkFloor> floors_;
    std::vector<Box> walls_;
    std::vector<Box> indoor_;
};

}  // namespace ts
