#include "objects/Helicopter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/BaseLayout.h"
#include "objects/Builder.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"

namespace ts {

namespace {

constexpr float kPadThickness = 0.12f;
constexpr float kBladeRadius = 4.2f;        // rotor diameter 8.4 m
constexpr float kHubY = 1.8f;               // rotor head above the body pivot
constexpr float kGravity = 9.81f;

float wrap360(float a) {
    a = std::fmod(a, 360.0f);
    return a < 0.0f ? a + 360.0f : a;
}
float smooth01(float x) {
    x = clampf(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
float approachExp(float cur, float target, float dt, float tau) {
    return cur + (target - cur) * (1.0f - std::exp(-dt / tau));
}

}  // namespace

float Helicopter::groundAt(float x, float z) {
    const float dx = x - layout::kHelipad.x, dz = z - layout::kHelipad.z;
    if (dx * dx + dz * dz <= layout::kHelipadRadius * layout::kHelipadRadius) return layout::kApronLevel + kPadThickness;
    if (x * x + z * z < kTerrainRadius * kTerrainRadius) return terrainHeight(x, z);
    return layout::kApronLevel;
}

Helicopter::Helicopter() {
    using P = PrimitiveType;
    Builder b("Scout Helicopter", "HEL", ShadingModel::Phong);
    root_ = b.root("Scout helicopter", {0, 0, 0}, "MH-6 style light helicopter (user-controllable)");
    root_->animation = "Position and yaw from the flight model";
    SceneNode* r = root_.get();

    const Material paint = withPbr({{0.10f, 0.125f, 0.085f}, 0.30f, 0.0f, 0.45f, 64.0f}, Surface::Camo, 0.42f, 0.1f);
    const Material paintLight = withPbr({{0.14f, 0.16f, 0.12f}, 0.30f, 0.0f, 0.40f, 48.0f}, Surface::Paint, 0.5f, 0.1f);
    const Material glass = withPbr({{0.03f, 0.05f, 0.06f}, 0.10f, 0.0f, 1.0f, 160.0f}, Surface::Glass, 0.03f, 0.1f);
    const Material metal = withPbr({{0.22f, 0.22f, 0.24f}, 0.35f, 0.0f, 0.80f, 96.0f}, Surface::Metal, 0.38f, 0.9f);
    const Material skidMat = withPbr({{0.09f, 0.09f, 0.10f}, 0.25f, 0.0f, 0.5f, 48.0f}, Surface::Metal, 0.5f, 0.8f);
    const Material hot = withPbr({{0.10f, 0.09f, 0.08f}, 0.95f, 0.0f, 0.6f, 48.0f}, Surface::Metal, 0.55f, 0.8f);
    const Material blade = withPbr({{0.06f, 0.06f, 0.07f}, 0.25f, 0.0f, 0.45f, 48.0f}, Surface::Plain, 0.40f, 0.2f);
    const Material tip = withPbr({{0.55f, 0.50f, 0.10f}, 0.25f, 0.0f, 0.35f, 32.0f}, Surface::Paint, 0.50f, 0.0f);
    const Material rubber = withPbr({{0.02f, 0.02f, 0.02f}, 0.25f, 0.0f, 0.1f, 8.0f}, Surface::Rubber, 0.9f, 0.0f);
    Material blur{{0.10f, 0.10f, 0.11f}, 0.25f, 0.0f, 0.2f, 16.0f};
    blur.opacity = 0.0f;
    blur.roughness = 0.5f;
    auto lamp = [](const Vec3& c, float inten, float range) {
        Material m = asLight(Material{c, 0.5f, 0.0f, 0.5f, 64.0f}, inten, range);
        m.surface = Surface::Glass;
        m.roughness = 0.1f;
        return m;
    };
    const Material red = lamp({1.0f, 0.06f, 0.04f}, 0.8f, 3.0f);
    const Material green = lamp({0.05f, 1.0f, 0.2f}, 0.8f, 3.0f);
    const Material white = lamp({1.0f, 1.0f, 0.95f}, 0.6f, 2.5f);
    const Material strobeMat = lamp({1.0f, 1.0f, 1.0f}, 3.0f, 9.0f);
    Material lensMat = asLight(Material{{1.0f, 0.96f, 0.82f}, 0.75f, 0.0f, 1.0f, 128.0f}, 0.0f, 55.0f, 12.0f);
    lensMat.surface = Surface::Glass;
    lensMat.roughness = 0.05f;
    const Material beamMat = withPbr({{1.0f, 0.95f, 0.80f}, 0.0f, 1.0f, 0.0f, 1.0f}, Surface::Plain, 1.0f, 0.0f);

    body_ = b.group(r, "Body", {0, kGroundClearance, 0}, {0, 0, 0}, "Airframe: pitches and rolls on top of the root");
    body_->animation = "Pitch / roll follow acceleration and turn rate (damped spring)";
    SceneNode* bd = body_;

    // Cabin and canopy.
    SceneNode* cabin = b.part(bd, "Cabin", P::Sphere, {1.35f, 1.45f, 2.5f}, {0, 0, 0}, {0, 0.18f, 0}, paint,
                              "Egg-shaped cabin (scaled sphere)");
    cabin->occluder = true;
    b.part(bd, "Canopy glass", P::Sphere, {1.22f, 1.05f, 1.5f}, {0, 0, 0}, {0, 0.36f, 0.62f}, glass,
           "Dark reflective canopy");
    b.part(bd, "Chin nose", P::Sphere, {0.9f, 0.5f, 0.9f}, {0, 0, 0}, {0, -0.22f, 0.8f}, paintLight, "Lower nose fairing");
    b.part(bd, "Door seam left", P::Cube, {0.03f, 0.75f, 0.02f}, {0, 0, 0}, {0.655f, 0.14f, 0.38f}, metal, "Door frame edge");
    b.part(bd, "Door seam right", P::Cube, {0.03f, 0.75f, 0.02f}, {0, 0, 0}, {-0.655f, 0.14f, 0.38f}, metal, "Door frame edge");
    cowling_ = b.part(bd, "Engine cowling", P::Sphere, {0.95f, 0.75f, 1.9f}, {0, 0, 0}, {0, 0.95f, -1.15f}, paintLight,
                      "Turbine engine fairing (warm in thermal)");
    cowling_->occluder = true;
    cowling_->animation = "Heat rises with rotor speed";
    exhaust_ = b.part(bd, "Engine exhaust", P::Cylinder, {0.2f, 0.2f, 0.55f}, {15, 0, 0}, {-0.35f, 1.0f, -2.1f}, hot,
                      "Exhaust pipe: hottest part in the thermal view");
    exhaust_->animation = "Heat rises with rotor speed";
    b.part(bd, "Rotor mast", P::Cylinder, {0.18f, 0.18f, 0.55f}, {90, 0, 0}, {0, 1.55f, 0}, metal, "Main rotor mast");

    // Tail boom (cone with the apex pointing aft), fin, stabiliser, bumper.
    b.part(bd, "Tail boom", P::Cone, {0.45f, 0.45f, 5.2f}, {0, 180, 0}, {0, 0.8f, -3.9f}, paint,
           "Tapered tail boom (cone, apex aft)");
    b.part(bd, "Tail fin", P::Cube, {0.07f, 1.05f, 0.95f}, {0, 0, 0}, {0, 1.2f, -5.0f}, paint, "Vertical fin");
    b.part(bd, "Horizontal stabiliser", P::Cube, {1.6f, 0.05f, 0.4f}, {0, 0, 0}, {0, 0.85f, -4.3f}, paint, "Stabiliser");
    b.part(bd, "Stabiliser plate left", P::Cube, {0.04f, 0.5f, 0.4f}, {0, 0, 0}, {0.8f, 0.85f, -4.3f}, paint, "End plate");
    b.part(bd, "Stabiliser plate right", P::Cube, {0.04f, 0.5f, 0.4f}, {0, 0, 0}, {-0.8f, 0.85f, -4.3f}, paint, "End plate");
    b.part(bd, "Tail bumper", P::Cube, {0.05f, 0.55f, 0.12f}, {0, 0, 0}, {0, 0.5f, -5.05f}, skidMat, "Tail skid");

    // Skids on struts.
    for (int sx = -1; sx <= 1; sx += 2) {
        const float x = 0.95f * sx;
        const char* side = sx > 0 ? "left" : "right";
        b.part(bd, std::string("Skid ") + side, P::Cylinder, {0.1f, 0.1f, 3.0f}, {0, 0, 0}, {x, -0.95f, 0.1f}, skidMat,
               "Landing skid tube");
        b.part(bd, std::string("Skid toe ") + side, P::Cylinder, {0.1f, 0.1f, 0.5f}, {-25, 0, 0}, {x, -0.88f, 1.68f},
               skidMat, "Upturned skid front");
        strut(b, bd, std::string("Front strut ") + side, P::Cylinder, {x, -0.95f, 0.9f}, {0.45f * sx, -0.42f, 0.8f}, 0.07f,
              skidMat, "Skid cross tube");
        strut(b, bd, std::string("Rear strut ") + side, P::Cylinder, {x, -0.95f, -0.8f}, {0.45f * sx, -0.42f, -0.7f}, 0.07f,
              skidMat, "Skid cross tube");
    }

    // Main rotor head: mast top -> (non-spinning) tip-path-plane group -> spinning rotor -> blades.
    SceneNode* head = b.group(bd, "Rotor head", {0, kHubY, 0}, {0, 0, 0}, "Hub position on the mast");
    tpp_ = b.group(head, "Tip-path plane", {0, 0, 0}, {0, 0, 0}, "Non-spinning frame tilted by the cyclic");
    tpp_->animation = "Tilts with cyclic: forward when accelerating, sideways in turns";
    spin_ = b.group(tpp_, "Main rotor", {0, 0, 0}, {0, 0, 0}, "Spinning rotor assembly");
    spin_->animation = "Angle integrates rpm * dt (300 rpm nominal, spools up over 6 s)";
    b.part(spin_, "Rotor hub", P::Cylinder, {0.45f, 0.45f, 0.2f}, {90, 0, 0}, {0, 0.02f, 0}, metal, "Hub plate");
    b.part(spin_, "Rotor hub cap", P::Sphere, {0.3f, 0.22f, 0.3f}, {0, 0, 0}, {0, 0.13f, 0}, metal, "Hub fairing");
    for (int i = 0; i < kBlades; ++i) {
        char nm[48];
        std::snprintf(nm, sizeof(nm), "Blade %d pivot", i + 1);
        pivots_[i] = b.group(spin_, nm, {0, 0, 0}, {0, 72.0f * i, 0}, "Fixed azimuth of the blade (72 deg spacing)");
        std::snprintf(nm, sizeof(nm), "Blade %d flap", i + 1);
        flaps_[i] = b.group(pivots_[i], nm, {0.0f, 0.0f, 0.3f}, {0, 0, 0}, "Flap hinge (coning, cyclic) and feather");
        flaps_[i]->animation = "Flap = coning + cyclic by azimuth; droops at low rpm; feather = collective";
        std::snprintf(nm, sizeof(nm), "Blade %d", i + 1);
        b.part(flaps_[i], nm, P::Cube, {0.22f, 0.03f, 3.9f}, {0, 0, 0}, {0, 0, 1.95f}, blade, "Main rotor blade");
        b.part(flaps_[i], "Blade grip", P::Cube, {0.12f, 0.08f, 0.35f}, {0, 0, 0}, {0, 0, 0.15f}, metal, "Root grip");
        b.part(flaps_[i], "Blade tip cap", P::Cube, {0.23f, 0.035f, 0.3f}, {0, 0, 0}, {0, 0, 3.75f}, tip, "Tracking tip");
    }
    mainBlur_ = b.part(tpp_, "Main rotor blur disc", P::Cylinder, {2 * kBladeRadius, 2 * kBladeRadius, 0.02f}, {90, 0, 0},
                       {0, 0.12f, 0}, blur, "Translucent disc swept by the blades (fades in with rpm)");
    mainBlur_->explodable = false;
    mainBlur_->animation = "Opacity follows rotor speed";

    // Tail rotor on the left of the fin (spins about the lateral axis, 5x faster than the main rotor).
    SceneNode* tailMount = b.group(bd, "Tail rotor mount", {0.2f, 1.3f, -5.1f}, {0, 0, 0}, "Gearbox position on the fin");
    tailSpin_ = b.group(tailMount, "Tail rotor", {0, 0, 0}, {0, 0, 0}, "Spinning tail rotor");
    tailSpin_->animation = "5x main rotor speed";
    b.part(tailSpin_, "Tail rotor hub", P::Cylinder, {0.12f, 0.12f, 0.1f}, {0, 90, 0}, {0, 0, 0}, metal, "Tail hub");
    b.part(tailSpin_, "Tail blade A", P::Cube, {0.03f, 0.13f, 0.55f}, {0, 0, 0}, {0, 0, 0.3f}, blade, "Tail rotor blade 1");
    b.part(tailSpin_, "Tail blade B", P::Cube, {0.03f, 0.13f, 0.55f}, {0, 0, 0}, {0, 0, -0.3f}, blade, "Tail rotor blade 2");
    tailBlur_ = b.part(tailMount, "Tail rotor blur disc", P::Cylinder, {1.2f, 1.2f, 0.01f}, {0, 90, 0}, {0.03f, 0, 0}, blur,
                       "Translucent disc swept by the tail blades");
    tailBlur_->explodable = false;

    // Navigation lights (red left, green right, white tail) and the anti-collision strobe.
    navRed_ = b.part(bd, "Nav light left (red)", P::Sphere, {0.14f, 0.14f, 0.14f}, {0, 0, 0}, {0.66f, -0.1f, 0.55f}, red,
                     "Port navigation light");
    navGreen_ = b.part(bd, "Nav light right (green)", P::Sphere, {0.14f, 0.14f, 0.14f}, {0, 0, 0}, {-0.66f, -0.1f, 0.55f},
                       green, "Starboard navigation light");
    navWhite_ = b.part(bd, "Nav light tail (white)", P::Sphere, {0.12f, 0.12f, 0.12f}, {0, 0, 0}, {0, 0.95f, -5.52f}, white,
                       "Tail position light");
    strobe_ = b.part(bd, "Anti-collision strobe", P::Sphere, {0.16f, 0.12f, 0.16f}, {0, 0, 0}, {0, 1.78f, -5.0f}, strobeMat,
                     "Strobe on top of the fin (practical light)");
    strobe_->animation = "Double flash every 1.2 s while powered";

    // Belly searchlight on a two-axis gimbal (pilot aims it with the arrow keys).
    gimbalYaw_ = b.group(bd, "Searchlight pan", {0, -0.58f, 0.9f}, {0, 0, 0}, "Searchlight azimuth axis");
    gimbalYaw_->animation = "Pan from the pilot (arrow keys / mouse)";
    b.part(gimbalYaw_, "Searchlight yoke", P::Cube, {0.3f, 0.08f, 0.3f}, {0, 0, 0}, {0, 0.05f, 0}, skidMat, "Gimbal base");
    gimbalPitch_ = b.group(gimbalYaw_, "Searchlight tilt", {0, -0.12f, 0}, {55, 0, 0}, "Searchlight elevation axis");
    gimbalPitch_->animation = "Tilt from the pilot";
    b.part(gimbalPitch_, "Searchlight housing", P::Cylinder, {0.26f, 0.26f, 0.3f}, {0, 0, 0}, {0, 0, 0}, metal, "Lamp body");
    lens_ = b.part(gimbalPitch_, "Searchlight lens", P::Sphere, {0.22f, 0.22f, 0.04f}, {0, 0, 0}, {0, 0, 0.16f}, lensMat,
                   "Practical spot light, 12 deg cone");
    b.setShading(ShadingModel::Beam);
    beam_ = b.part(gimbalPitch_, "Searchlight cone", P::BeamCone, {1, 1, 1}, {0, 0, 0}, {0, 0, 1}, beamMat,
                   "Visible beam (length follows the distance to the ground)");
    beam_->explodable = false;
    beam_->animation = "Length follows distance to the ground; hidden when the light is off";
    b.setShading(ShadingModel::Phong);
    (void)rubber;

    // Detail: swashplate + pitch links, side benches, antennas, wire cutter, tail gearbox.
    b.part(bd, "Swashplate", P::Cylinder, {0.5f, 0.5f, 0.08f}, {90, 0, 0}, {0, 1.42f, 0}, metal, "Non-rotating swashplate (cyclic input)");
    for (int i = 0; i < kBlades; ++i) {
        const float a = radians(72.0f * i + 20.0f);
        strut(b, bd, "Pitch link", P::Cylinder, {0.22f * std::sin(a), 1.42f, 0.22f * std::cos(a)},
              {0.3f * std::sin(a), kHubY + 0.1f, 0.3f * std::cos(a)}, 0.03f, metal, "Pitch link from swashplate to blade grip");
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        const char* side = sx > 0 ? "left" : "right";
        b.part(bd, std::string("Bench seat ") + side, P::Cube, {0.35f, 0.05f, 1.3f}, {0, 0, 0}, {0.98f * sx, -0.25f, 0.05f}, paintLight, "External troop bench");
        b.part(bd, std::string("Bench back ") + side, P::Cube, {0.05f, 0.45f, 1.3f}, {0, 0, 0}, {0.8f * sx, 0.0f, 0.05f}, paintLight, "Bench backrest");
        strut(b, bd, std::string("Bench strut ") + side, P::Cylinder, {0.95f * sx, -0.95f, 0.05f}, {0.95f * sx, -0.28f, 0.05f}, 0.05f, skidMat, "Bench support");
    }
    strut(b, bd, "Wire cutter", P::Cylinder, {0, 0.35f, 1.3f}, {0, 1.1f, 0.9f}, 0.04f, metal, "Wire strike protection blade");
    strut(b, bd, "Whip antenna", P::Cylinder, {0.25f, 0.8f, -2.4f}, {0.35f, 1.9f, -2.9f}, 0.02f, metal, "Radio whip antenna");
    b.part(bd, "Tail rotor gearbox", P::Cylinder, {0.2f, 0.2f, 0.3f}, {0, 90, 0}, {0.1f, 1.3f, -5.1f}, metal, "Tail rotor gearbox");
    b.part(bd, "Pitot tube", P::Cylinder, {0.03f, 0.03f, 0.4f}, {0, 0, 0}, {-0.5f, 0.1f, 1.45f}, metal, "Airspeed probe");

    // Parked on the helipad, nose toward the compound.
    pos_ = Vec3{layout::kHelipad.x, layout::kApronLevel + kPadThickness, layout::kHelipad.z};
    yawDeg_ = degrees(std::atan2(-layout::kHelipad.x, -layout::kHelipad.z));
    applyPose(0.0f);
}

const char* Helicopter::stateName() const {
    switch (state_) {
        case State::Parked: return "PARKED";
        case State::Startup: return "STARTUP";
        case State::Hover: return altitudeAgl() > 0.3f ? "HOVER" : "READY";
        case State::Fly: return "FLY";
        case State::Landing: return "LANDING";
    }
    return "?";
}

float Helicopter::bladeAzimuthDeg(int i) const {
    return wrap360(spin_->local.rotationDeg.y + pivots_[i]->local.rotationDeg.y);
}

void Helicopter::debugPlace(const Vec3& skidPos, float yaw, State s, float rotorFrac) {
    pos_ = skidPos;
    yawDeg_ = yaw;
    state_ = s;
    rotorFrac_ = rotorFrac;
    airborne_ = pos_.y - groundAt(pos_.x, pos_.z) > kMinAgl - 0.01f;
    vel_ = {0, 0, 0};
    applyPose(0.0f);
}

bool Helicopter::landingAllowed(const char** why) const {
    if (groundSpeed() > kLandMaxSpeed) { *why = "TOO FAST"; return false; }
    if (altitudeAgl() > kLandMaxAgl) { *why = "TOO HIGH"; return false; }
    // Flatness: sample the ground under the skid footprint.
    const float yaw = radians(yawDeg_);
    const Vec3 f{std::sin(yaw), 0, std::cos(yaw)}, l{std::cos(yaw), 0, -std::sin(yaw)};
    float lo = 1e9f, hi = -1e9f;
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            const Vec3 p = pos_ + f * (1.5f * i) + l * (0.95f * j);
            const float h = groundAt(p.x, p.z);
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    }
    if ((hi - lo) / 3.0f > kLandMaxSlope) { *why = "SLOPE"; return false; }
    return true;
}

void Helicopter::update(const WorldContext& w, const ControlInput* in) {
    const float dt = clampf(w.dt, 0.0f, 0.1f);
    time_ += dt;
    msgTimer_ = std::max(0.0f, msgTimer_ - dt);
    const bool possessed = in != nullptr;
    ControlInput none;
    const ControlInput& c = possessed ? *in : none;

    // --- state machine ---
    if (possessed && c.actionPressed) {
        switch (state_) {
            case State::Parked: state_ = State::Startup; break;
            case State::Startup: state_ = State::Parked; break;  // abort: rotor spools down again
            case State::Hover:
            case State::Fly: {
                const char* why = "";
                if (altitudeAgl() < 0.15f) {
                    state_ = State::Parked;  // on the ground: shut down
                } else if (landingAllowed(&why)) {
                    state_ = State::Landing;
                } else {
                    ++refusals_;
                    msg_ = why;
                    msgTimer_ = 2.5f;
                }
                break;
            }
            case State::Landing: state_ = State::Hover; break;  // go around
        }
    }
    if (!possessed && state_ == State::Hover && altitudeAgl() < 0.15f) state_ = State::Parked;

    // --- rotor speed ---
    if (state_ == State::Startup) {
        rotorFrac_ = std::min(1.0f, rotorFrac_ + dt / kSpoolUpSeconds);
        if (rotorFrac_ >= 1.0f) state_ = State::Hover;
    } else if (state_ == State::Parked) {
        rotorFrac_ = std::max(0.0f, rotorFrac_ - dt / kSpoolDownSeconds);
    } else {
        rotorFrac_ = 1.0f;
    }
    rotorTotalDeg_ += static_cast<double>(kRotorRpm) * 6.0 * rotorFrac_ * dt;
    tailDeg_ = static_cast<float>(std::fmod(static_cast<double>(tailDeg_) + kRotorRpm * kTailRatio * 6.0 * rotorFrac_ * dt, 360.0));

    // --- flight model ---
    const bool flying = state_ == State::Hover || state_ == State::Fly || state_ == State::Landing;
    const Vec3 oldVel = vel_;
    float turnCmd = 0.0f;
    if (flying && dt > 0.0f) {
        const float yaw = radians(yawDeg_);
        const Vec3 f{std::sin(yaw), 0, std::cos(yaw)};
        const float ground = groundAt(pos_.x, pos_.z);
        float agl = pos_.y - ground;
        const bool landing = state_ == State::Landing;
        const bool pilot = possessed && !landing;
        const float gate = smooth01((agl - 0.1f) / 0.5f);  // no horizontal authority sitting on the skids

        const float maxSpeed = c.boost ? kBoostSpeed : kMaxSpeed;
        const float fwdCmd = pilot ? clampf(c.forward, -1.0f, 1.0f) : 0.0f;
        const float wantSpeed = (fwdCmd >= 0.0f ? fwdCmd * maxSpeed : fwdCmd * 7.0f) * gate;
        Vec3 wantH = f * wantSpeed;
        Vec3 velH{vel_.x, 0, vel_.z};
        Vec3 acc = (wantH - velH) / 1.2f;
        const float accLimit = c.boost ? 12.0f : 8.0f;
        const float am = length(acc);
        if (am > accLimit) acc = acc * (accLimit / am);
        velH = velH + acc * dt;
        vel_.x = velH.x;
        vel_.z = velH.z;

        // yaw
        turnCmd = pilot ? clampf(c.turn, -1.0f, 1.0f) : 0.0f;
        const float wantYawRate = -turnCmd * 55.0f * gate;
        yawRate_ = approachExp(yawRate_, wantYawRate, dt, 0.35f);
        yawDeg_ += yawRate_ * dt;

        // vertical
        float wantVy = 0.0f;
        if (landing) {
            wantVy = -clampf(0.35f + agl * 0.45f, 0.35f, 2.0f);
        } else if (pilot) {
            wantVy = clampf(c.lift, -1.0f, 1.0f) * kClimbRate;
        }
        vel_.y = approachExp(vel_.y, wantVy, dt, 0.6f);
        pos_ = pos_ + vel_ * dt;

        // radius limit (the world's 60 m disc)
        const float rr = std::sqrt(pos_.x * pos_.x + pos_.z * pos_.z);
        if (rr > kMaxRadius) {
            const float nx = pos_.x / rr, nz = pos_.z / rr;
            pos_.x = nx * kMaxRadius;
            pos_.z = nz * kMaxRadius;
            const float out = vel_.x * nx + vel_.z * nz;
            if (out > 0.0f) { vel_.x -= out * nx; vel_.z -= out * nz; }
        }
        // static obstacles: do not fly into solid parts that rise above the skids (rotor + fuselage footprint)
        if (w.obstacles && agl > 0.5f) {
            const float margin = 2.2f;
            auto inside = [&](const Vec3& p, const Box& bx) {
                return p.x > bx.min.x - margin && p.x < bx.max.x + margin && p.z > bx.min.z - margin &&
                       p.z < bx.max.z + margin && p.y < bx.max.y + 0.3f && p.y + 2.5f > bx.min.y;
            };
            for (const Box& bx : *w.obstacles) {
                if (bx.max.y - ground < 0.6f) continue;           // flat things (pad, road) do not block
                if (bx.max.x - bx.min.x > 40.0f || bx.max.z - bx.min.z > 40.0f) continue;  // ground-like slabs
                if (inside(pos_, bx) && !inside(pos_ - Vec3{velH.x, 0, velH.z} * dt, bx)) {
                    pos_.x -= velH.x * dt;
                    pos_.z -= velH.z * dt;
                    vel_.x = vel_.z = 0.0f;
                }
            }
        }
        // altitude limits and ground contact
        const float g2 = groundAt(pos_.x, pos_.z);
        agl = pos_.y - g2;
        if (agl >= kMinAgl) airborne_ = true;
        if (landing) {
            if (agl <= 0.02f) {
                pos_.y = g2;
                vel_ = {0, 0, 0};
                airborne_ = false;
                state_ = State::Parked;  // spools down
            }
        } else {
            const float minAgl = airborne_ ? kMinAgl : 0.0f;
            if (agl < minAgl) { pos_.y = g2 + minAgl; if (vel_.y < 0) vel_.y = 0; }
            if (agl > kMaxAgl) { pos_.y = g2 + kMaxAgl; if (vel_.y > 0) vel_.y = 0; }
            if (!airborne_ && agl <= 0.0f) { pos_.y = g2; if (vel_.y < 0) vel_.y = 0; }
        }
        if (state_ == State::Hover || state_ == State::Fly)
            state_ = groundSpeed() > (state_ == State::Fly ? 1.0f : 2.0f) ? State::Fly : State::Hover;
    } else {
        // on the ground (parked / start-up): everything at rest
        vel_ = {0, 0, 0};
        yawRate_ = 0.0f;
        pos_.y = groundAt(pos_.x, pos_.z);
        airborne_ = false;
    }

    // --- attitude: damped spring toward the target given by acceleration and turn rate ---
    if (dt > 0.0f) {
        const float yaw = radians(yawDeg_);
        const Vec3 f{std::sin(yaw), 0, std::cos(yaw)}, l{std::cos(yaw), 0, -std::sin(yaw)};
        const Vec3 a = (vel_ - oldVel) / dt;
        accFwdS_ = approachExp(accFwdS_, dot(a, f), dt, 0.15f);
        accLeftS_ = approachExp(accLeftS_, dot(a, l), dt, 0.15f);
        turnRateS_ = approachExp(turnRateS_, -yawRate_, dt, 0.2f);  // positive = turning right
        const float vf = dot(vel_, f);
        float pitchT = 0.0f, rollT = 0.0f;
        if (airborne_ && flying) {
            pitchT = clampf(accFwdS_ * 1.7f + vf * 0.25f, -12.0f, 22.0f);
            rollT = clampf(0.5f * degrees(std::atan(vf * radians(turnRateS_) / kGravity)) - accLeftS_ * 1.2f, -25.0f, 25.0f);
            pitchT += 0.5f * std::sin(time_ * 1.7f);   // gentle hover wobble
            rollT += 0.4f * std::sin(time_ * 1.3f + 1.0f);
        }
        const float omega = 5.0f, zeta = 0.8f;
        pitchVel_ += (omega * omega * (pitchT - pitchDeg_) - 2.0f * zeta * omega * pitchVel_) * dt;
        pitchDeg_ += pitchVel_ * dt;
        rollVel_ += (omega * omega * (rollT - rollDeg_) - 2.0f * zeta * omega * rollVel_) * dt;
        rollDeg_ += rollVel_ * dt;
        // cyclic: the tip-path plane tilts further than the fuselage in the direction of the acceleration
        const float tf = clampf(accFwdS_ * 0.35f + 0.1f * vf, -6.0f, 6.0f) * rotorFrac_;
        const float tl = clampf(-0.35f * rollDeg_ + accLeftS_ * 0.3f, -6.0f, 6.0f) * rotorFrac_;
        tiltFwd_ = approachExp(tiltFwd_, tf, dt, 0.2f);
        tiltLeft_ = approachExp(tiltLeft_, tl, dt, 0.2f);
    }

    // --- searchlight pilot controls ---
    if (possessed) {
        lightPan_ = clampf(lightPan_ - (c.aimYaw * 70.0f * dt + c.aimYawDelta * 0.3f), -150.0f, 150.0f);
        lightDown_ = clampf(lightDown_ - (c.aimPitch * 50.0f * dt + c.aimPitchDelta * 0.3f), -10.0f, 90.0f);
        if (c.firePressed) lightOn_ = !lightOn_;
    }
    if (rotorFrac_ <= 0.0f && state_ == State::Parked) lightOn_ = false;

    // --- rotor wash dust ---
    if (w.combat && flying && rotorFrac_ > 0.6f) {
        const float agl = altitudeAgl();
        if (agl < 4.0f) {
            dustAcc_ += (1.0f - agl / 4.0f) * 90.0f * dt;
            const int n = static_cast<int>(dustAcc_);
            if (n > 0) {
                dustAcc_ -= static_cast<float>(n);
                w.combat->emitDust({pos_.x, groundAt(pos_.x, pos_.z) + 0.1f, pos_.z}, 3.0f + 2.5f * (1.0f - agl / 4.0f), n);
            }
        } else {
            dustAcc_ = 0.0f;
        }
    }

    applyPose(dt);
}

void Helicopter::applyPose(float dt) {
    (void)dt;
    root_->local.translation = pos_;
    root_->local.rotationDeg = {0, yawDeg_, 0};
    body_->local.rotationDeg = {pitchDeg_, 0, rollDeg_};

    // Rotor: spin, coning, cyclic flapping, collective feather.
    const float rotAng = static_cast<float>(std::fmod(rotorTotalDeg_, 360.0));
    spin_->local.rotationDeg = {0, rotAng, 0};
    tpp_->local.rotationDeg = {tiltFwd_, 0, -tiltLeft_};
    const float cone = lerpf(-7.0f, 3.5f, smooth01((rotorFrac_ - 0.15f) / 0.6f));
    const float collective = 3.0f + 5.0f * smooth01(rotorFrac_ - 0.3f) + 1.5f * clampf(vel_.y / kClimbRate, -1.0f, 1.0f);
    for (int i = 0; i < kBlades; ++i) {
        const float psi = radians(rotAng + 72.0f * i);   // blade azimuth, 0 = fuselage nose, toward +X
        // The disc is already tilted as a whole; the blade's own flapping is only the lag/flutter relative to it,
        // plus a visible cyclic component because the blades rise and fall once per revolution.
        float beta = cone - 0.5f * tiltFwd_ * std::cos(psi) - 0.5f * tiltLeft_ * std::sin(psi);
        beta += 0.35f * std::sin(time_ * 9.0f + 2.1f * i) * smooth01(rotorFrac_ * 2.0f - 0.8f);
        const float feather = collective + 0.6f * (tiltFwd_ * std::sin(psi) - tiltLeft_ * std::cos(psi));
        flaps_[i]->local.rotationDeg = {-beta, 0, feather};
    }
    tailSpin_->local.rotationDeg = {tailDeg_, 0, 0};

    // Blur discs fade in with rotor speed (scaled to a point when invisible).
    const float op = 0.26f * smooth01((rotorFrac_ - 0.3f) / 0.6f);
    auto setBlur = [&](SceneNode* n, float opacity, float diameter) {
        n->material.opacity = opacity;
        const float d = opacity > 0.004f ? diameter : 0.001f;
        n->local.scale = {d, d, opacity > 0.004f ? n->local.scale.z : 0.001f};
        if (opacity > 0.004f) n->local.scale.z = diameter > 2.0f ? 0.02f : 0.01f;
    };
    setBlur(mainBlur_, op, 2.0f * kBladeRadius);
    setBlur(tailBlur_, op * 0.9f, 1.2f);

    // Engine heat grows with rotor speed (thermal view).
    cowling_->material.heat = lerpf(0.30f, 0.62f, rotorFrac_);
    exhaust_->material.heat = lerpf(0.40f, 0.98f, rotorFrac_);

    // Lights.
    const bool powered = rotorFrac_ > 0.02f || state_ != State::Parked;
    auto setLamp = [](SceneNode* n, bool on, float inten) {
        n->material.emissive = on ? 1.0f : 0.0f;
        n->material.lightIntensity = on ? inten : 0.0f;
    };
    setLamp(navRed_, powered, 0.8f);
    setLamp(navGreen_, powered, 0.8f);
    setLamp(navWhite_, powered, 0.6f);
    const float ph = std::fmod(time_, 1.2f);
    const bool flash = powered && ((ph < 0.08f) || (ph > 0.2f && ph < 0.28f));
    setLamp(strobe_, flash, 3.0f);

    // Searchlight gimbal, lens, visible beam (length = distance to the ground along the axis).
    gimbalYaw_->local.rotationDeg = {0, lightPan_, 0};
    gimbalPitch_->local.rotationDeg = {lightDown_, 0, 0};
    lens_->material.emissive = lightOn_ ? 1.0f : 0.0f;
    lens_->material.lightIntensity = lightOn_ ? 5.0f : 0.0f;
    if (lightOn_) {
        const Mat4 m = translate(pos_) * rotateEulerYXZ({0, yawDeg_, 0}) * translate({0, kGroundClearance, 0}) *
                       rotateEulerYXZ({pitchDeg_, 0, rollDeg_}) * translate(gimbalYaw_->local.translation) *
                       rotateEulerYXZ(gimbalYaw_->local.rotationDeg) * translate(gimbalPitch_->local.translation) *
                       rotateEulerYXZ(gimbalPitch_->local.rotationDeg);
        const Vec3 o = transformPoint(m, {0, 0, 0.16f});
        const Vec3 d = normalize(transformDirection(m, {0, 0, 1}));
        float len = 60.0f;
        for (float t = 0.2f; t < 60.0f; t += 0.4f) {
            const Vec3 q = o + d * t;
            if (q.y <= groundAt(q.x, q.z)) { len = t; break; }
        }
        const float width = 2.0f * len * std::tan(radians(12.0f));
        beam_->local.translation = {0, 0, 0.16f + 0.5f * len};
        beam_->local.scale = {width, width, len};
    } else {
        beam_->local.translation = {0, 0, 0.2f};
        beam_->local.scale = {0.001f, 0.001f, 0.001f};
    }
}

std::vector<Box> Helicopter::dynamicObstacles() const {
    if (altitudeAgl() > 0.3f) return {};
    const float yaw = radians(yawDeg_);
    const float s = std::sin(yaw), c = std::cos(yaw);
    auto box = [&](float x0, float x1, float z0, float z1, float y0, float y1) {
        Box bx{{1e9f, y0, 1e9f}, {-1e9f, y1, -1e9f}};
        const float xs[2] = {x0, x1}, zs[2] = {z0, z1};
        for (float x : xs)
            for (float z : zs) {
                const float wx = pos_.x + x * c + z * s, wz = pos_.z - x * s + z * c;
                bx.min.x = std::min(bx.min.x, wx); bx.max.x = std::max(bx.max.x, wx);
                bx.min.z = std::min(bx.min.z, wz); bx.max.z = std::max(bx.max.z, wz);
            }
        return bx;
    };
    const float g = pos_.y;
    return {box(-0.95f, 0.95f, -1.4f, 1.5f, g, g + 2.0f),    // cabin + skids
            box(-0.3f, 0.3f, -5.4f, -1.4f, g + 0.9f, g + 2.4f)};  // tail boom + fin
}

CameraHint Helicopter::camera(bool firstPerson) const {
    CameraHint h;
    const float yaw = radians(yawDeg_);
    const Vec3 f{std::sin(yaw), 0, std::cos(yaw)};
    const Vec3 c = pos_ + Vec3{0, kGroundClearance, 0};
    if (firstPerson) {
        const Mat4 m = translate(pos_) * rotateEulerYXZ({0, yawDeg_, 0}) * translate({0, kGroundClearance, 0}) *
                       rotateEulerYXZ({pitchDeg_, 0, rollDeg_});
        h.eye = transformPoint(m, {-0.3f, 0.5f, 0.55f});
        h.target = transformPoint(m, {-0.3f, 0.5f - 0.12f, 6.0f});
        h.fovDeg = 75.0f;
        h.sight = true;
    } else {
        h.eye = c - f * 14.0f + Vec3{0, 4.5f, 0};
        h.target = c + f * 5.0f + Vec3{0, 0.5f, 0};
        h.fovDeg = 55.0f;
    }
    return h;
}

std::string Helicopter::status() const {
    const float kt = length(vel_) * 1.9438f;
    const int hdg = static_cast<int>(std::lround(wrap360(-yawDeg_))) % 360;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "ALT %.1f M  IAS %d KT  HDG %03d  ROTOR %d%%  STATE %s%s%s%s", std::max(0.0f, altitudeAgl()),
                  static_cast<int>(std::lround(kt)), hdg, static_cast<int>(std::lround(rotorFrac_ * 100.0f)), stateName(),
                  lightOn_ ? "  LIGHT" : "", msgTimer_ > 0.0f ? "  LAND REFUSED: " : "", msgTimer_ > 0.0f ? msg_ : "");
    return buf;
}

std::unique_ptr<Controllable> makeHelicopter() { return std::make_unique<Helicopter>(); }

}  // namespace ts
