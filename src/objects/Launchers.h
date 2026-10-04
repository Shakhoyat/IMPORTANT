// Hidden-camp weapons (v5): a SAM launcher on a trailer, an MLRS rocket truck, two parked tanks in earth
// revetments, an earth-covered ammunition bunker, and the camouflage dressing (revetments, nets, cut branches)
// that hides all of them. Geometry is lab primitives only (Builder rows); every site lies on the outer apron.
//
//   SamLauncher   root (site, yaw) -> Trailer (static) -> Turntable (yaw, +-170 deg) -> Cradle (elevation 0..60 deg)
//                 -> 4 canisters (cap, rear hatch); hydraulic ram (barrel + rod) re-aimed every frame; round node
//   MlrsTruck     root -> Truck (static) -> Mount (yaw, +-165 deg) -> Pod (elevation 0..55 deg) -> 12 tubes (6 x 2);
//                 hydraulic ram; pool of rocket nodes for the ripple
//
// Controls (both): actionPressed (\) stow <-> ready, arrows / mouse traverse + elevate (only when ready),
// firePressed (ENTER) launches. The launch is a show effect (Combat::muzzleFlash / emitDust + a flying round node);
// no damaging projectile is created.
#pragma once

#include <array>
#include <memory>
#include <vector>

#include "scene/Controllable.h"

namespace ts {

namespace camp {

// A vehicle bay on the outer apron: the vehicle stands at (x, z) facing yaw (forward = (sin yaw, cos yaw)).
// halfW / halfD = inner half width and half depth of the U-shaped earth revetment (open at the rear, -Z); the
// earth walls reach 1.45 m beyond the inner face (sloped), 0.6 m behind the rear end and 1.45 m in front of it;
// 'front' = how far the net / gun barrel reaches in front of the vehicle centre. Plan metres.
struct Site {
    const char* name;
    float x, z, yawDeg;
    float halfW, halfD, front;
};
constexpr Site kSamSite{"SAM site", 25.1f, 6.7f, 60.0f, 2.2f, 4.0f, 5.45f};
constexpr Site kMlrsSite{"MLRS site", -11.5f, 22.5f, -15.0f, 2.2f, 4.5f, 5.95f};
constexpr Site kTankSouth{"Tank bay south", 9.9f, -25.0f, 150.0f, 2.0f, 3.5f, 6.2f};
constexpr Site kTankWest{"Tank bay west", -25.0f, -3.5f, 270.0f, 2.0f, 3.5f, 6.2f};
constexpr Site kAmmoSite{"Ammo bunker", 10.5f, 26.2f, 270.0f, 2.0f, 3.0f, 3.0f};
// The existing tank park (BaseLayout kMotorPool, yaw kTankParkYawDeg) also gets a revetment and a net.
constexpr float kPoolHalfW = 3.2f, kPoolHalfD = 4.6f, kPoolFront = 6.3f;

// Plan rectangle of a site in world metres (revetment + net): centre, half extents along its own axes.
struct PlanRect {
    float cx, cz, halfX, halfZ, yawDeg;
};
PlanRect planRect(const Site& s);
PlanRect poolPlanRect();
std::array<Site, 5> allSites();
}  // namespace camp

class SamLauncher : public Controllable {
public:
    static constexpr int kCanisters = 4;
    static constexpr float kElevMinDeg = 0.0f, kElevMaxDeg = 60.0f, kDefaultElevDeg = 45.0f;
    static constexpr float kAzLimitDeg = 170.0f;
    static constexpr float kElevRate = 14.0f, kElevAccel = 35.0f;   // deg/s, deg/s^2 (trapezoid profile)
    static constexpr float kAzRate = 22.0f, kAzAccel = 55.0f;
    static constexpr float kReloadSeconds = 8.0f;
    static constexpr float kMinFireElevDeg = 5.0f;
    static constexpr float kRoundLife = 2.0f;

    SamLauncher();
    const char* name() const override { return "SAM-1 LAUNCHER"; }
    const char* controls() const override {
        return "\\ stow/ready  arrows/mouse turntable + elevation  ENTER launch (reload 8 s)  C sight";
    }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return root_->local.translation; }
    std::vector<Box> dynamicObstacles() const override;

    // --- test / scene access ---
    int ownerId = 1;
    bool deployed() const { return deployed_; }
    float elevationDeg() const { return elev_.x; }
    float turntableDeg() const { return az_.x; }           // relative to the trailer
    float azimuthDeg() const;                              // compass heading of the canisters, 0 = +Z, 90 = +X
    int loadedCount() const;
    bool loaded(int k) const { return loaded_[k]; }
    float reloadLeft(int k) const { return reload_[k]; }
    bool capVisible(int k) const;
    bool roundInFlight() const { return flightT_ >= 0.0f; }
    Vec3 muzzleWorld(int k) const;
    Vec3 axisWorld() const;
    const SceneNode* canisterNode(int k) const { return canister_[k]; }
    const SceneNode* muzzleMarker(int k) const { return muzzleMark_[k]; }
    const SceneNode* roundNode() const { return round_; }
    void place(float x, float z, float yawDeg);

private:
    struct Slew {
        float x = 0.0f, v = 0.0f;
        void step(float target, float vmax, float acc, float dt);
    };
    void build();
    Vec3 muzzleLocal(int k) const;
    Vec3 axisLocal() const;
    Vec3 toWorld(const Vec3& p) const;
    Vec3 dirToWorld(const Vec3& d) const;
    void poseParts();
    void launch(const WorldContext& w);

    std::unique_ptr<SceneNode> root_;
    SceneNode *turn_ = nullptr, *cradle_ = nullptr, *ramBarrel_ = nullptr, *ramRod_ = nullptr, *round_ = nullptr;
    SceneNode* canister_[kCanisters] = {};
    SceneNode* cap_[kCanisters] = {};
    SceneNode* muzzleMark_[kCanisters] = {};
    SceneNode* flame_ = nullptr;
    SceneNode* lampMark_ = nullptr;
    Slew elev_, az_;
    float elevCmd_ = kDefaultElevDeg, azCmd_ = 0.0f;
    bool deployed_ = false;
    bool loaded_[kCanisters] = {true, true, true, true};
    float reload_[kCanisters] = {0, 0, 0, 0};
    float flightT_ = -1.0f;
    Vec3 flightPos_{}, flightDir_{};
    float yaw_ = 0.0f;   // rad
    float time_ = 0.0f;
};

class MlrsTruck : public Controllable {
public:
    static constexpr int kColumns = 6, kRows = 2, kTubes = kColumns * kRows;
    static constexpr float kElevMinDeg = 0.0f, kElevMaxDeg = 55.0f, kDefaultElevDeg = 35.0f;
    static constexpr float kAzLimitDeg = 165.0f;
    static constexpr float kElevRate = 12.0f, kElevAccel = 30.0f;
    static constexpr float kAzRate = 18.0f, kAzAccel = 45.0f;
    static constexpr float kRippleInterval = 0.25f;   // s between tubes
    static constexpr float kReloadSeconds = 15.0f;    // after the last tube fired
    static constexpr float kMinFireElevDeg = 5.0f;
    static constexpr int kRocketPool = 6;
    static constexpr float kRocketLife = 1.8f;

    MlrsTruck();
    const char* name() const override { return "MLRS-1 ROCKET TRUCK"; }
    const char* controls() const override {
        return "\\ stow/ready  arrows/mouse mount + elevation  ENTER ripple fire (12 tubes, 0.25 s)  C sight";
    }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return root_->local.translation; }
    std::vector<Box> dynamicObstacles() const override;

    int ownerId = 1;
    bool deployed() const { return deployed_; }
    float elevationDeg() const { return elev_.x; }
    float mountDeg() const { return az_.x; }
    float azimuthDeg() const;
    int loadedCount() const;
    bool rippling() const { return rippling_; }
    int firedInRipple() const { return rippleFired_; }
    float reloadLeft() const { return reload_; }
    bool capVisible(int tube) const;
    Vec3 muzzleWorld(int tube) const;
    Vec3 axisWorld() const;
    const SceneNode* tubeNode(int i) const { return tube_[i]; }
    const SceneNode* muzzleMarker(int i) const { return muzzleMark_[i]; }
    int rocketsInFlight() const;
    void place(float x, float z, float yawDeg);

private:
    struct Slew {
        float x = 0.0f, v = 0.0f;
        void step(float target, float vmax, float acc, float dt);
    };
    struct Rocket {
        SceneNode* node = nullptr;
        float t = -1.0f;
        Vec3 pos{}, dir{};
    };
    void build();
    Vec3 muzzleLocal(int tube) const;
    Vec3 axisLocal() const;
    Vec3 toWorld(const Vec3& p) const;
    Vec3 dirToWorld(const Vec3& d) const;
    void poseParts();
    void fireTube(const WorldContext& w, int tube);

    std::unique_ptr<SceneNode> root_;
    SceneNode *mount_ = nullptr, *pod_ = nullptr, *ramBarrel_ = nullptr, *ramRod_ = nullptr;
    SceneNode* tube_[kTubes] = {};
    SceneNode* cap_[kTubes] = {};
    SceneNode* muzzleMark_[kTubes] = {};
    Rocket rockets_[kRocketPool];
    Slew elev_, az_;
    float elevCmd_ = kDefaultElevDeg, azCmd_ = 0.0f;
    bool deployed_ = false;
    bool loaded_[kTubes];
    bool rippling_ = false;
    int rippleNext_ = 0, rippleFired_ = 0;
    float rippleClock_ = 0.0f, reload_ = 0.0f;
    float yaw_ = 0.0f;
    float time_ = 0.0f;
};

// Factory called by Scene: [SAM-1, MLRS-1, 2 parked tanks, ammo bunker, camp dressing].
std::vector<std::unique_ptr<Controllable>> makeLaunchers();

}  // namespace ts
