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

// v8: how much outdoor light an indoor volume lets in. The buried bunker: almost no sky light and no sun / moon
// (the defaults); a building with windows: part of the sky light, and the key light, whose shadow map then decides
// what the openings let through.
struct IndoorLight {
    float sky = 0.012f;   // fraction of the sky's ambient / specular ambient
    float key = 0.0f;     // 1: the sun / moon still reach in (shadowed by the walls and roof)
    float exposureBiasEv = 0.0f;   // v9 M4: local exposure bias (scene/EyeAdapt.h): how much further the eye opens inside
};

class WalkSpace {
public:
    void addFloor(const WalkFloor& f) { floors_.push_back(f); }
    void addWall(const Box& b) { walls_.push_back(b); }
    void addIndoor(const Box& b, IndoorLight l = {}) {
        indoor_.push_back(b);
        indoorLight_.push_back(l);
    }
    // v8: the floor region under (x, z) for a walker at feetY (any distance from the centre); false = none.
    bool pickFloor(float x, float z, float feetY, float& y) const;

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
    const std::vector<IndoorLight>& indoorLights() const { return indoorLight_; }   // parallel to indoorVolumes()

private:
    std::vector<WalkFloor> floors_;
    std::vector<Box> walls_;
    std::vector<Box> indoor_;
    std::vector<IndoorLight> indoorLight_;
};

// ---- v7.1 WP-T: open-world movement on foot and the camera's spring-arm probe -----------------------------------
// The whole map: the 20 m terrain disc (WalkSpace floors: ramp, tunnel, rooms), the apron out to the HESCO wall, the
// gate and the road out, and the flat ground beyond up to the treeline (soft bound). Pure CPU (tests/test_open.cpp).

// Ground under (x, z) for a walker whose feet are at feetY: WalkSpace floors inside the terrain disc, the terrain,
// and the apron / far ground level outside it (ws may be null).
float groundHeight(const WalkSpace* ws, float x, float z, float feetY);

// Soft map bound: inside kSoftStart the step passes unchanged; between kSoftStart and kHardEdge the outward part of
// the step fades out (quadratically); nothing gets past kHardEdge (the near hills start at about 42 m).
struct MapBounds {
    static constexpr float kSoftStart = 37.5f, kHardEdge = 41.5f;
    // New (x, z) for a step from (ox, oz) to (nx, nz).
    static void apply(float ox, float oz, float& nx, float& nz, float softStart = kSoftStart, float hardEdge = kHardEdge);
};

struct FootParams {
    float radius = 0.25f;         // body cylinder (soldier::kBodyRadius)
    float height = 1.8f;
    float step = 0.45f;           // highest ledge walked up without a jump (box tops below this become floor)
    float snapDown = 0.32f;       // drops up to this follow the ground; deeper ones start a fall
    float gravity = 16.0f;        // m/s^2 (game gravity, about 1.6 g: a snappy 0.6 s jump)
    float jumpSpeed = 4.8f;       // m/s take-off: apex 0.72 m
    bool treeline = true;         // collide with the treeline pines
    bool bounds = true;           // soft map bound
};

// Body moving on foot: position, vertical speed, on the ground or in the air.
struct FootBody {
    float x = 0.0f, z = 0.0f, feetY = 0.0f;
    float vy = 0.0f;              // m/s, up positive (in the air)
    bool grounded = true;
    float airTime = 0.0f;         // s since leaving the ground
    // Events of the last step
    bool jumped = false, landed = false;
    float landSpeed = 0.0f;       // m/s downwards at touch-down (landed)
    bool blocked = false;         // a wall / solid stopped part of the horizontal step
};

// Highest support under the body: the ground plus the tops of solids within a step above the feet.
float supportHeight(const WalkSpace* ws, const std::vector<Box>& solids, float x, float z, float feetY, const FootParams& p);
// One movement step: horizontal displacement (dx, dz) in metres (already speed * dt), jump request (grounded only),
// gravity, landing, ceilings, collisions (WalkSpace walls, solids taller than a step, treeline pines), map bound.
void stepFoot(FootBody& b, const WalkSpace* ws, const std::vector<Box>& solids, float dx, float dz, bool jump, float dt,
              const FootParams& p = FootParams{});

// Spring-arm probe: the free length (0..maxLen) along unit 'dir' from 'origin' for a sphere of 'radius' before it
// touches a solid box, a WalkSpace wall, the ground / a floor or a ceiling (feetY: the followed walker's level, picks
// the bunker floor). Boxes the origin starts inside are ignored (the subject's own volume).
float sphereCast(const WalkSpace* ws, const std::vector<Box>& solids, const Vec3& origin, const Vec3& dir, float maxLen,
                 float radius, float feetY);

}  // namespace ts
