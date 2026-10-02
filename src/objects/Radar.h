// Air-defence surveillance radar on its concrete plinth: lattice mast, equipment shelter, rotating head with a
// curved-array antenna (7 x 3 thin cube tiles bent along an arc), feed horn, counterweight, IFF bar and a
// blinking aviation light. Modes: ROTATE (12 rpm), SECTOR (+-45 deg sweep around a centre the user aims),
// STOP (manual azimuth). Elevation tilt -5..+20 deg.
// Azimuth convention: degrees clockwise seen from above, 0 = +Z (north); azimuthDeg() is that angle.
#pragma once

#include <memory>

#include "scene/Controllable.h"

namespace ts {

class Radar : public Controllable {
public:
    enum class Mode { Rotate, Sector, Stop };

    static constexpr float kRpm = 12.0f;             // 72 deg/s, one turn per 5 s
    static constexpr float kSectorHalfWidth = 45.0f;
    static constexpr float kSectorPeriod = 6.0f;     // seconds per full back-and-forth sweep
    static constexpr float kTiltMin = -5.0f, kTiltMax = 20.0f;
    static constexpr int kTileCols = 7, kTileRows = 3;

    Radar();

    const char* name() const override { return "AIR-DEFENCE RADAR"; }
    const char* controls() const override { return "ARROWS/mouse aim (sector centre / azimuth, tilt)  \\ mode  ENTER transmit"; }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext& w, const ControlInput* in) override;
    CameraHint camera(bool firstPerson) const override;
    std::string status() const override;
    Vec3 position() const override;

    float azimuthDeg() const { return azimuth_; }      // 0..360 clockwise from +Z, the antenna boresight
    float tiltDeg() const { return tilt_; }
    float sectorCentreDeg() const { return centre_; }
    Mode mode() const { return mode_; }
    const char* modeName() const;
    bool transmitting() const { return transmit_; }
    // Test hooks.
    void debugSetMode(Mode m);
    void debugSetCentre(float deg) { centre_ = deg; }
    const SceneNode* azimuthNode() const { return azNode_; }
    const SceneNode* antennaNode() const { return antenna_; }

private:
    void applyPose();

    std::unique_ptr<SceneNode> root_;
    SceneNode *azNode_ = nullptr, *elNode_ = nullptr, *antenna_ = nullptr, *beaconLamp_ = nullptr, *txLamp_ = nullptr,
              *ventLamp_ = nullptr;
    Mode mode_ = Mode::Rotate;
    float azimuth_ = 0.0f, omega_ = 72.0f;   // deg, deg/s
    float centre_ = 0.0f, phase_ = 0.0f;
    float tilt_ = 4.0f;
    bool transmit_ = true;
    float time_ = 0.0f;
};

std::unique_ptr<Controllable> makeRadar();

}  // namespace ts
