// Light utility / scout helicopter (MH-6 Little Bird class) on the helipad: user-controllable, with a
// start-up / hover / fly / landing state machine, a damped-spring attitude model, a 5-blade main rotor with
// coning + cyclic flapping, a tail rotor, rotor blur discs, nav lights, a strobe and a pilot-aimed searchlight.
// Hierarchy: root (position, yaw) -> body (pitch, roll) -> {cabin, boom, skids, rotor head -> {tip-path-plane
// disc, spinning rotor -> 5 x (pivot -> flap/feather node -> blade)}, tail rotor, searchlight gimbal}.
#pragma once

#include <memory>

#include "scene/Controllable.h"

namespace ts {

class Helicopter : public Controllable {
public:
    enum class State { Parked, Startup, Hover, Fly, Landing };

    // Tunables (also quoted by the tests / report).
    static constexpr int kBlades = 5;
    static constexpr float kRotorRpm = 300.0f;        // nominal main rotor speed (5 rev/s)
    static constexpr float kTailRatio = 5.0f;         // tail rotor turns 5x faster
    static constexpr float kSpoolUpSeconds = 6.0f;
    static constexpr float kAlertIdle = 0.38f;   // rotor fraction of an unmanned helicopter on alert (flight idle)
    static constexpr float kSpoolDownSeconds = 8.0f;
    static constexpr float kMaxSpeed = 15.0f;         // m/s cruise
    static constexpr float kBoostSpeed = 25.0f;       // m/s with Shift
    static constexpr float kClimbRate = 4.0f;         // m/s
    static constexpr float kMinAgl = 0.5f;            // airborne floor
    static constexpr float kMaxAgl = 40.0f;
    static constexpr float kMaxRadius = 60.0f;        // from the world origin
    static constexpr float kLandMaxSpeed = 6.0f;      // landing request accepted at or below this ground speed
    static constexpr float kLandMaxAgl = 20.0f;       // ... and at or below this height
    static constexpr float kLandMaxSlope = 0.15f;     // rise / run over the skid footprint
    static constexpr float kGroundClearance = 1.0f;   // body pivot above the skid bottoms

    Helicopter();

    const char* name() const override { return "MH-6 HELICOPTER"; }
    const char* controls() const override {
        return "W/S fly  A/D turn  PGUP/PGDN climb  SHIFT boost  ARROWS aim searchlight  ENTER light  \\ start/land";
    }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override { return pos_ + Vec3{0.0f, kGroundClearance + 0.3f, 0.0f}; }
    std::vector<Box> dynamicObstacles() const override;

    // --- read-only state (HUD, tests) ---
    State state() const { return state_; }
    const char* stateName() const;
    float rotorFraction() const { return rotorFrac_; }                 // 0..1 of nominal rpm
    double rotorAngleTotalDeg() const { return rotorTotalDeg_; }       // unwrapped, integrates rpm * dt
    float bladeAzimuthDeg(int i) const;                                // wrapped 0..360, from the node tree
    float tailRotorAngleDeg() const { return tailDeg_; }
    float altitudeAgl() const { return pos_.y - groundAt(pos_.x, pos_.z); }
    float pitchDeg() const { return pitchDeg_; }
    float rollDeg() const { return rollDeg_; }
    float yawDeg() const { return yawDeg_; }
    Vec3 velocity() const { return vel_; }
    float groundSpeed() const { return std::sqrt(vel_.x * vel_.x + vel_.z * vel_.z); }
    bool searchlightOn() const { return lightOn_; }
    int landRefusals() const { return refusals_; }
    const SceneNode* bladePivot(int i) const { return pivots_[i]; }
    const SceneNode* mainBlurDisc() const { return mainBlur_; }
    // Test hook: place the aircraft (world skid-bottom position) without going through the flight model.
    void debugPlace(const Vec3& skidPos, float yawDeg, State s, float rotorFrac);

    static float groundAt(float x, float z);   // terrain + apron + helipad surface

private:
    void applyPose(float dt);
    bool landingAllowed(const char** why) const;

    std::unique_ptr<SceneNode> root_;
    SceneNode *body_ = nullptr, *tpp_ = nullptr, *spin_ = nullptr, *tailSpin_ = nullptr;
    SceneNode *pivots_[kBlades] = {}, *flaps_[kBlades] = {};
    SceneNode *mainBlur_ = nullptr, *tailBlur_ = nullptr;
    SceneNode *gimbalYaw_ = nullptr, *gimbalPitch_ = nullptr, *lens_ = nullptr, *beam_ = nullptr;
    SceneNode *navRed_ = nullptr, *navGreen_ = nullptr, *navWhite_ = nullptr, *strobe_ = nullptr;
    SceneNode *cowling_ = nullptr, *exhaust_ = nullptr;

    State state_ = State::Parked;
    Vec3 pos_;               // skid bottom centre, world
    Vec3 vel_;
    float yawDeg_ = 0.0f, yawRate_ = 0.0f;
    float pitchDeg_ = 0.0f, pitchVel_ = 0.0f, rollDeg_ = 0.0f, rollVel_ = 0.0f;
    float accFwdS_ = 0.0f, accLeftS_ = 0.0f, turnRateS_ = 0.0f;
    float tiltFwd_ = 0.0f, tiltLeft_ = 0.0f;   // rotor tip-path-plane tilt (cyclic), degrees
    float rotorFrac_ = 0.0f;
    double rotorTotalDeg_ = 0.0;
    float tailDeg_ = 0.0f;
    bool airborne_ = false;
    float time_ = 0.0f;
    float dustAcc_ = 0.0f;
    // searchlight: pan about the body Y (positive = left), tilt down from horizontal
    float lightPan_ = 0.0f, lightDown_ = 55.0f;
    bool lightOn_ = false;
    bool beamsAllowed_ = true;   // v8: the visible beam cone only at night (a sunlit beam is invisible)
    float msgTimer_ = 0.0f;
    const char* msg_ = "";
    int refusals_ = 0;
};

std::unique_ptr<Controllable> makeHelicopter();

}  // namespace ts
