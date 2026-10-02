// Anti-aircraft Gatling gun (Phalanx / Gepard like) on a ground pedestal at layout::kAaGunSite.
//
//   root (site)
//     pedestal, base plate
//     Mount yaw (360 deg)        turntable, side cheeks, ammo drum, radome + rotating search antenna
//       Cradle pitch (-5..+85)   gun housing, EO sensor, ejection port
//         Barrel cluster         6 barrels revolving about the cluster axis (spin spools up in 0.5 s, down in 1.5 s)
//   plus a pool of 20 ballistic shell casings thrown out of the ejection port while firing.
//
// Fire (held): spin-up 0.5 s, then 30 rounds/s (= 6 barrels x 5 rev/s) as tracers at 1000 m/s with 3.5 mrad dispersion.
#pragma once

#include <memory>
#include <vector>

#include "scene/Controllable.h"

namespace ts {

class AaGun : public Controllable {
public:
    static constexpr int kBarrels = 6;
    static constexpr float kPitchMinDeg = -5.0f, kPitchMaxDeg = 85.0f, kSlewDeg = 60.0f;
    static constexpr float kSpoolUp = 0.5f, kSpoolDown = 1.5f, kMaxSpin = 5.0f;   // spin in rev/s
    static constexpr float kRoundsPerSecond = 30.0f, kMuzzleSpeed = 1000.0f, kDispersion = 0.0035f;
    static constexpr int kStartRounds = 1550, kCasingPool = 20;

    AaGun();

    const char* name() const override { return "AA-1 GATLING"; }
    const char* controls() const override { return "arrows/mouse aim  ENTER/Ctrl hold to fire  C sight"; }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return root_->local.translation; }
    bool onBlast(const Vec3& point, float power) override;

    int ownerId = 2;
    float yawDeg() const { return degrees(yaw_); }
    float pitchDeg() const { return pitch_; }
    float spin() const { return spin_; }                 // rev/s
    float spinAngleDeg() const { return spinAngle_; }
    int rounds() const { return rounds_; }
    int roundsFired() const { return fired_; }
    float scorch() const { return scorch_; }
    const SceneNode* cluster() const { return cluster_; }
    Vec3 muzzlePosition() const;
    Vec3 muzzleDirection() const;

private:
    void buildRig();
    void spawnCasing();

    struct Casing { Vec3 pos, vel; float age = 99.0f, spin = 0.0f; };

    std::unique_ptr<SceneNode> root_;
    SceneNode *mount_ = nullptr, *cradle_ = nullptr, *cluster_ = nullptr, *search_ = nullptr, *flash_ = nullptr;
    std::vector<SceneNode*> barrels_, casingNodes_, camoParts_;
    std::vector<Vec3> camoBase_;
    std::vector<Casing> casings_;

    float time_ = 0.0f, yaw_ = 0.0f, yawCmd_ = 0.0f, pitch_ = 20.0f, pitchCmd_ = 20.0f;
    float spin_ = 0.0f, spinAngle_ = 0.0f, rofAcc_ = 0.0f, flashT_ = 10.0f, heat_ = 0.0f;
    float sweepDir_ = 1.0f, outward_ = 0.0f, shake_ = 0.0f, scorch_ = 0.0f, searchAngle_ = 0.0f;
    int rounds_ = kStartRounds, fired_ = 0, casingNext_ = 0;
    unsigned rng_ = 12345u;
    bool firing_ = false, possessed_ = false;
    Mat4 cradleWorld_, mountWorld_;
    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return static_cast<float>(rng_ >> 8) / 16777216.0f; }
};

std::unique_ptr<Controllable> makeAaGun();

}  // namespace ts
