// The outer apron of the military base (r 20 .. 36 m): HESCO perimeter wall with guard towers and concertina
// wire, outer gate with boom barrier, road, helipad, windsock, flagpole, motor pool (containers, trucks,
// camouflage net), fuel depot, generator, light towers, Quonset barracks and the sandbag / fence surrounds
// of the AA gun pit and the radar. Every coordinate comes from objects/BaseLayout.h.
// Geometry is lab primitives only (Builder rows). Nothing outside the compound is an occluder (the rover grid
// only covers r < 20); large solids are UAV / projectile obstacles.
#pragma once

#include <memory>
#include <vector>

#include "objects/BaseLayout.h"
#include "scene/Controllable.h"

namespace ts {

// Shared deterministic wind (the flag and the windsock read the same function of time).
float windDirectionDeg(float t);  // direction the air moves towards, degrees about +Y (0 = +Z, 90 = +X)
float windStrength(float t);      // 0.35 .. 1.0

// Boom barrier at the outer gate (key: actionPressed / firePressed toggles open / closed).
// Arm rotates 0 .. 85 deg about a horizontal pivot, 2.5 s end to end with smoothstep easing.
class GateBarrier : public Controllable {
public:
    static constexpr float kMaxAngleDeg = 85.0f;
    static constexpr float kTravelSeconds = 2.5f;
    static constexpr float kBlockingAngleDeg = 15.0f;  // arm below this angle blocks the road

    GateBarrier();
    const char* name() const override { return "GATE BARRIER"; }
    const char* controls() const override { return "ENTER / \\ raise or lower the boom"; }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return pivotWorld(); }
    std::vector<Box> dynamicObstacles() const override;

    float angleDeg() const { return angle_; }
    bool moving() const { return t_ != target_; }
    bool wantsOpen() const { return target_ > 0.5f; }
    Vec3 pivotWorld() const;

private:
    std::unique_ptr<SceneNode> root_;
    SceneNode* arm_ = nullptr;
    SceneNode* lamp_ = nullptr;
    SceneNode* armLamp_ = nullptr;
    float t_ = 0.0f, target_ = 0.0f, angle_ = 0.0f, clock_ = 0.0f;
};

// Windsock on a pole: a hierarchy of five cone segments, each drooping a little more when the wind is weak.
class Windsock : public Controllable {
public:
    Windsock();
    const char* name() const override { return "WINDSOCK"; }
    const char* controls() const override { return "(scenery)"; }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override;
    bool possessable() const override { return false; }
    int segmentCount() const { return static_cast<int>(segments_.size()); }
    const std::vector<SceneNode*>& segments() const { return segments_; }

private:
    std::unique_ptr<SceneNode> root_;
    SceneNode* swivel_ = nullptr;
    std::vector<SceneNode*> segments_;  // segment k is the child group of segment k-1
};

// Flag on the flagpole: twelve strips in a chain, each yawed by a travelling sine wave.
class WavingFlag : public Controllable {
public:
    static constexpr int kStrips = 12;
    static constexpr float kStripWidth = 0.2f, kFlagHeight = 1.5f;
    WavingFlag();
    const char* name() const override { return "FLAG"; }
    const char* controls() const override { return "(scenery)"; }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override;
    bool possessable() const override { return false; }
    const std::vector<SceneNode*>& strips() const { return strips_; }
    SceneNode* hoist() const { return hoist_; }

private:
    std::unique_ptr<SceneNode> root_;
    SceneNode* hoist_ = nullptr;
    std::vector<SceneNode*> strips_;
};

struct BaseRig {
    std::unique_ptr<SceneNode> root;                      // static apron geometry (Gouraud / Flat)
    std::vector<std::unique_ptr<Controllable>> objects;   // GateBarrier, Windsock, WavingFlag
};

BaseRig buildBase();

// Footprints that must stay free of Base geometry (the tank, AA gun, helicopter and radar are built there by
// other engineers). Plan view, world metres. Flush markings and pads (top <= kFlushHeight above the apron)
// may overlap a zone.
struct ClearZone {
    const char* name;
    float cx, cz;
    float halfX, halfZ;  // half extents along the zone's own axes (circle: halfX = radius)
    float yawDeg;        // zone axis rotation about Y (forward = (sin yaw, cos yaw))
    bool circle;
};
constexpr float kFlushHeight = 0.12f;
std::vector<ClearZone> baseClearZones();
// baseClearZones() plus the plan rectangles of the camp bays (SAM, MLRS, two parked tanks, ammo bunker and the
// revetment around the original tank spot). Base geometry keeps out of all of them (flush markings excepted).
std::vector<ClearZone> campClearZones();

}  // namespace ts
