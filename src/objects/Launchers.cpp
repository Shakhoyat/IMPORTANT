#include "objects/Launchers.h"
#include "primitives/Library.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/BaseLayout.h"
#include "objects/Builder.h"
#include "objects/PropKit.h"
#include "objects/Structures.h"
#include "objects/Tank.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"

namespace ts {

namespace camp {

PlanRect planRect(const Site& s) {
    const float rear = s.halfD + 0.6f, fr = std::max(s.halfD + 1.45f, s.front);
    const float cz = 0.5f * (fr - rear), hz = 0.5f * (fr + rear);
    const float y = radians(s.yawDeg);
    return {s.x + std::sin(y) * cz, s.z + std::cos(y) * cz, s.halfW + 1.45f, hz, s.yawDeg};
}

PlanRect poolPlanRect() {
    const float rear = kPoolHalfD + 0.6f, fr = std::max(kPoolHalfD + 1.45f, kPoolFront);
    const float cz = 0.5f * (fr - rear), hz = 0.5f * (fr + rear);
    const float y = radians(layout::kTankParkYawDeg);
    return {layout::kMotorPool.x + std::sin(y) * cz, layout::kMotorPool.z + std::cos(y) * cz, kPoolHalfW + 1.45f, hz,
            layout::kTankParkYawDeg};
}

std::array<Site, 5> allSites() { return {kSamSite, kMlrsSite, kTankSouth, kTankWest, kAmmoSite}; }

}  // namespace camp

namespace {

using P = PrimitiveType;

// ---- materials ----------------------------------------------------------------------------------------------
Material olivePaint() { return makeMaterial({0.145f, 0.175f, 0.105f}, 0.30f, Surface::Paint, 0.60f, 0.25f); }
Material camoPaint() { return makeMaterial({0.165f, 0.195f, 0.115f}, 0.28f, Surface::Camo, 0.75f, 0.1f); }
Material steel() { return mats::towerSteel(); }
Material rubber() { return makeMaterial({0.025f, 0.025f, 0.025f}, 0.30f, Surface::Rubber, 0.85f, 0.0f); }
Material darkGlass() { return makeMaterial({0.03f, 0.04f, 0.05f}, 0.20f, Surface::Glass, 0.10f, 0.0f); }
Material hazardBand() { return makeMaterial({0.30f, 0.24f, 0.04f}, 0.26f, Surface::Hazard, 0.55f, 0.0f); }
Material plate() { return makeMaterial({0.16f, 0.17f, 0.16f}, 0.30f, Surface::SteelPlate, 0.50f, 0.6f); }
Material earthMat() { return makeMaterial({0.13f, 0.105f, 0.075f}, 0.15f, Surface::Ground, 0.95f, 0.0f); }
Material tacRed() { return makeMaterial({1.0f, 0.05f, 0.03f}, 0.35f, Surface::Glass, 0.2f, 0.0f, 0.55f); }
Material tacTealLamp() {
    return asLight(makeMaterial({0.05f, 0.85f, 0.60f}, 0.35f, Surface::Glass, 0.2f, 0.0f, 1.0f), 0.6f, 4.0f, 60.0f);
}
Material flameMat() { return makeMaterial({1.0f, 0.55f, 0.15f}, 0.75f, Surface::Plain, 0.6f, 0.0f, 2.2f); }

Vec3 rotY(const Vec3& p, float a) {
    return {p.x * std::cos(a) + p.z * std::sin(a), p.y, -p.x * std::sin(a) + p.z * std::cos(a)};
}
// Rotation that raises the +Z axis by 'el' radians (about X).
Vec3 rotRaise(const Vec3& p, float el) {
    return {p.x, p.y * std::cos(el) + p.z * std::sin(el), -p.y * std::sin(el) + p.z * std::cos(el)};
}

void setVisible(SceneNode& n, bool v) {
    n.visible = v;
    for (auto& c : n.children) setVisible(*c, v);
}

// Trapezoid velocity profile: accelerates at 'acc', cruises at 'vmax', brakes so that it stops on the target.
void slewStep(float& x, float& v, float target, float vmax, float acc, float dt) {
    const float d = target - x;
    const float want = (d >= 0.0f ? 1.0f : -1.0f) * std::min(vmax, std::sqrt(2.0f * acc * std::fabs(d)));
    v += clampf(want - v, -acc * dt, acc * dt);
    x += v * dt;
    if ((target - x) * d <= 0.0f || (std::fabs(target - x) < 1e-3f && std::fabs(v) < acc * dt)) {
        x = target;
        v = 0.0f;
    }
}

void readAim(const ControlInput& in, float dt, float azRate, float elRate, float azLim, float elMin, float elMax, float& azCmd,
             float& elCmd) {
    azCmd = clampf(azCmd - (in.aimYaw * azRate * dt + in.aimYawDelta), -azLim, azLim);  // right arrow = clockwise
    elCmd = clampf(elCmd + in.aimPitch * elRate * dt + in.aimPitchDelta, elMin, elMax);
}

// Re-aims a cylinder part between two points of its parent's space.
void poseStrut(SceneNode* n, const Vec3& a, const Vec3& c, float thickness) {
    const Vec3 d = c - a;
    n->local.translation = (a + c) * 0.5f;
    n->local.rotationDeg = alignZ(d);
    n->local.scale = {thickness, thickness, std::max(0.02f, length(d))};
}

// Wheel: tyre + hub; axis along X.
void wheel(Builder& b, SceneNode* p, const Vec3& pos, float dia, float width) {
    b.part(p, "Wheel", P::Cylinder, {dia, dia, width}, {0, 90, 0}, pos, rubber(), "Tyre");
    b.part(p, "Wheel hub", P::Cylinder, {0.45f * dia, 0.45f * dia, width + 0.04f}, {0, 90, 0}, pos, steel(), "Wheel hub cap");
}

// v7: two raised tread bands on a tyre (slightly larger diameter than the tyre: lugs in the silhouette).
void treads(Builder& b, SceneNode* p, const Vec3& pos, float dia, float width) {
    for (float u : {-0.28f, 0.28f})
        detail(b, p, "Tyre tread", P::Torus, lib::torusScale(1.03f * dia, 0.2f * width), {0, 90, 0}, pos + Vec3{u * width, 0, 0},
               shade(rubber(), 0.8f), "Raised tread band of the off-road tyre");
}

// A show-launch round: tail at the node origin, nose along +Z. Returns the flame part through 'flame'.
SceneNode* roundGroup(Builder& b, SceneNode* parent, const char* name, float dia, float len, SceneNode** flame) {
    SceneNode* g = b.group(parent, name, {0, 0, 0}, {0, 0, 0}, "Flying round of the launch effect (hidden while in the canister)");
    g->animation = "Slides out of its canister with speed 8 + 60 t m/s, leaves a smoke trail, disappears after the flight";
    b.part(g, "Round body", P::Cylinder, {dia, dia, len}, {0, 0, 0}, {0, 0, 0.5f * len}, makeMaterial({0.30f, 0.31f, 0.27f}, 0.35f, Surface::Paint, 0.5f, 0.3f), "Missile body");
    b.part(g, "Round nose", P::Cone, {dia, dia, 0.28f * len}, {0, 0, 0}, {0, 0, len + 0.14f * len}, makeMaterial({0.12f, 0.13f, 0.11f}, 0.35f, Surface::Paint, 0.5f, 0.2f), "Radome cone");
    b.part(g, "Round fins A", P::Cube, {3.0f * dia, 0.02f, 0.12f * len}, {0, 0, 0}, {0, 0, 0.12f * len}, steel(), "Pair of tail fins");
    b.part(g, "Round fins B", P::Cube, {0.02f, 3.0f * dia, 0.12f * len}, {0, 0, 0}, {0, 0, 0.12f * len}, steel(), "Second pair of tail fins");
    SceneNode* f = b.part(g, "Round flame", P::Sphere, {1.3f * dia, 1.3f * dia, 1.2f * len * 0.5f}, {0, 0, 0}, {0, 0, -0.25f * len}, flameMat(), "Motor exhaust flame (emissive)");
    f->animation = "Flickers while the motor burns";
    f->obstacle = false;
    if (flame) *flame = f;
    setVisible(*g, false);
    for (auto& c : g->children) c->obstacle = false;
    return g;
}

Box rotatedFootprint(const Vec3& centre, float yaw, float hw, float hl, float height) {
    const float s = std::fabs(std::sin(yaw)), c = std::fabs(std::cos(yaw));
    const float hx = c * hw + s * hl, hz = s * hw + c * hl;
    return Box{{centre.x - hx, centre.y, centre.z - hz}, {centre.x + hx, centre.y + height, centre.z + hz}};
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------
// SAM launcher
// ---------------------------------------------------------------------------------------------------------
namespace {
constexpr Vec3 kSamTurn{0.0f, 1.14f, 0.4f};        // turntable origin (root space)
constexpr Vec3 kSamCradle{0.0f, 1.6f, 0.0f};       // cradle pivot (turntable space)
constexpr float kCanLen = 3.4f, kCanCentreZ = 0.7f, kCanFrontZ = 2.4f, kCanRearZ = -1.0f;
const Vec3 kCanOffset[4] = {{-0.28f, 0.26f, 0}, {0.28f, 0.26f, 0}, {-0.28f, -0.26f, 0}, {0.28f, -0.26f, 0}};
const Vec3 kSamRamBase{0.0f, 0.38f, -1.3f};        // turntable space
const Vec3 kSamRamLug{0.0f, -0.6f, -0.7f};         // cradle space
}  // namespace

void SamLauncher::Slew::step(float target, float vmax, float acc, float dt) { slewStep(x, v, target, vmax, acc, dt); }

SamLauncher::SamLauncher() {
    build();
    place(camp::kSamSite.x, camp::kSamSite.z, camp::kSamSite.yawDeg);
    poseParts();
}

void SamLauncher::place(float x, float z, float yawDeg) {
    yaw_ = radians(yawDeg);
    root_->local.translation = {x, terrainHeight(x, z), z};
    root_->local.rotationDeg = {0, yawDeg, 0};
}

void SamLauncher::build() {
    Builder b("SAM launcher", "SAM", ShadingModel::Phong);
    root_ = b.root("SAM launcher", {0, 0, 0}, "Trailer-mounted surface-to-air missile launcher with four canisters (user controlled)");
    SceneNode* r = root_.get();
    const Material od = olivePaint(), camo = camoPaint(), st = steel();

    // ---- trailer (static) ----
    SceneNode* tr = b.group(r, "Trailer", {0, 0, 0}, {0, 0, 0}, "Two-axle launcher trailer; tow bar towards -Z (the open end of the revetment)");
    b.part(tr, "Trailer frame", P::Cube, {1.3f, 0.28f, 6.8f}, {0, 0, 0}, {0, 0.86f, 0}, st, "Central beam of the trailer");
    for (float s : {-1.0f, 1.0f})
        b.part(tr, "Trailer side rail", P::Cube, {0.12f, 0.30f, 6.8f}, {0, 0, 0}, {s * 1.1f, 0.9f, 0}, st, "Outer rail");
    b.part(tr, "Trailer deck", P::Cube, {2.6f, 0.10f, 6.2f}, {0, 0, 0}, {0, 1.09f, 0}, od, "Deck plate under the turntable");
    for (float z : {-1.7f, -2.9f})
        for (float s : {-1.0f, 1.0f}) wheel(b, tr, {s * 1.25f, 0.5f, z}, 1.0f, 0.4f);
    for (float s : {-1.0f, 1.0f})
        b.part(tr, "Fender", P::Cube, {0.5f, 0.08f, 2.1f}, {0, 0, 0}, {s * 1.25f, 1.05f, -2.3f}, od, "Mudguard over the tandem wheels");
    strut(b, tr, "Tow bar", P::Cylinder, {0, 0.9f, -3.4f}, {0, 0.65f, -5.3f}, 0.12f, st, "Drawbar towards the rear");
    b.part(tr, "Tow eye", P::Sphere, {0.22f, 0.22f, 0.22f}, {0, 0, 0}, {0, 0.65f, -5.35f}, st, "Lunette eye");
    strut(b, tr, "Landing leg", P::Cylinder, {0, 0.9f, -3.2f}, {0, 0.05f, -3.2f}, 0.1f, st, "Parking jack of the drawbar");
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f}) {
            strut(b, tr, "Outrigger", P::Cylinder, {sx * 1.3f, 0.9f, sz * 2.6f}, {sx * 1.8f, 0.06f, sz * 3.0f}, 0.1f, st, "Stabiliser jack");
            b.part(tr, "Outrigger pad", P::Cube, {0.4f, 0.06f, 0.4f}, {0, 0, 0}, {sx * 1.8f, 0.03f, sz * 3.0f}, st, "Ground pad");
        }
    // Control cabin at the front of the deck.
    b.part(tr, "Control cabin", P::Cube, {1.8f, 0.9f, 1.4f}, {0, 0, 0}, {0, 1.59f, 2.5f}, od, "Fire-control and generator cabin");
    b.part(tr, "Cabin door", P::Cube, {0.6f, 0.7f, 0.04f}, {0, 0, 0}, {0.4f, 1.55f, 3.21f}, st, "Access door");
    for (int i = 0; i < 3; ++i)
        b.part(tr, "Cabin louvre", P::Cube, {0.4f, 0.04f, 0.03f}, {0, 0, 0}, {-0.55f, 1.4f + 0.12f * i, 3.21f}, st, "Cooling louvre");
    b.part(tr, "Cabin exhaust", P::Cylinder, {0.1f, 0.1f, 0.5f}, {90, 0, 0}, {0.7f, 2.25f, 2.1f}, st, "Generator exhaust");
    lampMark_ = b.part(tr, "Cabin tactical lamp", P::Sphere, {0.10f, 0.10f, 0.05f}, {0, 0, 0}, {-0.4f, 1.9f, 3.22f}, tacRed(), "Dim red tactical marker above the cabin door (emissive only)");
    strut(b, tr, "Power cable", P::Cylinder, {0, 1.2f, 1.8f}, {0, 1.22f, 0.9f}, 0.07f, rubber(), "Cable from the cabin to the turntable");

    // ---- turntable ----
    turn_ = b.group(r, "Turntable", kSamTurn, {0, 0, 0}, "Rotating base of the launcher");
    turn_->animation = "Yaw +-170 deg, trapezoid velocity profile (22 deg/s, 55 deg/s^2)";
    b.part(turn_, "Slew ring", P::Cylinder, {1.9f, 1.9f, 0.22f}, {90, 0, 0}, {0, 0.11f, 0}, st, "Slewing bearing");
    for (int i = 0; i < 12; ++i) {
        const float a = radians(30.0f * i);
        b.part(turn_, "Slew gear tooth", P::Cube, {0.1f, 0.12f, 0.1f}, {0, degrees(-a), 0}, {0.98f * std::sin(a), 0.11f, 0.98f * std::cos(a)}, st, "Tooth of the slewing gear");
    }
    b.part(turn_, "Turntable deck", P::Cube, {1.9f, 0.12f, 3.0f}, {0, 0, 0}, {0, 0.28f, -0.1f}, od, "Platform carrying the cradle");
    for (float s : {-1.0f, 1.0f}) {
        b.part(turn_, "Cradle upright", P::Cube, {0.14f, 1.4f, 0.5f}, {0, 0, 0}, {s * 0.8f, 1.05f, 0}, od, "Yoke arm holding the trunnion");
        b.part(turn_, "Upright brace", P::Cube, {0.1f, 0.1f, 0.9f}, {0, 0, 0}, {s * 0.8f, 0.5f, 0.2f}, st, "Brace");
    }
    b.part(turn_, "Ram foot", P::Cube, {0.3f, 0.16f, 0.3f}, {0, 0, 0}, {kSamRamBase.x, kSamRamBase.y - 0.04f, kSamRamBase.z}, st, "Hydraulic ram mounting block");
    ramBarrel_ = b.part(turn_, "Ram cylinder", P::Cylinder, {0.16f, 0.16f, 0.5f}, {0, 0, 0}, {0, 0.5f, -1.0f}, st, "Hydraulic elevation cylinder");
    ramBarrel_->animation = "Re-aimed every frame from its fixed foot to the cradle lug";
    ramRod_ = b.part(turn_, "Ram rod", P::Cylinder, {0.09f, 0.09f, 0.5f}, {0, 0, 0}, {0, 0.7f, -0.8f}, makeMaterial({0.6f, 0.6f, 0.62f}, 0.25f, Surface::Metal, 0.3f, 1.0f), "Chrome piston rod");
    ramRod_->animation = "Extends as the cradle rises";

    // ---- cradle ----
    cradle_ = b.group(turn_, "Cradle", kSamCradle, {0, 0, 0}, "Elevating frame holding the four canisters");
    cradle_->animation = "Elevation 0..60 deg about the trunnions, trapezoid profile (14 deg/s, 35 deg/s^2)";
    for (float s : {-1.0f, 1.0f}) {
        b.part(cradle_, "Cradle rail", P::Cube, {0.10f, 0.10f, kCanLen}, {0, 0, 0}, {s * 0.62f, 0, kCanCentreZ}, st, "Side rail of the cradle");
        b.part(cradle_, "Trunnion", P::Cylinder, {0.16f, 0.16f, 0.34f}, {0, 90, 0}, {s * 0.72f, 0, 0}, st, "Pivot pin in the yoke");
    }
    for (float z : {-0.8f, 0.9f, 2.1f}) {
        b.part(cradle_, "Cradle cross beam", P::Cube, {1.4f, 0.10f, 0.12f}, {0, 0, 0}, {0, -0.55f, z}, st, "Lower cross beam");
        b.part(cradle_, "Canister strap", P::Cube, {1.35f, 0.06f, 0.10f}, {0, 0, 0}, {0, 0.52f, z}, st, "Upper strap");
    }
    b.part(cradle_, "Ram lug", P::Cube, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, kSamRamLug, st, "Attachment of the ram rod");
    for (int k = 0; k < kCanisters; ++k) {
        char nm[48];
        std::snprintf(nm, sizeof(nm), "Canister %d", k + 1);
        canister_[k] = b.group(cradle_, nm, kCanOffset[k], {0, 0, 0}, "Square launch canister with front cap");
        b.part(canister_[k], "Canister tube", P::Cube, {0.44f, 0.44f, kCanLen}, {0, 0, 0}, {0, 0, kCanCentreZ}, camo, "Canister body");
        b.part(canister_[k], "Hazard band", P::Cube, {0.46f, 0.46f, 0.18f}, {0, 0, 0}, {0, 0, 1.9f}, hazardBand(), "Warning band near the muzzle");
        muzzleMark_[k] = b.part(canister_[k], "Muzzle collar", P::Cube, {0.50f, 0.50f, 0.06f}, {0, 0, 0}, {0, 0, kCanFrontZ - 0.05f}, st, "Reinforced mouth of the canister (muzzle reference)");
        cap_[k] = b.part(canister_[k], "Canister cap", P::Cube, {0.40f, 0.40f, 0.04f}, {0, 0, 0}, {0, 0, kCanFrontZ + 0.01f}, od, "Frangible front cap, hidden after the launch");
        cap_[k]->animation = "Hidden when the round has left, shown again after the 8 s reload";
        b.part(canister_[k], "Rear hatch", P::Cube, {0.46f, 0.46f, 0.06f}, {0, 0, 0}, {0, 0, kCanRearZ - 0.02f}, st, "Rear blast-venting plate");
    }

    // ---- flying round ----
    round_ = roundGroup(b, r, "SAM round", 0.28f, 2.6f, &flame_);

    // ---- v7 detail pass (appended: ids above unchanged): canister ribs and stencils, a fire-control status panel,
    // generator grille, worn cabin edges and tyre treads. Surface detail only (no obstacles, parent heat).
    const Material stencil = decal({0.52f, 0.50f, 0.42f}, 0.28f);
    for (int k = 0; k < kCanisters; ++k) {
        detail(b, canister_[k], "Canister rib", P::Cube, {0.47f, 0.47f, 0.07f}, {0, 0, 0}, {0, 0, -0.2f}, st, "Stiffening rib");
        const float sx = kCanOffset[k].x > 0.0f ? 1.0f : -1.0f;
        detail(b, canister_[k], "Canister stencil", P::Cube, {0.006f, 0.12f, 0.9f}, {0, 0, 0}, {0.223f * sx, 0.0f, 0.9f}, stencil,
               "Painted round designation");
    }
    detail(b, tr, "Status panel", P::Cube, {0.012f, 0.26f, 0.42f}, {0, 0, 0}, {-0.906f, 1.72f, 2.5f},
           makeMaterial({0.30f, 0.80f, 0.55f}, 0.32f, Surface::Screen, 0.3f, 0.0f, 0.7f), "Launcher status display (emissive)");
    detail(b, tr, "Status LED", P::Sphere, {0.04f, 0.04f, 0.04f}, {0, 0, 0}, {-0.912f, 1.92f, 2.38f}, indicator({0.1f, 1.0f, 0.2f}, 1.0f),
           "Power-on indicator (emissive only)");
    detail(b, tr, "Status LED", P::Sphere, {0.04f, 0.04f, 0.04f}, {0, 0, 0}, {-0.912f, 1.92f, 2.62f}, indicator({1.0f, 0.55f, 0.05f}, 1.0f),
           "Missile-ready indicator (emissive only)");
    detail(b, tr, "Generator grille", P::Cube, {0.012f, 0.42f, 0.6f}, {0, 0, 0}, {0.906f, 1.5f, 2.5f}, shade(st, 0.55f),
           "Generator air grille");
    for (float s : {-1.0f, 1.0f})
        bevel(b, tr, "Cabin roof edge", {s * 0.9f, 2.04f, 1.8f}, {s * 0.9f, 2.04f, 3.2f}, 0.05f, reworked(shade(od, 1.4f), 0.45f, 0.3f),
              "Worn roof edge of the cabin");
    for (float z : {-1.7f, -2.9f})
        for (float s : {-1.0f, 1.0f}) treads(b, tr, {s * 1.25f, 0.5f, z}, 1.0f, 0.4f);

}

Vec3 SamLauncher::muzzleLocal(int k) const {
    const float el = radians(elev_.x);
    const Vec3 pc = kCanOffset[k] + Vec3{0, 0, kCanFrontZ + 0.03f};
    return kSamTurn + rotY(kSamCradle + rotRaise(pc, el), radians(az_.x));
}

Vec3 SamLauncher::axisLocal() const { return rotY(rotRaise({0, 0, 1}, radians(elev_.x)), radians(az_.x)); }
Vec3 SamLauncher::toWorld(const Vec3& p) const { return root_->local.translation + rotY(p, yaw_); }
Vec3 SamLauncher::dirToWorld(const Vec3& d) const { return rotY(d, yaw_); }
Vec3 SamLauncher::muzzleWorld(int k) const { return toWorld(muzzleLocal(k)); }
Vec3 SamLauncher::axisWorld() const { return normalize(dirToWorld(axisLocal())); }
float SamLauncher::azimuthDeg() const {
    float a = degrees(yaw_) + az_.x;
    a = std::fmod(a, 360.0f);
    return a < 0.0f ? a + 360.0f : a;
}
int SamLauncher::loadedCount() const {
    int n = 0;
    for (bool l : loaded_) n += l ? 1 : 0;
    return n;
}
bool SamLauncher::capVisible(int k) const { return cap_[k]->visible; }

void SamLauncher::poseParts() {
    turn_->local.rotationDeg = {0, az_.x, 0};
    cradle_->local.rotationDeg = {-elev_.x, 0, 0};
    // Ram: fixed foot -> lug rotated with the cradle (both in turntable space).
    const Vec3 a = kSamRamBase, c = kSamCradle + rotRaise(kSamRamLug, radians(elev_.x));
    const Vec3 d = c - a;
    const float L = length(d);
    const Vec3 mid = a + d * (0.55f);
    (void)L;
    poseStrut(ramBarrel_, a, mid, 0.16f);
    poseStrut(ramRod_, mid - d * 0.05f, c, 0.09f);
    if (flightT_ >= 0.0f) {
        round_->local.translation = flightPos_;
        round_->local.rotationDeg = alignZ(flightDir_);
        flame_->material.emissive = 1.6f + 0.8f * std::sin(60.0f * time_);
    }
}

void SamLauncher::launch(const WorldContext& w) {
    int k = -1;
    for (int i = 0; i < kCanisters; ++i)
        if (loaded_[i]) { k = i; break; }
    if (k < 0) return;
    loaded_[k] = false;
    cap_[k]->visible = false;
    reload_[k] = kReloadSeconds;
    const Vec3 ax = axisLocal();
    flightDir_ = ax;
    flightPos_ = muzzleLocal(k) - ax * 2.6f;   // starts inside the canister, slides out
    flightT_ = 0.0f;
    setVisible(*round_, true);
    if (w.combat) {
        const Vec3 m = muzzleWorld(k), aw = axisWorld();
        w.combat->muzzleFlash(m, aw, 1.8f);
        w.combat->emitDust(m, 1.2f, 14);
        Vec3 back = toWorld(muzzleLocal(k) - ax * 3.6f);
        back.y = root_->local.translation.y + 0.2f;
        w.combat->emitDust(back, 2.6f, 28);   // back-blast cloud on the ground
    }
}

void SamLauncher::update(const WorldContext& w, const ControlInput* in) {
    const float dt = clampf(w.dt, 0.0f, 0.05f);
    time_ = w.time;
    if (in) {
        if (in->actionPressed) deployed_ = !deployed_;
        if (deployed_) readAim(*in, dt, kAzRate, kElevRate, kAzLimitDeg, kElevMinDeg, kElevMaxDeg, azCmd_, elevCmd_);
    } else if (w.alert > 0.0f) {
        // On alert with nobody at the controls: canisters raised through the net's aperture and ready, the turntable
        // sweeping the part of its sector the aperture leaves free (air targets: ground intruders are not laid on).
        deployed_ = true;
        elevCmd_ = kDefaultElevDeg - 5.0f + 6.0f * std::sin(0.11f * time_);
        azCmd_ = 12.0f * std::sin(0.21f * time_ + 0.8f);
    }
    elev_.step(deployed_ ? elevCmd_ : 0.0f, kElevRate, kElevAccel, dt);
    az_.step(deployed_ ? azCmd_ : 0.0f, kAzRate, kAzAccel, dt);
    if (in && in->firePressed && deployed_ && elev_.x >= kMinFireElevDeg) launch(w);
    for (int k = 0; k < kCanisters; ++k)
        if (!loaded_[k]) {
            reload_[k] -= dt;
            if (reload_[k] <= 0.0f) {
                reload_[k] = 0.0f;
                loaded_[k] = true;
                cap_[k]->visible = true;
            }
        }
    if (flightT_ >= 0.0f) {
        flightT_ += dt;
        const float speed = 8.0f + 60.0f * flightT_;
        flightPos_ = flightPos_ + flightDir_ * (speed * dt);
        if (w.combat) w.combat->emitDust(toWorld(flightPos_), 0.45f, 3);   // smoke trail
        if (flightT_ > kRoundLife) {
            flightT_ = -1.0f;
            setVisible(*round_, false);
        }
    }
    poseParts();
}

CameraHint SamLauncher::camera(bool firstPerson) const {
    CameraHint h;
    const Vec3 ax = axisWorld();
    if (firstPerson) {
        const Vec3 up = normalize(dirToWorld(rotY(rotRaise({0, 1, 0}, radians(elev_.x)), radians(az_.x))));
        const Vec3 pivot = toWorld(kSamTurn + rotY(kSamCradle, radians(az_.x)));
        h.eye = pivot + up * 0.95f - ax * 0.6f;
        h.target = h.eye + ax * 100.0f;
        h.fovDeg = 32.0f;
        h.sight = true;
    } else {
        const float hd = yaw_ + radians(az_.x);
        const Vec3 d{std::sin(hd), 0.0f, std::cos(hd)};
        const Vec3 o = toWorld(kSamTurn + Vec3{0, 1.6f, 0});
        h.eye = o - d * 12.0f + Vec3{0, 3.0f, 0};
        h.target = o + d * 8.0f + Vec3{0, 1.5f, 0};
        h.fovDeg = 55.0f;
    }
    return h;
}

std::string SamLauncher::status() const {
    const int az = static_cast<int>(std::lround(azimuthDeg())) % 360;
    const char* state = !deployed_ ? (elev_.x > 1.0f ? "STOWING" : "STOWED") : (elev_.x < kMinFireElevDeg ? "RAISING" : "READY");
    float rl = 0.0f;
    for (int k = 0; k < kCanisters; ++k)
        if (reload_[k] > 0.0f && (rl == 0.0f || reload_[k] < rl)) rl = reload_[k];
    char buf[128];
    if (rl > 0.0f)
        std::snprintf(buf, sizeof(buf), "SAM-1  AZ %03d  EL %02d  %s %d/4  RELOAD %.0fS", az, static_cast<int>(std::lround(elev_.x)), state, loadedCount(), rl);
    else
        std::snprintf(buf, sizeof(buf), "SAM-1  AZ %03d  EL %02d  %s %d/4", az, static_cast<int>(std::lround(elev_.x)), state, loadedCount());
    return buf;
}

std::vector<Box> SamLauncher::dynamicObstacles() const {
    return {rotatedFootprint(root_->local.translation, yaw_, 1.3f, 3.4f, 3.2f)};
}

// ---------------------------------------------------------------------------------------------------------
// MLRS truck
// ---------------------------------------------------------------------------------------------------------
namespace {
constexpr Vec3 kMlrsMount{0.0f, 1.35f, -1.6f};     // mount origin (root space)
constexpr Vec3 kMlrsPod{0.0f, 1.5f, 0.0f};         // pod pivot (mount space)
constexpr float kTubeLen = 3.6f, kTubeCentreZ = 1.0f, kTubeFrontZ = 2.8f, kTubePitch = 0.34f;
const Vec3 kMlrsRamBase{0.0f, 0.56f, -0.9f};       // mount space
const Vec3 kMlrsRamLug{0.0f, -0.45f, -0.6f};       // pod space
Vec3 tubeOffset(int i) {
    const int col = i % MlrsTruck::kColumns, row = i / MlrsTruck::kColumns;
    return {(static_cast<float>(col) - 2.5f) * kTubePitch, (row == 0 ? 0.17f : -0.17f), 0.0f};
}
// Ripple order: alternate left / right columns, top row then bottom row.
int rippleTube(int n) {
    const int order[6] = {0, 5, 1, 4, 2, 3};
    return (n / 6) * 6 + order[n % 6];
}
}  // namespace

void MlrsTruck::Slew::step(float target, float vmax, float acc, float dt) { slewStep(x, v, target, vmax, acc, dt); }

MlrsTruck::MlrsTruck() {
    for (bool& l : loaded_) l = true;
    build();
    place(camp::kMlrsSite.x, camp::kMlrsSite.z, camp::kMlrsSite.yawDeg);
    poseParts();
}

void MlrsTruck::place(float x, float z, float yawDeg) {
    yaw_ = radians(yawDeg);
    root_->local.translation = {x, terrainHeight(x, z), z};
    root_->local.rotationDeg = {0, yawDeg, 0};
}

void MlrsTruck::build() {
    Builder b("MLRS rocket truck", "MLRS", ShadingModel::Phong);
    root_ = b.root("MLRS rocket truck", {0, 0, 0}, "6x2 rocket truck with a rotating, elevating 12-tube pod (user controlled)");
    SceneNode* r = root_.get();
    const Material od = olivePaint(), camo = camoPaint(), st = steel();

    SceneNode* tk = b.group(r, "Truck", {0, 0, 0}, {0, 0, 0}, "Three-axle tactical truck (local +Z = cab)");
    b.part(tk, "Chassis", P::Cube, {1.1f, 0.32f, 8.0f}, {0, 0, 0}, {0, 0.95f, 0}, st, "Ladder frame");
    b.part(tk, "Cab", P::Cube, {2.45f, 1.6f, 1.9f}, {0, 0, 0}, {0, 1.95f, 3.0f}, od, "Cab-over-engine crew cab");
    b.part(tk, "Cab roof armour", P::Cube, {2.5f, 0.1f, 1.95f}, {0, 0, 0}, {0, 2.8f, 3.0f}, od, "Roof plate");
    b.part(tk, "Windscreen", P::Cube, {2.1f, 0.7f, 0.05f}, {0, 0, 0}, {0, 2.3f, 3.97f}, darkGlass(), "Windscreen");
    b.part(tk, "Bumper", P::Cube, {2.4f, 0.25f, 0.3f}, {0, 0, 0}, {0, 1.0f, 4.1f}, st, "Front bumper");
    for (float s : {-0.8f, 0.8f}) b.part(tk, "Headlight", P::Sphere, {0.22f, 0.22f, 0.1f}, {0, 0, 0}, {s, 1.4f, 3.98f}, darkGlass(), "Headlamp (off)");
    for (float s : {-1.0f, 1.0f}) b.part(tk, "Mirror", P::Cube, {0.06f, 0.35f, 0.2f}, {0, 0, 0}, {s * 1.35f, 2.3f, 3.8f}, st, "Mirror arm");
    b.part(tk, "Cab tactical lamp", P::Sphere, {0.10f, 0.10f, 0.05f}, {0, 0, 0}, {0.9f, 1.15f, 4.02f}, makeMaterial({0.05f, 0.85f, 0.60f}, 0.35f, Surface::Glass, 0.2f, 0.0f, 0.5f), "Blue-green convoy marker (emissive only)");
    b.part(tk, "Bed deck", P::Cube, {2.5f, 0.2f, 5.0f}, {0, 0, 0}, {0, 1.25f, -1.6f}, od, "Load bed under the mount");
    for (float s : {-1.0f, 1.0f}) b.part(tk, "Bed side", P::Cube, {0.1f, 0.3f, 5.0f}, {0, 0, 0}, {s * 1.2f, 1.5f, -1.6f}, od, "Low side plate");
    for (float z : {3.1f, -1.6f, -3.0f})
        for (float s : {-1.0f, 1.0f}) wheel(b, tk, {s * 1.15f, 0.575f, z}, 1.15f, 0.45f);
    for (float s : {-1.0f, 1.0f}) {
        b.part(tk, "Fender", P::Cube, {0.5f, 0.08f, 1.3f}, {0, 0, 0}, {s * 1.15f, 1.2f, 3.1f}, od, "Front mudguard");
        b.part(tk, "Fender", P::Cube, {0.5f, 0.08f, 2.4f}, {0, 0, 0}, {s * 1.15f, 1.2f, -2.3f}, od, "Rear mudguard");
        strut(b, tk, "Outrigger", P::Cylinder, {s * 1.2f, 1.0f, -3.8f}, {s * 1.8f, 0.06f, -4.0f}, 0.1f, st, "Rear stabiliser jack");
        b.part(tk, "Outrigger pad", P::Cube, {0.4f, 0.06f, 0.4f}, {0, 0, 0}, {s * 1.8f, 0.03f, -4.0f}, st, "Ground pad");
    }

    mount_ = b.group(r, "Mount", kMlrsMount, {0, 0, 0}, "Rotating mount on the load bed");
    mount_->animation = "Yaw +-165 deg, trapezoid velocity profile (18 deg/s, 45 deg/s^2)";
    b.part(mount_, "Slew ring", P::Cylinder, {1.7f, 1.7f, 0.2f}, {90, 0, 0}, {0, 0.1f, 0}, st, "Slewing bearing");
    b.part(mount_, "Mount base", P::Cube, {1.9f, 0.35f, 1.9f}, {0, 0, 0}, {0, 0.38f, 0}, od, "Base block of the mount");
    for (float s : {-1.0f, 1.0f}) {
        b.part(mount_, "Pod arm", P::Cube, {0.22f, 1.4f, 0.6f}, {0, 0, 0}, {s * 1.22f, 1.05f, 0}, od, "Yoke arm");
        b.part(mount_, "Arm brace", P::Cube, {0.1f, 0.1f, 1.1f}, {0, 0, 0}, {s * 1.22f, 0.5f, 0.2f}, st, "Brace");
    }
    b.part(mount_, "Ram foot", P::Cube, {0.3f, 0.12f, 0.3f}, {0, 0, 0}, {kMlrsRamBase.x, kMlrsRamBase.y - 0.04f, kMlrsRamBase.z}, st, "Ram mounting block");
    ramBarrel_ = b.part(mount_, "Ram cylinder", P::Cylinder, {0.16f, 0.16f, 0.5f}, {0, 0, 0}, {0, 0.8f, -0.7f}, st, "Hydraulic elevation cylinder");
    ramBarrel_->animation = "Re-aimed every frame from its fixed foot to the pod lug";
    ramRod_ = b.part(mount_, "Ram rod", P::Cylinder, {0.09f, 0.09f, 0.5f}, {0, 0, 0}, {0, 0.9f, -0.6f}, makeMaterial({0.6f, 0.6f, 0.62f}, 0.25f, Surface::Metal, 0.3f, 1.0f), "Chrome piston rod");
    ramRod_->animation = "Extends as the pod rises";

    pod_ = b.group(mount_, "Pod", kMlrsPod, {0, 0, 0}, "Elevating launch-tube pod");
    pod_->animation = "Elevation 0..55 deg about the trunnions, trapezoid profile (12 deg/s, 30 deg/s^2)";
    for (float s : {-1.0f, 1.0f}) {
        b.part(pod_, "Pod side plate", P::Cube, {0.08f, 0.9f, kTubeLen}, {0, 0, 0}, {s * 1.17f, 0, kTubeCentreZ}, st, "Side plate of the pod");
        b.part(pod_, "Trunnion", P::Cylinder, {0.18f, 0.18f, 0.3f}, {0, 90, 0}, {s * 1.2f, 0, 0}, st, "Pivot pin in the yoke arm");
    }
    for (float y : {-0.44f, 0.44f}) b.part(pod_, "Pod frame", P::Cube, {2.3f, 0.08f, kTubeLen}, {0, 0, 0}, {0, y, kTubeCentreZ}, st, "Top / bottom plate of the pod");
    b.part(pod_, "Pod rear plate", P::Cube, {2.3f, 0.9f, 0.08f}, {0, 0, 0}, {0, 0, -0.84f}, st, "Blast plate behind the tubes");
    b.part(pod_, "Ram lug", P::Cube, {0.2f, 0.2f, 0.2f}, {0, 0, 0}, kMlrsRamLug, st, "Attachment of the ram rod");
    for (int i = 0; i < kTubes; ++i) {
        char nm[32];
        std::snprintf(nm, sizeof(nm), "Tube %02d", i + 1);
        SceneNode* t = b.group(pod_, nm, tubeOffset(i), {0, 0, 0}, "Rocket launch tube with front cap");
        tube_[i] = b.part(t, "Launch tube", P::Cylinder, {0.30f, 0.30f, kTubeLen}, {0, 0, 0}, {0, 0, kTubeCentreZ}, camo, "Fibre-glass launch tube");
        muzzleMark_[i] = b.part(t, "Muzzle ring", P::Torus, lib::torusScale(0.34f, 0.08f), {0, 0, 0}, {0, 0, kTubeFrontZ - 0.06f}, st, "Reinforced tube mouth (muzzle reference)");
        cap_[i] = b.part(t, "Tube cap", P::Cylinder, {0.31f, 0.31f, 0.04f}, {0, 0, 0}, {0, 0, kTubeFrontZ + 0.01f}, od, "Frangible front cap, hidden after the round leaves");
        cap_[i]->animation = "Hidden when the tube has fired, shown again after the reload";
    }

    for (int i = 0; i < kRocketPool; ++i) {
        char nm[32];
        std::snprintf(nm, sizeof(nm), "Rocket %d", i + 1);
        rockets_[i].node = roundGroup(b, r, nm, 0.22f, 2.2f, nullptr);
    }

    // ---- v7 detail pass (appended: ids above unchanged): cab windows, radiator grille, exhaust stack, worn cab edges,
    // pod stencils and tyre treads. Surface detail only (no obstacles, parent heat).
    for (float s : {-1.0f, 1.0f}) {
        detail(b, tk, "Cab side window", P::Cube, {0.02f, 0.5f, 0.8f}, {0, 0, 0}, {s * 1.228f, 2.3f, 3.05f}, darkGlass(), "Side window of the cab");
        bevel(b, tk, "Cab roof edge", {s * 1.25f, 2.85f, 2.03f}, {s * 1.25f, 2.85f, 3.97f}, 0.05f, reworked(shade(od, 1.4f), 0.45f, 0.3f),
              "Worn roof edge of the cab");
        detail(b, pod_, "Pod stencil", P::Cube, {0.006f, 0.16f, 1.6f}, {0, 0, 0}, {s * 1.213f, 0.0f, 1.2f}, decal({0.52f, 0.50f, 0.42f}, 0.30f),
               "Painted pod designation");
    }
    detail(b, tk, "Radiator grille", P::Cube, {1.1f, 0.42f, 0.02f}, {0, 0, 0}, {0, 1.5f, 3.96f}, shade(st, 0.45f), "Radiator grille");
    detail(b, tk, "Exhaust stack", P::Cylinder, {0.12f, 0.12f, 0.9f}, {90, 0, 0}, {1.0f, 2.45f, 1.95f}, shade(st, 0.6f),
           "Vertical exhaust stack behind the cab");
    for (float z : {3.1f, -1.6f, -3.0f})
        for (float s : {-1.0f, 1.0f}) treads(b, tk, {s * 1.15f, 0.575f, z}, 1.15f, 0.45f);
}

Vec3 MlrsTruck::muzzleLocal(int tube) const {
    const Vec3 pc = tubeOffset(tube) + Vec3{0, 0, kTubeFrontZ + 0.03f};
    return kMlrsMount + rotY(kMlrsPod + rotRaise(pc, radians(elev_.x)), radians(az_.x));
}
Vec3 MlrsTruck::axisLocal() const { return rotY(rotRaise({0, 0, 1}, radians(elev_.x)), radians(az_.x)); }
Vec3 MlrsTruck::toWorld(const Vec3& p) const { return root_->local.translation + rotY(p, yaw_); }
Vec3 MlrsTruck::dirToWorld(const Vec3& d) const { return rotY(d, yaw_); }
Vec3 MlrsTruck::muzzleWorld(int tube) const { return toWorld(muzzleLocal(tube)); }
Vec3 MlrsTruck::axisWorld() const { return normalize(dirToWorld(axisLocal())); }
float MlrsTruck::azimuthDeg() const {
    float a = std::fmod(degrees(yaw_) + az_.x, 360.0f);
    return a < 0.0f ? a + 360.0f : a;
}
int MlrsTruck::loadedCount() const {
    int n = 0;
    for (bool l : loaded_) n += l ? 1 : 0;
    return n;
}
bool MlrsTruck::capVisible(int tube) const { return cap_[tube]->visible; }
int MlrsTruck::rocketsInFlight() const {
    int n = 0;
    for (const Rocket& r : rockets_) n += r.t >= 0.0f ? 1 : 0;
    return n;
}

void MlrsTruck::poseParts() {
    mount_->local.rotationDeg = {0, az_.x, 0};
    pod_->local.rotationDeg = {-elev_.x, 0, 0};
    const Vec3 a = kMlrsRamBase, c = kMlrsPod + rotRaise(kMlrsRamLug, radians(elev_.x));
    const Vec3 d = c - a;
    const Vec3 mid = a + d * 0.55f;
    poseStrut(ramBarrel_, a, mid, 0.16f);
    poseStrut(ramRod_, mid - d * 0.05f, c, 0.09f);
    for (Rocket& rk : rockets_)
        if (rk.t >= 0.0f) {
            rk.node->local.translation = rk.pos;
            rk.node->local.rotationDeg = alignZ(rk.dir);
        }
}

void MlrsTruck::fireTube(const WorldContext& w, int tube) {
    loaded_[tube] = false;
    cap_[tube]->visible = false;
    for (Rocket& rk : rockets_)
        if (rk.t < 0.0f) {
            rk.t = 0.0f;
            rk.dir = axisLocal();
            rk.pos = muzzleLocal(tube) - rk.dir * 2.0f;
            setVisible(*rk.node, true);
            break;
        }
    if (w.combat) {
        const Vec3 m = muzzleWorld(tube), aw = axisWorld();
        w.combat->muzzleFlash(m, aw, 1.2f);
        w.combat->emitDust(m, 0.9f, 10);   // smoke puff per tube
        Vec3 back = toWorld(muzzleLocal(tube) - axisLocal() * 3.0f);
        back.y = root_->local.translation.y + 0.2f;
        w.combat->emitDust(back, 1.8f, 12);
    }
}

void MlrsTruck::update(const WorldContext& w, const ControlInput* in) {
    const float dt = clampf(w.dt, 0.0f, 0.05f);
    time_ = w.time;
    if (in) {
        if (in->actionPressed && !rippling_) deployed_ = !deployed_;
        if (deployed_) readAim(*in, dt, kAzRate, kElevRate, kAzLimitDeg, kElevMinDeg, kElevMaxDeg, azCmd_, elevCmd_);
    } else if (w.alert > 0.0f && !rippling_) {
        // On alert: the pod raised through the net's aperture to its firing elevation, the mount sweeping inside it
        // (long-range fire: ground intruders inside the camp are not laid on).
        deployed_ = true;
        elevCmd_ = 34.0f + 5.0f * std::sin(0.09f * time_ + 1.3f);
        azCmd_ = 9.0f * std::sin(0.16f * time_);
    }
    elev_.step(deployed_ ? elevCmd_ : 0.0f, kElevRate, kElevAccel, dt);
    az_.step(deployed_ ? azCmd_ : 0.0f, kAzRate, kAzAccel, dt);
    if (in && in->firePressed && deployed_ && !rippling_ && elev_.x >= kMinFireElevDeg && loadedCount() > 0) {
        rippling_ = true;
        rippleNext_ = 0;
        rippleFired_ = 0;
        rippleClock_ = kRippleInterval;   // first tube immediately
    }
    if (rippling_) {
        rippleClock_ += dt;
        while (rippleClock_ >= kRippleInterval && rippleNext_ < kTubes) {
            rippleClock_ -= kRippleInterval;
            const int tube = rippleTube(rippleNext_++);
            if (loaded_[tube]) {
                fireTube(w, tube);
                ++rippleFired_;
            }
        }
        if (rippleNext_ >= kTubes) {
            rippling_ = false;
            reload_ = kReloadSeconds;
        }
    }
    if (reload_ > 0.0f) {
        reload_ -= dt;
        if (reload_ <= 0.0f) {
            reload_ = 0.0f;
            for (int i = 0; i < kTubes; ++i) {
                loaded_[i] = true;
                cap_[i]->visible = true;
            }
        }
    }
    for (Rocket& rk : rockets_)
        if (rk.t >= 0.0f) {
            rk.t += dt;
            rk.pos = rk.pos + rk.dir * ((6.0f + 55.0f * rk.t) * dt);
            if (w.combat) w.combat->emitDust(toWorld(rk.pos), 0.35f, 2);
            if (rk.t > kRocketLife) {
                rk.t = -1.0f;
                setVisible(*rk.node, false);
            }
        }
    poseParts();
}

CameraHint MlrsTruck::camera(bool firstPerson) const {
    CameraHint h;
    const Vec3 ax = axisWorld();
    if (firstPerson) {
        const Vec3 up = normalize(dirToWorld(rotY(rotRaise({0, 1, 0}, radians(elev_.x)), radians(az_.x))));
        const Vec3 pivot = toWorld(kMlrsMount + rotY(kMlrsPod, radians(az_.x)));
        h.eye = pivot + up * 0.85f - ax * 0.6f;
        h.target = h.eye + ax * 100.0f;
        h.fovDeg = 32.0f;
        h.sight = true;
    } else {
        const float hd = yaw_ + radians(az_.x);
        const Vec3 d{std::sin(hd), 0.0f, std::cos(hd)};
        const Vec3 o = toWorld(kMlrsMount + Vec3{0, 1.6f, 0});
        h.eye = o - d * 13.0f + Vec3{0, 3.2f, 0};
        h.target = o + d * 8.0f + Vec3{0, 1.5f, 0};
        h.fovDeg = 55.0f;
    }
    return h;
}

std::string MlrsTruck::status() const {
    const int az = static_cast<int>(std::lround(azimuthDeg())) % 360;
    const int el = static_cast<int>(std::lround(elev_.x));
    char buf[128];
    if (rippling_)
        std::snprintf(buf, sizeof(buf), "MLRS-1  AZ %03d  EL %02d  RIPPLE %d/%d", az, el, rippleFired_, kTubes);
    else if (reload_ > 0.0f)
        std::snprintf(buf, sizeof(buf), "MLRS-1  AZ %03d  EL %02d  RELOAD %.0fS", az, el, reload_);
    else {
        const char* state = !deployed_ ? (elev_.x > 1.0f ? "STOWING" : "STOWED") : (elev_.x < kMinFireElevDeg ? "RAISING" : "READY");
        std::snprintf(buf, sizeof(buf), "MLRS-1  AZ %03d  EL %02d  %s %d/%d", az, el, state, loadedCount(), kTubes);
    }
    return buf;
}

std::vector<Box> MlrsTruck::dynamicObstacles() const {
    return {rotatedFootprint(root_->local.translation, yaw_, 1.3f, 4.2f, 3.0f)};
}

// ---------------------------------------------------------------------------------------------------------
// Ammunition bunker (static) and camp dressing (static)
// ---------------------------------------------------------------------------------------------------------
namespace {

class StaticProp : public Controllable {
public:
    StaticProp(std::unique_ptr<SceneNode> r, const char* nm, Vec3 pos, Vec3 eye, Vec3 target)
        : root_(std::move(r)), name_(nm), pos_(pos), eye_(eye), target_(target) {}
    const char* name() const override { return name_; }
    const char* controls() const override { return "(static scenery)"; }
    bool moves() const override { return false; }
    SceneNode* root() override { return root_.get(); }
    void update(const WorldContext&, const ControlInput*) override {}
    CameraHint camera(bool) const override {
        CameraHint h;
        h.eye = eye_;
        h.target = target_;
        return h;
    }
    std::string status() const override { return name_; }
    Vec3 position() const override { return pos_; }
    bool possessable() const override { return false; }

private:
    std::unique_ptr<SceneNode> root_;
    const char* name_;
    Vec3 pos_, eye_, target_;
};

std::unique_ptr<Controllable> makeAmmoBunker() {
    const camp::Site& s = camp::kAmmoSite;
    Builder b("Ammunition bunker", "AMMO", ShadingModel::Gouraud);
    const Vec3 g{s.x, terrainHeight(s.x, s.z), s.z};
    auto root = b.root("Ammunition bunker", g, "Earth-covered ammunition store; entrance towards local +Z");
    root->local.rotationDeg = {0, s.yawDeg, 0};
    SceneNode* r = root.get();
    const Material earth = earthMat(), conc = mats::concrete(), st = steel(), bag = mats::sandbag();
    b.part(r, "Concrete chamber", P::Cube, {5.6f, 1.3f, 5.2f}, {0, 0, 0}, {0, 0.65f, -0.4f}, conc, "Reinforced concrete chamber buried in the mound");
    b.part(r, "Earth mound", P::Sphere, {8.2f, 4.4f, 7.4f}, {0, 0, 0}, {0, 0.0f, -0.4f}, earth, "Earth cover that hides the chamber (half-buried ellipsoid)");
    b.part(r, "Earth mound lobe", P::Sphere, {4.2f, 2.4f, 3.4f}, {0, 20, 0}, {-2.2f, 0.1f, -1.8f}, earth, "Secondary spoil heap");
    b.part(r, "Earth mound lobe", P::Sphere, {3.8f, 2.2f, 3.0f}, {0, -25, 0}, {2.3f, 0.1f, -1.2f}, earth, "Secondary spoil heap");
    const Material turf = mats::leaves();
    for (int i = 0; i < 5; ++i)
        b.part(r, "Turf clump", P::Sphere, {1.0f, 0.4f, 0.9f}, {0, 37.0f * i, 0}, {-1.8f + 0.9f * i, 1.7f - 0.1f * std::fabs(2.0f - i), -1.2f + 0.3f * i}, turf, "Grass and brush growing on the mound");
    // Headwall with the blast door.
    for (float sx : {-1.0f, 1.0f}) b.part(r, "Headwall block", P::Cube, {1.3f, 1.9f, 0.5f}, {0, 0, 0}, {sx * 1.15f, 0.95f, 2.4f}, conc, "Concrete headwall beside the door");
    b.part(r, "Headwall lintel", P::Cube, {3.6f, 0.4f, 0.5f}, {0, 0, 0}, {0, 1.7f, 2.4f}, conc, "Lintel over the door");
    b.part(r, "Blast door", P::Cube, {1.0f, 1.5f, 0.12f}, {0, 0, 0}, {0, 0.75f, 2.2f}, plate(), "Steel blast door");
    b.part(r, "Door frame stripe", P::Cube, {1.2f, 0.1f, 0.14f}, {0, 0, 0}, {0, 1.55f, 2.2f}, hazardBand(), "Hazard stripe on the frame");
    b.part(r, "Door step", P::Cube, {1.4f, 0.14f, 0.5f}, {0, 0, 0}, {0, 0.07f, 2.85f}, conc, "Concrete step");
    b.part(r, "Door step marker", P::Sphere, {0.09f, 0.04f, 0.09f}, {0, 0, 0}, {0.5f, 0.16f, 2.95f}, makeMaterial({0.05f, 0.85f, 0.60f}, 0.35f, Surface::Glass, 0.2f, 0.0f, 0.55f), "Blue-green marker on the step (emissive only)");
    b.part(r, "Door lamp hood", P::Cube, {0.34f, 0.04f, 0.3f}, {0, 0, 0}, {0, 2.0f, 2.75f}, st, "Hood that keeps the light pointing down");
    b.part(r, "Door lamp", P::Sphere, {0.16f, 0.16f, 0.06f}, {90, 0, 0}, {0, 1.93f, 2.75f}, tacTealLamp(), "Dim blue-green hooded lamp over the door (practical light)");
    b.part(r, "Warning sign", P::Cube, {0.5f, 0.35f, 0.03f}, {0, 0, 0}, {-1.15f, 1.3f, 2.67f}, mats::warningSign(), "EXPLOSIVES sign");
    // Blast wing walls and baffle.
    for (float sx : {-1.0f, 1.0f}) {
        b.part(r, "Wing wall core", P::Cube, {0.6f, 1.3f, 2.4f}, {0, 0, 0}, {sx * 2.0f, 0.65f, 3.5f}, earth, "Earth wing wall guiding blast away from the door");
        b.part(r, "Wing wall slope", P::Wedge, {2.4f, 1.3f, 0.65f}, {0, sx * 90.0f, 0}, {sx * 2.625f, 0.65f, 3.5f}, earth, "Outer slope of the wing wall");
        for (int k = 0; k < 4; ++k) b.part(r, "Wing sandbag", P::Capsule, {0.28f, 0.17f, 0.5f}, {0, 90, 0}, {sx * 2.0f, 1.38f, 2.5f + 0.6f * k}, bag, "Sandbag crest");
    }
    b.part(r, "Blast baffle", P::Cube, {2.6f, 1.0f, 0.5f}, {0, 0, 0}, {0, 0.5f, 5.4f}, bag, "Sandbag baffle in front of the door");
    for (float sx : {-1.0f, 1.0f}) {
        b.part(r, "Vent pipe", P::Cylinder, {0.2f, 0.2f, 1.0f}, {90, 0, 0}, {sx * 1.3f, 2.0f, -1.0f}, st, "Ventilation pipe through the earth cover");
        b.part(r, "Vent cap", P::Cylinder, {0.34f, 0.34f, 0.06f}, {90, 0, 0}, {sx * 1.3f, 2.55f, -1.0f}, st, "Rain cap");
    }
    addSupplyCache(b, r, 0.0f, 0.0f, 0.0f, 0);   // placed by the dressing below (moved)
    SceneNode* cache = r->children.back().get();
    cache->local.translation = {-2.9f, 0.0f, 4.7f};
    cache->local.rotationDeg = {0, 30, 0};
    return std::make_unique<StaticProp>(std::move(root), "AMMO BUNKER", g, g + Vec3{-9.0f, 3.5f, 4.0f}, g + Vec3{0, 1.2f, 0});
}

Vec3 groundAt(float x, float z) { return {x, terrainHeight(x, z), z}; }

// Revetment, net and branches for one vehicle bay.
void dressBay(Builder& b, SceneNode* parent, float x, float z, float yawDeg, float halfW, float halfD, float netFront,
              uint32_t seed, float apertureHalf, float apertureBayZ0, int branchSides, bool frontWall = true) {
    const Vec3 g = groundAt(x, z);
    addRevetment(b, parent, g, yawDeg, halfW, halfD, 1.3f, frontWall);
    const float rear = halfD + 0.6f, fr = std::max(halfD + 1.45f, netFront);
    const float width = 2.0f * (halfW + 1.2f), depth = rear + fr, cz = 0.5f * (fr - rear);
    const float y = radians(yawDeg);
    const Vec3 nc = groundAt(x + std::sin(y) * cz, z + std::cos(y) * cz);
    // Hole above the launcher: net-local z runs from -depth/2 .. +depth/2 about the net centre.
    if (apertureHalf > 0.0f)
        addCamoNet(b, parent, nc, yawDeg, width, depth, 3.9f, 0.32f, seed, apertureHalf, apertureBayZ0 - cz, depth);
    else
        addCamoNet(b, parent, nc, yawDeg, width, depth, 3.9f, 0.32f, seed);
    for (int k = 0; k < branchSides; ++k) {
        const float side = (k == 0) ? -1.0f : 1.0f;
        const float lx = side * (halfW - 0.55f);
        const Vec3 p = groundAt(x + std::cos(y) * lx, z - std::sin(y) * lx);
        // group yaw so that the branch group's +X points away from the vehicle on this side
        addCutBranches(b, parent, p, yawDeg + (side < 0 ? 180.0f : 0.0f), 0.0f, 2.8f, seed + 17u * (k + 1));
    }
}

std::unique_ptr<Controllable> makeDressing() {
    Builder b("Camp camouflage", "CAMP", ShadingModel::Gouraud);
    auto root = b.root("Camp camouflage", {0, 0, 0}, "Earth revetments, camouflage nets and cut branches around the camp vehicles");
    SceneNode* r = root.get();
    using namespace camp;
    // SAM and MLRS: net with an aperture over the launcher so the canisters can be raised through it.
    dressBay(b, r, kSamSite.x, kSamSite.z, kSamSite.yawDeg, kSamSite.halfW, kSamSite.halfD, kSamSite.front, 0x5a31u, 1.6f, 0.0f, 2);
    dressBay(b, r, kMlrsSite.x, kMlrsSite.z, kMlrsSite.yawDeg, kMlrsSite.halfW, kMlrsSite.halfD, kMlrsSite.front, 0x6b42u, 1.8f, -2.2f, 2);
    dressBay(b, r, kTankSouth.x, kTankSouth.z, kTankSouth.yawDeg, kTankSouth.halfW, kTankSouth.halfD, kTankSouth.front, 0x7c53u, 0.0f, 0.0f, 2);
    dressBay(b, r, kTankWest.x, kTankWest.z, kTankWest.yawDeg, kTankWest.halfW, kTankWest.halfD, kTankWest.front, 0x8d64u, 0.0f, 0.0f, 2);
    // The original tank park gets the same treatment (walls stay outside the 6 x 9 m tank spot); its front is open:
    // the ready tank drives straight out onto the road.
    dressBay(b, r, layout::kMotorPool.x, layout::kMotorPool.z, layout::kTankParkYawDeg, kPoolHalfW, kPoolHalfD, kPoolFront, 0x9e75u, 0.0f, 0.0f, 0,
             false);
    // Ammunition crates inside two of the bays (rear corners).
    addSupplyCache(b, r, kSamSite.x - std::sin(radians(kSamSite.yawDeg)) * 3.4f + std::cos(radians(kSamSite.yawDeg)) * 1.2f,
                   kSamSite.z - std::cos(radians(kSamSite.yawDeg)) * 3.4f - std::sin(radians(kSamSite.yawDeg)) * 1.2f, 20.0f, 0);
    addSupplyCache(b, r, kMlrsSite.x - std::sin(radians(kMlrsSite.yawDeg)) * 3.9f - std::cos(radians(kMlrsSite.yawDeg)) * 1.2f,
                   kMlrsSite.z - std::cos(radians(kMlrsSite.yawDeg)) * 3.9f + std::sin(radians(kMlrsSite.yawDeg)) * 1.2f, -35.0f, 1);
    for (auto& n : r->children) n->occluder = false;
    return std::make_unique<StaticProp>(std::move(root), "CAMP NETS", Vec3{kSamSite.x, 0, kSamSite.z},
                                        Vec3{kSamSite.x - 14.0f, 6.0f, kSamSite.z - 6.0f}, Vec3{kSamSite.x, 1.5f, kSamSite.z});
}

}  // namespace

std::vector<std::unique_ptr<Controllable>> makeLaunchers() {
    std::vector<std::unique_ptr<Controllable>> v;
    v.push_back(std::make_unique<SamLauncher>());
    v.push_back(std::make_unique<MlrsTruck>());
    v.push_back(makeParkedTank({camp::kTankSouth.x, 0.0f, camp::kTankSouth.z}, camp::kTankSouth.yawDeg, 2));
    v.push_back(makeParkedTank({camp::kTankWest.x, 0.0f, camp::kTankWest.z}, camp::kTankWest.yawDeg, 3));
    v.push_back(makeAmmoBunker());
    v.push_back(makeDressing());
    return v;
}

}  // namespace ts
