#include "objects/Radar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/BaseLayout.h"
#include "objects/Builder.h"
#include "objects/PropKit.h"

namespace ts {

namespace {
constexpr float kHeadY = 5.0f;     // azimuth platform height above the plinth base
constexpr float kElevY = 0.9f;     // elevation axis above the platform
constexpr float kDishZ0 = 1.0f;    // dish centre in front of the elevation axis
constexpr float kArcR = 5.0f;      // arc radius of the bent antenna
constexpr float kTileStep = 7.5f;  // degrees of arc per tile column

float wrap360(float a) {
    a = std::fmod(a, 360.0f);
    return a < 0.0f ? a + 360.0f : a;
}
float angDiff(float to, float from) {  // shortest signed difference, degrees
    float d = std::fmod(to - from, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}
}  // namespace

Radar::Radar() {
    using P = PrimitiveType;
    Builder b("Air-defence radar", "RDR", ShadingModel::Phong);
    const Vec3 site = layout::kRadarSite;
    root_ = b.root("Air-defence radar", site, "Surveillance radar site (user-controllable)");
    SceneNode* r = root_.get();

    const Material concrete = makeMaterial({0.16f, 0.16f, 0.15f}, 0.12f, Surface::Concrete, 0.85f, 0.0f);
    const Material steel = withPbr({{0.30f, 0.31f, 0.32f}, 0.28f, 0.0f, 0.7f, 64.0f}, Surface::Metal, 0.5f, 0.85f);
    const Material dark = withPbr({{0.13f, 0.14f, 0.15f}, 0.28f, 0.0f, 0.5f, 48.0f}, Surface::Metal, 0.55f, 0.7f);
    const Material tile = withPbr({{0.20f, 0.23f, 0.22f}, 0.30f, 0.0f, 0.6f, 64.0f}, Surface::Paint, 0.45f, 0.4f);
    const Material white = withPbr({{0.38f, 0.39f, 0.37f}, 0.30f, 0.0f, 0.4f, 32.0f}, Surface::Paint, 0.5f, 0.1f);
    const Material shelter = makeMaterial({0.07f, 0.09f, 0.06f}, 0.35f, Surface::Corrugated, 0.6f, 0.4f);
    const Material doorMat = makeMaterial({0.05f, 0.065f, 0.045f}, 0.30f, Surface::SteelPlate, 0.55f, 0.6f);
    Material beacon = asLight(Material{{1.0f, 0.05f, 0.03f}, 0.5f, 0.0f, 0.5f, 64.0f}, 2.5f, 14.0f);
    beacon.surface = Surface::Glass;
    beacon.roughness = 0.1f;
    Material tx{{0.1f, 1.0f, 0.3f}, 0.4f, 0.0f, 0.4f, 64.0f};
    tx.surface = Surface::Glass;
    tx.roughness = 0.1f;
    Material vent = asLight(Material{{1.0f, 0.62f, 0.2f}, 0.4f, 1.0f, 0.2f, 16.0f}, 0.8f, 5.0f);
    vent.roughness = 0.6f;

    // --- fixed site ---
    SceneNode* plinth = b.part(r, "Concrete plinth", P::Cube, {4.6f, 0.6f, 4.6f}, {0, 0, 0}, {0, 0.3f, 0}, concrete,
                               "Foundation block under the mast");
    plinth->occluder = true;

    // Four-leg lattice mast: legs lean inward; three rings and cross braces.
    const float y0 = 0.6f, y1 = 4.65f;
    const float h0 = 0.8f, h1 = 0.45f;
    auto legPt = [&](int sx, int sz, float y) {
        const float t = (y - y0) / (y1 - y0), h = lerpf(h0, h1, t);
        return Vec3{sx * h, y, sz * h};
    };
    const int cx[4] = {1, 1, -1, -1}, cz[4] = {1, -1, -1, 1};
    for (int i = 0; i < 4; ++i) {
        SceneNode* leg = strut(b, r, "Mast leg", P::Cylinder, legPt(cx[i], cz[i], y0), legPt(cx[i], cz[i], y1), 0.14f, steel,
                               "Lattice mast leg");
        leg->occluder = true;
    }
    const float ringY[4] = {0.6f, 1.95f, 3.3f, 4.65f};
    for (int k = 1; k < 4; ++k) {
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            strut(b, r, "Mast ring bar", P::Cylinder, legPt(cx[i], cz[i], ringY[k]), legPt(cx[j], cz[j], ringY[k]), 0.07f,
                  steel, "Horizontal lattice bar");
        }
    }
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            const bool flip = (k + i) & 1;
            strut(b, r, "Mast brace", P::Cylinder, legPt(cx[i], cz[i], flip ? ringY[k + 1] : ringY[k]),
                  legPt(cx[j], cz[j], flip ? ringY[k] : ringY[k + 1]), 0.05f, steel, "Diagonal lattice brace");
        }
    }
    b.part(r, "Mast platform", P::Cube, {1.5f, 0.12f, 1.5f}, {0, 0, 0}, {0, 4.7f, 0}, steel, "Top platform under the bearing");
    b.part(r, "Bearing housing", P::Cylinder, {1.0f, 1.0f, 0.3f}, {90, 0, 0}, {0, 4.9f, 0}, dark, "Azimuth bearing");
    txLamp_ = b.part(r, "On-air lamp", P::Sphere, {0.14f, 0.14f, 0.14f}, {0, 0, 0}, {0.65f, 4.82f, 0.65f}, tx,
                     "Green lamp: lit while the radar transmits");
    txLamp_->animation = "Emissive while TRANSMIT is on";

    // Equipment shelter beside the plinth (door faces the mast, lit vent faces the base).
    SceneNode* box = b.part(r, "Equipment shelter", P::Cube, {2.4f, 2.3f, 3.0f}, {0, 0, 0}, {-4.6f, 1.3f, 0.5f}, shelter,
                            "Corrugated electronics shelter");
    box->occluder = true;
    b.part(r, "Shelter slab", P::Cube, {3.0f, 0.15f, 3.6f}, {0, 0, 0}, {-4.6f, 0.075f, 0.5f}, concrete, "Shelter base slab");
    b.part(r, "Shelter door", P::Cube, {0.06f, 1.9f, 0.9f}, {0, 0, 0}, {-3.37f, 1.1f, 0.0f}, doorMat, "Steel access door");
    b.part(r, "Door handle", P::Cube, {0.08f, 0.06f, 0.2f}, {0, 0, 0}, {-3.32f, 1.1f, 0.3f}, steel, "Handle");
    b.part(r, "Door step", P::Cube, {0.5f, 0.2f, 1.2f}, {0, 0, 0}, {-3.1f, 0.25f, 0.0f}, concrete, "Concrete step");
    ventLamp_ = b.part(r, "Lit vent", P::Cube, {0.08f, 0.3f, 0.9f}, {0, 0, 0}, {-5.83f, 1.9f, 0.6f}, vent,
                       "Warm light from the ventilation louvre (practical light)");
    b.part(r, "Roof air conditioner", P::Cube, {0.9f, 0.4f, 0.9f}, {0, 0, 0}, {-4.6f, 2.65f, 1.2f}, dark, "HVAC unit");
    b.part(r, "AC fan", P::Cylinder, {0.7f, 0.7f, 0.05f}, {90, 0, 0}, {-4.6f, 2.87f, 1.2f}, steel, "Condenser fan grille");
    strut(b, r, "Power cable", P::Cylinder, {-3.4f, 0.5f, 1.4f}, {-2.3f, 0.5f, 1.2f}, 0.09f, dark, "Cable run to the plinth");
    strut(b, r, "Mast cable duct", P::Cylinder, {-2.3f, 0.6f, 1.2f}, {-0.5f, 4.6f, 0.45f}, 0.07f, dark, "Cable up the mast");

    // --- rotating head ---
    azNode_ = b.group(r, "Azimuth head", {0, kHeadY, 0}, {0, 0, 0}, "Rotating platform carrying the elevation yoke");
    azNode_->animation = "ROTATE 12 rpm / SECTOR +-45 deg sweep / STOP manual; yaw = -azimuth";
    b.part(azNode_, "Pedestal", P::Cube, {1.3f, 0.4f, 1.3f}, {0, 0, 0}, {0, 0.2f, 0}, dark, "Turntable casting");
    b.part(azNode_, "Yoke cheek left", P::Cube, {0.18f, 1.1f, 0.8f}, {0, 0, 0}, {0.7f, 0.85f, 0}, dark, "Trunnion bearing arm");
    b.part(azNode_, "Yoke cheek right", P::Cube, {0.18f, 1.1f, 0.8f}, {0, 0, 0}, {-0.7f, 0.85f, 0}, dark, "Trunnion bearing arm");
    // Aviation light on a pole at the highest point (pole on the axis; the dish is ahead of the axis).
    b.part(azNode_, "Beacon pole", P::Cylinder, {0.07f, 0.07f, 3.6f}, {90, 0, 0}, {0, 2.2f, 0}, steel, "Obstruction light pole");
    beaconLamp_ = b.part(azNode_, "Aviation light", P::Sphere, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, {0, 4.05f, 0}, beacon,
                         "Blinking red obstruction light at the highest point");
    beaconLamp_->animation = "Blinks 1 Hz";

    elNode_ = b.group(azNode_, "Elevation yoke", {0, kElevY, 0}, {0, 0, 0}, "Tilts the antenna -5..+20 deg");
    elNode_->animation = "Rotation about X = -tilt (aimPitch)";
    b.part(elNode_, "Trunnion", P::Cylinder, {0.14f, 0.14f, 1.5f}, {0, 90, 0}, {0, 0, 0}, steel, "Elevation axle");
    antenna_ = b.group(elNode_, "Antenna array", {0, 0, kDishZ0}, {0, 0, 0}, "Curved array of 7 x 3 tiles");
    // Bent array: each column follows the arc, tile normal pointing at the feed.
    for (int i = 0; i < kTileCols; ++i) {
        const float aDeg = (i - 3) * kTileStep;
        const float a = radians(aDeg);
        const float x = kArcR * std::sin(a), z = kArcR * (1.0f - std::cos(a));
        for (int j = 0; j < kTileRows; ++j) {
            char nm[48];
            std::snprintf(nm, sizeof(nm), "Array tile c%d r%d", i + 1, j + 1);
            b.part(antenna_, nm, P::Cube, {0.64f, 0.78f, 0.05f}, {0, -aDeg, 0}, {x, (j - 1) * 0.8f, z}, tile,
                   "Radiating tile of the curved array");
        }
        // back rib (two per column, between the tile rows)
        for (int s = -1; s <= 1; s += 2) {
            const Vec3 back{-std::sin(a) * 0.08f, 0, -std::cos(a) * 0.08f};
            b.part(antenna_, "Antenna rib", P::Cube, {0.66f, 0.08f, 0.08f}, {0, -aDeg, 0}, Vec3{x, s * 0.4f, z} + back, steel,
                   "Back rib following the arc");
        }
    }
    b.part(antenna_, "Antenna spine", P::Cube, {0.14f, 2.6f, 0.14f}, {0, 0, 0}, {0, 0, -0.1f}, steel, "Central vertical spine");
    strut(b, elNode_, "Arm left", P::Cube, {0.45f, 0.0f, 0.0f}, {0.3f, 0.0f, kDishZ0 - 0.1f}, 0.12f, steel, "Dish support arm");
    strut(b, elNode_, "Arm right", P::Cube, {-0.45f, 0.0f, 0.0f}, {-0.3f, 0.0f, kDishZ0 - 0.1f}, 0.12f, steel, "Dish support arm");
    // Feed horn at the focus on three struts.
    const float fz = kDishZ0 + 2.4f;
    b.part(elNode_, "Feed horn", P::Cone, {0.4f, 0.4f, 0.55f}, {0, 0, 0}, {0, 0, fz}, white, "Feed horn (opening faces the array)");
    strut(b, elNode_, "Feed waveguide", P::Cylinder, {0, 0, kDishZ0}, {0, 0, fz - 0.25f}, 0.08f, steel, "Waveguide to the horn");
    strut(b, elNode_, "Feed strut left", P::Cylinder, {1.7f, 0, kDishZ0 + 0.35f}, {0.12f, 0, fz - 0.1f}, 0.06f, steel, "Feed support");
    strut(b, elNode_, "Feed strut right", P::Cylinder, {-1.7f, 0, kDishZ0 + 0.35f}, {-0.12f, 0, fz - 0.1f}, 0.06f, steel, "Feed support");
    strut(b, elNode_, "Feed strut top", P::Cylinder, {0, 1.2f, kDishZ0 + 0.05f}, {0, 0.12f, fz - 0.1f}, 0.06f, steel, "Feed support");
    // Counterweight behind the axis.
    strut(b, elNode_, "Counterweight arm", P::Cylinder, {0, 0, -0.1f}, {0, 0, -1.4f}, 0.12f, steel, "Balance arm");
    b.part(elNode_, "Counterweight", P::Cube, {0.9f, 0.7f, 0.6f}, {0, 0, 0}, {0, 0, -1.7f}, dark, "Counterweight block");
    // IFF bar on top of the array with dipoles.
    b.part(elNode_, "IFF bar", P::Cube, {2.4f, 0.1f, 0.14f}, {0, 0, 0}, {0, 1.62f, kDishZ0 + 0.0f}, white, "Secondary radar (IFF) antenna");
    b.part(elNode_, "IFF post left", P::Cube, {0.06f, 0.3f, 0.06f}, {0, 0, 0}, {0.8f, 1.45f, kDishZ0 - 0.05f}, steel, "IFF post");
    b.part(elNode_, "IFF post right", P::Cube, {0.06f, 0.3f, 0.06f}, {0, 0, 0}, {-0.8f, 1.45f, kDishZ0 - 0.05f}, steel, "IFF post");
    for (int i = 0; i < 6; ++i)
        b.part(elNode_, "IFF dipole", P::Cube, {0.04f, 0.2f, 0.04f}, {0, 0, 0}, {(i - 2.5f) * 0.42f, 1.78f, kDishZ0}, steel,
               "IFF dipole element");
    (void)y1;
    applyPose();
}

const char* Radar::modeName() const {
    switch (mode_) {
        case Mode::Rotate: return "ROTATE";
        case Mode::Sector: return "SECTOR";
        case Mode::Stop: return "STOP";
    }
    return "?";
}

void Radar::debugSetMode(Mode m) {
    mode_ = m;
    if (m == Mode::Sector) { centre_ = azimuth_; phase_ = 0.0f; }
}

Vec3 Radar::position() const { return layout::kRadarSite + Vec3{0, kHeadY + kElevY, 0}; }

void Radar::update(const WorldContext& w, const ControlInput* in) {
    const float dt = clampf(w.dt, 0.0f, 0.1f);
    time_ += dt;
    if (!in) {
        if (mode_ != Mode::Rotate) mode_ = Mode::Rotate;   // idle behaviour: keep scanning
    } else {
        if (in->actionPressed) {
            const Mode next = mode_ == Mode::Rotate ? Mode::Sector : mode_ == Mode::Sector ? Mode::Stop : Mode::Rotate;
            debugSetMode(next);
        }
        if (in->firePressed) transmit_ = !transmit_;
        tilt_ = clampf(tilt_ + in->aimPitch * 20.0f * dt + in->aimPitchDelta * 0.2f, kTiltMin, kTiltMax);
        const float yawIn = in->aimYaw * 45.0f * dt + in->aimYawDelta * 0.3f;
        if (mode_ == Mode::Sector) centre_ = wrap360(centre_ + yawIn);
        if (mode_ == Mode::Stop) azimuth_ = wrap360(azimuth_ + yawIn);
    }
    switch (mode_) {
        case Mode::Rotate:
            omega_ += clampf(kRpm * 6.0f - omega_, -60.0f * dt, 60.0f * dt);
            azimuth_ = wrap360(azimuth_ + omega_ * dt);
            break;
        case Mode::Sector: {
            phase_ += 2.0f * PI * dt / kSectorPeriod;
            if (phase_ > 2.0f * PI) phase_ -= 2.0f * PI;
            const float target = centre_ + kSectorHalfWidth * std::sin(phase_);
            const float d = angDiff(target, azimuth_);
            const float step = clampf(d, -120.0f * dt, 120.0f * dt);
            omega_ = dt > 0.0f ? step / dt : 0.0f;
            azimuth_ = wrap360(azimuth_ + step);
            break;
        }
        case Mode::Stop:
            omega_ += clampf(0.0f - omega_, -120.0f * dt, 120.0f * dt);
            azimuth_ = wrap360(azimuth_ + omega_ * dt);
            break;
    }
    applyPose();
}

void Radar::applyPose() {
    azNode_->local.rotationDeg = {0, -azimuth_, 0};
    elNode_->local.rotationDeg = {-tilt_, 0, 0};
    const float ph = std::fmod(time_, 1.0f);
    const bool on = ph < 0.5f;
    beaconLamp_->material.emissive = on ? 1.0f : 0.0f;
    beaconLamp_->material.lightIntensity = on ? 2.5f : 0.0f;
    txLamp_->material.emissive = transmit_ ? 0.45f : 0.0f;
}

CameraHint Radar::camera(bool firstPerson) const {
    CameraHint h;
    const Vec3 site = layout::kRadarSite;
    if (firstPerson) {
        // Operator's view along the beam, from in front of the array.
        const float yaw = radians(-azimuth_), t = radians(tilt_);
        const Vec3 dir{std::sin(yaw) * std::cos(t), std::sin(t), std::cos(yaw) * std::cos(t)};
        const Vec3 axis = site + Vec3{0, kHeadY + kElevY, 0};
        h.eye = axis + dir * 4.2f;
        h.target = axis + dir * 40.0f;
        h.fovDeg = 60.0f;
    } else {
        h.eye = site + Vec3{-12.0f, 4.8f, -6.0f};
        h.target = site + Vec3{0, 5.2f, 0};
        h.fovDeg = 55.0f;
    }
    return h;
}

std::string Radar::status() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "MODE %s  AZ %03d  TILT %+03d  %s%s", modeName(), static_cast<int>(std::lround(azimuth_)) % 360,
                  static_cast<int>(std::lround(tilt_)), transmit_ ? "TX ON" : "TX OFF",
                  mode_ == Mode::Sector ? "  SECTOR +-45" : "");
    return buf;
}

std::unique_ptr<Controllable> makeRadar() { return std::make_unique<Radar>(); }

}  // namespace ts
