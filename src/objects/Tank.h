// Main battle tank (Leopard / M1 like, scaled 0.8): user-controllable ground weapon with a hierarchical rig.
//
//   root (world position, yaw, terrain tilt)
//     Hull (suspension heave + pitch spring: acceleration squat, recoil rock)
//       hull plates, skirts, lights, exhaust grille, antennas (spring sway)
//       Track L / Track R : 40 link cubes each, placed every frame from an arc-length parameter on the closed
//                           track path (top run, front arc, bottom run, rear arc), road wheels / sprocket /
//                           idler / return rollers spinning with the belt
//       Turret yaw (stabilised against the hull)
//         cheeks, bustle, sights, commander cupola -> rotating machine gun, smoke launchers
//         Gun pitch (-8..+20 deg) -> mantlet, Barrel recoil (slides back 0.35 m) -> barrel, fume extractor, muzzle brake
//
// Drive: skid steer. Left / right track speeds follow forward +- turn with acceleration limits;
// body speed v = (vl + vr) / 2, yaw rate = (vr - vl) / kTrackSeparation (yaw measured about +Y, so a right
// turn, vr < vl, has a negative yaw rate).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "scene/Controllable.h"

namespace ts {

// Closed track path in the hull's (z forward, y up) plane: a stadium around sprocket (rear) and idler (front).
namespace tankpath {
constexpr float kSprocketZ = -2.8f, kIdlerZ = 2.8f, kCentreY = 0.55f, kRadius = 0.5f;
float perimeter();
// Point at arc length s (0..perimeter): top run runs forward, front arc, bottom run backward, rear arc.
// 'phi' is the rotation about the hull X axis (radians) that takes a link's local +Y onto the outward normal.
void point(float s, float& z, float& y, float& phi);
// Shortest distance from (z, y) to the path (for tests).
float distance(float z, float y);
}  // namespace tankpath

class Tank : public Controllable {
public:
    static constexpr int kLinksPerSide = 40;
    static constexpr int kRoadWheels = 7;
    static constexpr float kTrackSeparation = 2.2f;   // distance between the two track centre lines
    static constexpr float kHullLength = 6.2f, kHullWidth = 2.9f;
    static constexpr float kMaxForward = 8.0f, kMaxReverse = 3.0f, kBoost = 1.5f;
    static constexpr float kReload = 3.5f, kMuzzleSpeed = 120.0f;
    static constexpr float kGunMinDeg = -8.0f, kGunMaxDeg = 20.0f;
    static constexpr float kTurretSlewDeg = 40.0f, kGunSlewDeg = 20.0f;
    static constexpr float kRecoilDistance = 0.35f, kRecoilReturn = 0.6f;
    static constexpr int kStartRounds = 18;

    Tank();
    Tank(const std::string& idPrefix, const std::string& objectName);   // second / third hull: unique node ids

    const char* name() const override { return "MBT-1 TANK"; }
    const char* controls() const override {
        return "W/S drive  A/D steer  Shift boost  arrows/mouse turret+gun  ENTER fire  C sight";
    }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return root_->local.translation; }
    std::vector<Box> dynamicObstacles() const override;
    bool onBlast(const Vec3& point, float power) override;

    // --- scene / test access ---
    int ownerId = 1;                                   // id passed to Combat::fire*
    void place(float x, float z, float yawDeg);        // teleport (terrain height is applied on the next update)
    const SceneNode* link(int side, int i) const { return links_[side][i]; }   // side 0 = left (+X), 1 = right
    float trackPhase(int side) const { return phase_[side]; }                  // metres the belt has travelled
    float leftSpeed() const { return vl_; }
    float rightSpeed() const { return vr_; }
    float speed() const { return 0.5f * (vl_ + vr_); }
    float yawDeg() const { return degrees(yaw_); }
    float turretYawWorldDeg() const { return degrees(turretYaw_); }
    float gunPitchDeg() const { return gunPitch_; }
    float recoil() const { return recoilNow_; }
    float reloadLeft() const { return reload_; }
    int rounds() const { return rounds_; }
    float hullHeave() const { return heave_; }
    float scorch() const { return scorch_; }
    Vec3 muzzlePosition() const;
    Vec3 muzzleDirection() const;
    void lockTurret(bool lock) { turretLocked_ = lock; }   // parked: no idle scanning, gun held at +3 deg

private:
    void buildRig(const std::string& idPrefix, const std::string& objectName);
    void animateTracks(float dt);
    void updateTurret(const WorldContext& w, const ControlInput* in);
    void applyTerrain(float dt);
    bool blocked(float x, float z, float yaw, const std::vector<Box>* obstacles) const;

    std::unique_ptr<SceneNode> root_;
    SceneNode *hull_ = nullptr, *turret_ = nullptr, *gunPitchNode_ = nullptr, *recoilNode_ = nullptr;
    SceneNode *mgYaw_ = nullptr, *mgPitch_ = nullptr, *antenna_[2] = {nullptr, nullptr};
    SceneNode *headlight_[2] = {nullptr, nullptr}, *grille_ = nullptr, *muzzleFlashNode_ = nullptr, *barrelNode_ = nullptr;
    SceneNode* links_[2][kLinksPerSide] = {};
    SceneNode* trackGroup_[2] = {nullptr, nullptr};
    std::vector<SceneNode*> roadWheels_[2];   // spin groups (road wheels, sprocket, idler)
    std::vector<float> wheelRadius_[2];
    std::vector<SceneNode*> returnRollers_[2];
    std::vector<SceneNode*> camoParts_;       // darkened by blasts
    std::vector<Vec3> camoBase_;

    // state
    float time_ = 0.0f;
    float x_ = 0.0f, z_ = 0.0f, yaw_ = 0.0f;        // yaw (rad) about +Y: forward = (sin yaw, 0, cos yaw)
    float vl_ = 0.0f, vr_ = 0.0f;
    float phase_[2] = {0.0f, 0.0f};
    float prevSpeed_ = 0.0f, accel_ = 0.0f, yawRate_ = 0.0f;
    float pitchTerrain_ = 0.0f, rollTerrain_ = 0.0f;  // smoothed terrain tilt (rad)
    float dynPitch_ = 0.0f, dynPitchVel_ = 0.0f;      // suspension pitch (rad, + = nose down)
    float heave_ = 0.0f;
    float turretYaw_ = 0.0f, turretCmd_ = 0.0f;       // world yaw (rad) and commanded yaw
    float gunPitch_ = 0.0f, gunCmd_ = 0.0f;           // degrees relative to the turret
    float mgYawWorld_ = 0.0f;
    float reload_ = 0.0f, recoilT_ = 10.0f, recoilNow_ = 0.0f;
    int rounds_ = kStartRounds;
    float barrelHeat_ = 0.0f;
    float flashT_ = 10.0f;
    float shake_ = 0.0f, scorch_ = 0.0f;
    float antSwayX_ = 0.0f, antVelX_ = 0.0f, antSwayZ_ = 0.0f, antVelZ_ = 0.0f;
    float scanDir_ = 1.0f, dustAcc_ = 0.0f;
    bool possessed_ = false, lightsOn_ = false, turretLocked_ = false;
    Mat4 pitchWorld_, turretWorld_;                   // cached for camera / muzzle
};

// Tank standing in a camp bay: not possessable, turret locked forward, unique node ids (index 2, 3, ...).
class ParkedTank : public Tank {
public:
    ParkedTank(const Vec3& pos, float yawDeg, int index);
    const char* name() const override { return name_.c_str(); }
    const char* controls() const override { return "(parked in its revetment)"; }
    bool possessable() const override { return false; }
    bool moves() const override { return false; }  // turret locked, parked: shadows go in the static cache
    void update(const WorldContext& w, const ControlInput*) override { Tank::update(w, nullptr); }

private:
    std::string name_;
};

std::unique_ptr<Controllable> makeTank();
std::unique_ptr<Controllable> makeParkedTank(const Vec3& pos, float yawDeg, int index = 2);

}  // namespace ts
