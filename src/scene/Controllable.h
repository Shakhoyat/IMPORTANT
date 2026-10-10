// Objects the user can take control of (keys 5..9, 0) and animated objects with their own state machine.
// Every implementation keeps its own node hierarchy and sets the nodes' local transforms in update();
// Scene adds root() to the scene graph (unless it already has a parent) and calls update() once per frame.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/Occlusion.h"
#include "scene/SceneNode.h"

namespace ts {

class Combat;
class WalkSpace;

// Player input for the possessed object, sampled once per frame. Axes are in [-1, 1].
struct ControlInput {
    float forward = 0.0f;        // W / S
    float turn = 0.0f;           // D / A (positive = turn right)
    float lift = 0.0f;           // PgUp / PgDn (helicopter, UAV)
    float aimYaw = 0.0f;         // Right / Left arrow (positive = right), held
    float aimPitch = 0.0f;       // Up / Down arrow (positive = up), held
    float aimYawDelta = 0.0f;    // mouse look this frame, degrees (positive = right)
    float aimPitchDelta = 0.0f;  // degrees (positive = up)
    bool fire = false;           // Enter / Left Ctrl held (automatic weapons)
    bool firePressed = false;    // went down this frame (single shots)
    bool actionPressed = false;  // Backslash: secondary action (open / close, start / land, mode)
    bool boost = false;          // Shift
    // v7.1 WP-T (open-world movement of a possessed soldier; other controllables ignore them)
    float strafe = 0.0f;         // sideways walk, positive = right (third-person aim, first person with mouse look)
    bool jump = false;           // Space went down this frame
};

// What the world offers during update().
struct WorldContext {
    float time = 0.0f, dt = 0.0f;
    Combat* combat = nullptr;                      // may be null (unit tests)
    const std::vector<Box>* obstacles = nullptr;   // static solid parts, for vehicle collision; may be null
    float wetness = 0.0f;
    // v6 day/night cycle (defaults = the v5 night scene)
    const WalkSpace* walk = nullptr;   // bunker floors / walls for anything moving on foot; may be null
    float hour = 23.0f;                // time of day, 0..24
    float daylight = 0.0f;             // 0 night .. 1 full day
    bool beamsOn = true;               // searchlights / lamps allowed (night)
    bool soldierAtBlastDoors = false;  // a soldier is in the entrance trench or the tunnel near the blast doors
};

// How the camera frames a possessed object (world space, recomputed every frame).
struct CameraHint {
    Vec3 eye, target;
    float fovDeg = 55.0f;
    bool sight = false;  // eye is a gun sight / cockpit view: the HUD draws a reticle
};

class Controllable {
public:
    virtual ~Controllable() = default;
    virtual const char* name() const = 0;       // HUD label, e.g. "MBT-1 TANK"
    virtual const char* controls() const = 0;   // one help line, e.g. "W/S drive  A/D steer  arrows turret  ENTER fire"
    virtual SceneNode* root() = 0;
    // in == nullptr: not possessed (idle or autonomous behaviour).
    virtual void update(const WorldContext& w, const ControlInput* in) = 0;
    // firstPerson: the sight / cockpit view instead of the third-person chase view.
    virtual CameraHint camera(bool firstPerson) const = 0;
    virtual std::string status() const = 0;     // one HUD line, e.g. "SPEED 4.2 M/S  TURRET 032  GUN +04  AMMO 18"
    virtual Vec3 position() const = 0;
    virtual bool possessable() const { return true; }       // false: animated scenery (flag)
    virtual bool moves() const { return true; }             // false: static dressing (shadows are cached)
    virtual std::vector<Box> dynamicObstacles() const { return {}; }  // closed doors, lowered barrier arm
    // Friendly mark for the UAV squad's identification: measurements within this radius are not engaged
    // (0 = not a friendly mark). Vehicles 4.5 m, soldiers about 1.2 m.
    virtual float friendlyRadius() const { return 4.5f; }
    // Damage from an explosion at 'point' (world); return true if the object reacted.
    virtual bool onBlast(const Vec3& point, float power) { (void)point; (void)power; return false; }
};

}  // namespace ts
