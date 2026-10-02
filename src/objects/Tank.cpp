#include "objects/Tank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/Builder.h"
#include "objects/BaseLayout.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"
#include "scene/Combat.h"

namespace ts {

namespace {

constexpr float kCornerZ = 2.6f;            // terrain sample points: track contact corners
constexpr float kHalfLen = 0.5f * Tank::kHullLength, kHalfWid = 0.5f * Tank::kHullWidth;
constexpr float kMaxRadius = layout::kPerimeterRadius - 3.0f;
constexpr float kTurretY = 1.42f, kTurretZ = -0.15f;
constexpr float kGunPivotY = 0.42f, kGunPivotZ = 1.5f, kBarrelBaseZ = 0.3f, kMuzzleZ = 4.5f;

float wrapPi(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a < -PI) a += 2.0f * PI;
    return a;
}
float smooth01(float u) { u = clampf(u, 0.0f, 1.0f); return u * u * (3.0f - 2.0f * u); }

Mat4 worldOf(const SceneNode* n) {
    Mat4 m = n->local.matrix();
    for (const SceneNode* p = n->parent; p; p = p->parent) m = p->local.matrix() * m;
    return m;
}

// Spring toward a target: x'' = w^2 (target - x) - 2 zeta w x'.
void spring(float& x, float& v, float target, float w, float zeta, float dt) {
    const int steps = 4;
    const float h = dt / steps;
    for (int i = 0; i < steps; ++i) {
        v += (w * w * (target - x) - 2.0f * zeta * w * v) * h;
        x += v * h;
    }
}

// 2D separating-axis test: oriented hull rectangle against a world AABB (ground plane).
bool obbHitsBox(float cx, float cz, float yaw, float hw, float hl, const Box& b) {
    const float s = std::sin(yaw), c = std::cos(yaw);
    const float fx = s, fz = c, lx = c, lz = -s;      // forward, left axes
    const float bx = 0.5f * (b.min.x + b.max.x), bz = 0.5f * (b.min.z + b.max.z);
    const float ex = 0.5f * (b.max.x - b.min.x), ez = 0.5f * (b.max.z - b.min.z);
    const float dx = bx - cx, dz = bz - cz;
    // world axes
    const float rx = std::fabs(lx) * hw + std::fabs(fx) * hl;
    const float rz = std::fabs(lz) * hw + std::fabs(fz) * hl;
    if (std::fabs(dx) > rx + ex || std::fabs(dz) > rz + ez) return false;
    // hull axes
    const float rf = ex * std::fabs(fx) + ez * std::fabs(fz);
    const float rl = ex * std::fabs(lx) + ez * std::fabs(lz);
    if (std::fabs(dx * fx + dz * fz) > hl + rf) return false;
    if (std::fabs(dx * lx + dz * lz) > hw + rl) return false;
    return true;
}

}  // namespace

// ---------------------------------------------------------------- track path

namespace tankpath {
float perimeter() { return 2.0f * (kIdlerZ - kSprocketZ) + 2.0f * PI * kRadius; }

void point(float s, float& z, float& y, float& phi) {
    const float L = kIdlerZ - kSprocketZ, A = PI * kRadius, R = kRadius, P = perimeter();
    s = std::fmod(s, P);
    if (s < 0.0f) s += P;
    if (s < L) {
        z = kSprocketZ + s; y = kCentreY + R; phi = 0.0f;
    } else if (s < L + A) {
        const float th = (s - L) / R;
        z = kIdlerZ + R * std::sin(th); y = kCentreY + R * std::cos(th); phi = th;
    } else if (s < 2.0f * L + A) {
        z = kIdlerZ - (s - L - A); y = kCentreY - R; phi = PI;
    } else {
        const float th = (s - 2.0f * L - A) / R;
        z = kSprocketZ - R * std::sin(th); y = kCentreY - R * std::cos(th); phi = PI + th;
    }
}

float distance(float z, float y) {
    if (z >= kSprocketZ && z <= kIdlerZ)
        return std::min(std::fabs(y - (kCentreY + kRadius)), std::fabs(y - (kCentreY - kRadius)));
    const float cz = z > 0.0f ? kIdlerZ : kSprocketZ;
    return std::fabs(std::sqrt((z - cz) * (z - cz) + (y - kCentreY) * (y - kCentreY)) - kRadius);
}
}  // namespace tankpath

// ---------------------------------------------------------------- rig

Tank::Tank() { buildRig(); }

void Tank::buildRig() {
    using P = PrimitiveType;
    Builder b("Main Battle Tank", "TNK", ShadingModel::Phong);
    root_ = b.root("Main Battle Tank", {0, 0, 0}, "User-controllable tracked tank (skid steer, stabilised turret)");
    root_->animation = "World position / yaw from skid-steer integration, tilt from terrain height at the 4 track corners";
    root_->explodable = false;
    SceneNode* r = root_.get();

    const Material camo = makeMaterial({0.10f, 0.12f, 0.075f}, 0.25f, Surface::Camo, 0.7f, 0.0f);
    const Material camoDark = makeMaterial({0.075f, 0.09f, 0.06f}, 0.25f, Surface::Camo, 0.72f, 0.0f);
    const Material steel = makeMaterial({0.05f, 0.052f, 0.05f}, 0.27f, Surface::Metal, 0.55f, 0.6f);
    const Material trackMat = makeMaterial({0.07f, 0.06f, 0.05f}, 0.30f, Surface::Metal, 0.62f, 0.55f);
    const Material rubber = makeMaterial({0.035f, 0.035f, 0.035f}, 0.30f, Surface::Rubber, 0.85f, 0.0f);
    const Material gunMetal = makeMaterial({0.08f, 0.085f, 0.08f}, 0.35f, Surface::Metal, 0.5f, 0.7f);
    const Material glass = makeMaterial({0.03f, 0.05f, 0.06f}, 0.2f, Surface::Glass, 0.1f, 0.0f);
    const Material hot = makeMaterial({0.07f, 0.06f, 0.055f}, 0.85f, Surface::Metal, 0.6f, 0.5f);
    const Material lampOff = makeMaterial({1.0f, 0.93f, 0.75f}, 0.3f, Surface::Glass, 0.15f, 0.0f);
    const Material tailRed = makeMaterial({0.8f, 0.05f, 0.03f}, 0.3f, Surface::Glass, 0.2f, 0.0f, 0.5f);

    auto camoPart = [&](SceneNode* n) { camoParts_.push_back(n); camoBase_.push_back(n->material.baseColor); return n; };
    // Flat plate between two (z, y) points of the hull profile, at lateral position x.
    auto plate = [&](SceneNode* parent, const char* name, float x, float w, float t, float z0, float y0, float z1,
                     float y1, const Material& m, const char* purpose) {
        const Vec3 d{0.0f, y1 - y0, z1 - z0};
        return b.part(parent, name, P::Cube, {w, t, length(d)}, alignZ(d), {x, 0.5f * (y0 + y1), 0.5f * (z0 + z1)}, m,
                      purpose);
    };

    hull_ = b.group(r, "Hull", {0, 0, 0}, {0, 0, 0}, "Hull body on the suspension (heave + pitch spring)");
    hull_->animation = "Suspension heave and pitch spring: squats when accelerating, rocks back when the gun fires";
    SceneNode* h = hull_;

    // --- hull plates -------------------------------------------------------------------------------------------
    camoPart(b.part(h, "Hull core", P::Cube, {1.7f, 0.9f, 5.6f}, {0, 0, 0}, {0, 0.8f, 0}, camo, "Armoured lower hull between the tracks"));
    camoPart(b.part(h, "Hull deck", P::Cube, {2.9f, 0.26f, 4.6f}, {0, 0, 0}, {0, 1.29f, -0.5f}, camo, "Superstructure deck over the tracks"));
    camoPart(plate(h, "Glacis plate", 0.0f, 2.5f, 0.12f, 1.8f, 1.42f, 3.1f, 0.78f, camo, "Sloped upper front plate (27 deg)"));
    camoPart(plate(h, "Lower nose plate", 0.0f, 2.2f, 0.10f, 3.1f, 0.78f, 2.9f, 0.30f, camoDark, "Steep lower front plate"));
    camoPart(b.part(h, "Rear plate", P::Cube, {2.6f, 0.9f, 0.12f}, {0, 0, 0}, {0, 0.85f, -2.9f}, camo, "Rear hull plate"));
    camoPart(b.part(h, "Engine deck", P::Cube, {2.4f, 0.14f, 1.7f}, {0, 0, 0}, {0, 1.47f, -1.95f}, camo, "Raised engine compartment cover"));
    grille_ = b.part(h, "Exhaust grille", P::Cube, {1.5f, 0.05f, 0.9f}, {0, 0, 0}, {0, 1.56f, -2.0f}, hot, "Engine exhaust grille: hottest hull part in thermal view");
    grille_->animation = "Heat follows the engine load (idle 0.55, full throttle 0.95)";
    b.part(h, "Louvre front", P::Cube, {1.5f, 0.03f, 0.08f}, {0, 0, 0}, {0, 1.58f, -1.55f}, steel, "Air intake louvre bar");
    b.part(h, "Louvre rear", P::Cube, {1.5f, 0.03f, 0.08f}, {0, 0, 0}, {0, 1.58f, -2.45f}, steel, "Air intake louvre bar");
    for (int s = -1; s <= 1; s += 2) {
        b.part(h, s < 0 ? "Exhaust pipe R" : "Exhaust pipe L", P::Cylinder, {0.13f, 0.13f, 0.5f}, {0, 0, 0}, {0.9f * s, 1.0f, -3.1f}, hot, "Exhaust stub (hot)");
        b.part(h, s < 0 ? "Front fender R" : "Front fender L", P::Cube, {0.7f, 0.05f, 1.3f}, {8, 0, 0}, {1.1f * s, 1.18f, 2.55f}, camoDark, "Mud guard over the idler");
        headlight_[s < 0 ? 1 : 0] = b.part(h, s < 0 ? "Headlight R" : "Headlight L", P::Cube, {0.22f, 0.16f, 0.14f}, {4, 0, 0}, {1.15f * s, 1.3f, 3.05f}, lampOff, "Practical headlight (on while driving)");
        b.part(h, s < 0 ? "Tail light R" : "Tail light L", P::Cube, {0.14f, 0.1f, 0.06f}, {0, 0, 0}, {1.1f * s, 1.15f, -2.98f}, tailRed, "Rear marker light");
    }
    for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 4; ++k)
            camoPart(b.part(h, "Side skirt", P::Cube, {0.07f, 0.5f, 1.2f}, {0, 0, 0}, {1.47f * s, 0.95f, -2.0f + 1.25f * k}, camoDark, "Side armour skirt panel (covers the upper wheels)"));
    b.part(h, "Driver hatch", P::Cylinder, {0.62f, 0.62f, 0.1f}, {90, 0, 0}, {0, 1.5f, 1.35f}, steel, "Driver's hatch");
    for (int k = -1; k <= 1; ++k)
        b.part(h, "Driver periscope", P::Cube, {0.18f, 0.08f, 0.06f}, {0, 0, 0}, {0.3f * k, 1.5f, 1.72f}, glass, "Driver vision block");

    // --- tracks, wheels ----------------------------------------------------------------------------------------
    const float roadZ0 = -1.9f, roadDz = 0.65f;
    for (int side = 0; side < 2; ++side) {
        const float sx = side == 0 ? 1.0f : -1.0f;
        const char* sn = side == 0 ? "L" : "R";
        char nm[64];
        std::snprintf(nm, sizeof(nm), "Track %s", sn);
        SceneNode* tg = trackGroup_[side] = b.group(h, nm, {sx * 0.5f * kTrackSeparation, 0, 0}, {0, 0, 0}, "Track assembly of one side");
        tg->animation = "40 links placed every frame from their arc-length parameter on the closed track path";
        for (int i = 0; i < kLinksPerSide; ++i) {
            std::snprintf(nm, sizeof(nm), "Track link %s%02d", sn, i);
            links_[side][i] = b.part(tg, nm, P::Cube, {0.55f, 0.08f, 0.33f}, {0, 0, 0}, {0, 0.05f, 0}, trackMat, "Track link (steel shoe)");
            links_[side][i]->animation = "Moves along the track path at the track speed (straight runs + semicircles)";
            links_[side][i]->explodable = false;
        }
        auto wheel = [&](const char* label, float z, float y, float radius, float width, bool sprocket) {
            std::snprintf(nm, sizeof(nm), "%s %s", label, sn);
            SceneNode* g = b.group(tg, nm, {0, y, z}, {0, 90, 0}, "Spinning wheel assembly");
            g->animation = "Spins with the track (angle = belt travel / radius)";
            roadWheels_[side].push_back(g);
            wheelRadius_[side].push_back(radius);
            b.part(g, "Tyre", sprocket ? P::Cylinder : P::Cylinder, {2 * radius, 2 * radius, width}, {0, 0, 0}, {0, 0, 0}, sprocket ? steel : rubber, sprocket ? "Sprocket ring" : "Road wheel rim and rubber tyre");
            b.part(g, "Hub", P::Cylinder, {radius, radius, width + 0.06f}, {0, 0, 0}, {0, 0, 0}, steel, "Hub cap");
            b.part(g, "Spoke", P::Cube, {2 * radius * 0.9f, 0.07f, width + 0.08f}, {0, 0, 0}, {0, 0, 0}, gunMetal, "Spoke bar (shows the rotation)");
        };
        for (int i = 0; i < kRoadWheels; ++i) wheel("Road wheel", roadZ0 + roadDz * i, 0.44f, 0.32f, 0.4f, false);
        wheel("Drive sprocket", tankpath::kSprocketZ, tankpath::kCentreY, 0.38f, 0.45f, true);
        wheel("Idler", tankpath::kIdlerZ, tankpath::kCentreY, 0.38f, 0.45f, false);
        for (int i = 0; i < 3; ++i) {
            std::snprintf(nm, sizeof(nm), "Return roller %s", sn);
            SceneNode* rr = b.part(tg, nm, P::Cylinder, {0.18f, 0.18f, 0.2f}, {0, 90, 0}, {0, 0.9f, -1.4f + 1.4f * i}, rubber, "Return roller carrying the upper track run");
            rr->animation = "Spins with the track";
            returnRollers_[side].push_back(rr);
        }
    }

    // --- turret ------------------------------------------------------------------------------------------------
    turret_ = b.group(h, "Turret yaw", {0, kTurretY, kTurretZ}, {0, 0, 0}, "Turret ring: yaw about the hull Y axis");
    turret_->animation = "Stabilised: the turret's world yaw is held while the hull turns; slews 40 deg/s";
    SceneNode* t = turret_;
    camoPart(b.part(t, "Turret body", P::Cube, {2.3f, 0.7f, 2.8f}, {0, 0, 0}, {0, 0.35f, -0.1f}, camo, "Cast turret body"));
    camoPart(b.part(t, "Turret cheek L", P::Cube, {1.0f, 0.66f, 1.2f}, {0, -22, 0}, {0.95f, 0.34f, 1.15f}, camo, "Angled front armour module"));
    camoPart(b.part(t, "Turret cheek R", P::Cube, {1.0f, 0.66f, 1.2f}, {0, 22, 0}, {-0.95f, 0.34f, 1.15f}, camo, "Angled front armour module"));
    camoPart(b.part(t, "Turret front plate", P::Cube, {1.0f, 0.62f, 0.4f}, {0, 0, 0}, {0, 0.34f, 1.5f}, camoDark, "Armour behind the mantlet"));
    camoPart(b.part(t, "Turret roof", P::Cube, {2.0f, 0.1f, 2.2f}, {0, 0, 0}, {0, 0.72f, -0.1f}, camo, "Roof plate"));
    camoPart(b.part(t, "Bustle", P::Cube, {2.2f, 0.62f, 1.3f}, {0, 0, 0}, {0, 0.34f, -1.95f}, camo, "Turret bustle (ammunition compartment)"));
    camoPart(b.part(t, "Bustle roof", P::Cube, {2.2f, 0.08f, 1.3f}, {6, 0, 0}, {0, 0.68f, -1.95f}, camoDark, "Sloped bustle roof"));
    b.part(t, "Stowage basket", P::Cube, {2.3f, 0.3f, 0.08f}, {0, 0, 0}, {0, 0.5f, -2.65f}, steel, "Rear stowage rack");
    b.part(t, "Bedroll A", P::Cylinder, {0.2f, 0.2f, 1.0f}, {0, 90, 0}, {0.5f, 0.62f, -2.72f}, camoDark, "Stowed camouflage net roll");
    b.part(t, "Bedroll B", P::Cylinder, {0.2f, 0.2f, 1.0f}, {0, 90, 0}, {-0.5f, 0.62f, -2.72f}, camoDark, "Stowed camouflage net roll");
    b.part(t, "Gunner sight box", P::Cube, {0.45f, 0.28f, 0.55f}, {0, 0, 0}, {-0.55f, 0.9f, 0.7f}, steel, "Gunner's stabilised sight housing");
    b.part(t, "Gunner sight lens", P::Sphere, {0.18f, 0.18f, 0.12f}, {0, 0, 0}, {-0.55f, 0.92f, 1.0f}, glass, "Sight objective lens");
    b.part(t, "Cupola ring", P::Cylinder, {0.85f, 0.85f, 0.2f}, {90, 0, 0}, {0.65f, 0.86f, -0.55f}, steel, "Commander's cupola ring");
    for (int i = 0; i < 6; ++i) {
        const float a = radians(60.0f * i);
        b.part(t, "Cupola vision block", P::Cube, {0.16f, 0.1f, 0.1f}, {0, degrees(a), 0},
               {0.65f + 0.4f * std::sin(a), 0.95f, -0.55f + 0.4f * std::cos(a)}, glass, "Commander vision block");
    }
    b.part(t, "Loader hatch", P::Cylinder, {0.75f, 0.75f, 0.08f}, {90, 0, 0}, {-0.65f, 0.76f, -0.75f}, steel, "Loader's hatch");
    b.part(t, "Loader hatch handle", P::Cube, {0.3f, 0.05f, 0.05f}, {0, 0, 0}, {-0.65f, 0.82f, -0.75f}, gunMetal, "Hatch handle");

    // commander's machine gun: yaw ring -> pitch -> gun
    mgYaw_ = b.group(t, "MG ring", {0.65f, 0.98f, -0.55f}, {0, 0, 0}, "Rotating machine-gun mount on the cupola");
    mgYaw_->animation = "Slews after the aim direction (90 deg/s), sweeps while idle";
    b.part(mgYaw_, "MG pintle", P::Cylinder, {0.06f, 0.06f, 0.2f}, {90, 0, 0}, {0, 0.0f, 0}, gunMetal, "Pintle post");
    mgPitch_ = b.group(mgYaw_, "MG elevation", {0, 0.1f, 0}, {0, 0, 0}, "MG elevation cradle");
    mgPitch_->animation = "Follows the main gun elevation";
    b.part(mgPitch_, "MG receiver", P::Cube, {0.12f, 0.16f, 0.7f}, {0, 0, 0}, {0, 0, 0.25f}, gunMetal, "Machine gun body");
    b.part(mgPitch_, "MG barrel", P::Cylinder, {0.04f, 0.04f, 0.7f}, {0, 0, 0}, {0, 0, 0.85f}, gunMetal, "Machine gun barrel");
    b.part(mgPitch_, "MG flash hider", P::Cylinder, {0.07f, 0.07f, 0.1f}, {0, 0, 0}, {0, 0, 1.22f}, steel, "Flash hider");
    b.part(mgPitch_, "MG ammo box", P::Cube, {0.22f, 0.18f, 0.3f}, {0, 0, 0}, {-0.17f, -0.03f, 0.1f}, camoDark, "Ammunition box");

    // smoke grenade launchers (2 x 4)
    for (int s = -1; s <= 1; s += 2) {
        b.part(t, s < 0 ? "Smoke launcher mount R" : "Smoke launcher mount L", P::Cube, {0.12f, 0.2f, 0.6f}, {0, 0, 0}, {1.18f * s, 0.5f, 0.6f}, steel, "Smoke launcher rack");
        for (int k = 0; k < 4; ++k)
            b.part(t, "Smoke grenade tube", P::Cylinder, {0.09f, 0.09f, 0.22f}, {-55, 30.0f * s, 0}, {1.22f * s, 0.62f, 0.4f + 0.14f * k}, gunMetal, "Smoke grenade launcher tube");
    }

    // antennas: mounts on the turret, whips sway with acceleration
    const float antLen[2] = {2.0f, 1.4f};
    const float antX[2] = {-0.9f, 0.9f}, antZ[2] = {-2.0f, -2.2f};
    for (int i = 0; i < 2; ++i) {
        b.part(t, "Antenna mount", P::Cylinder, {0.16f, 0.16f, 0.12f}, {90, 0, 0}, {antX[i], 0.72f, antZ[i]}, steel, "Antenna base");
        antenna_[i] = b.group(t, i == 0 ? "Antenna A pivot" : "Antenna B pivot", {antX[i], 0.78f, antZ[i]}, {0, 0, 0}, "Flexible antenna root");
        antenna_[i]->animation = "Bends opposite to the acceleration (damped spring), small wind flutter";
        b.part(antenna_[i], i == 0 ? "Whip antenna A" : "Whip antenna B", P::Cylinder, {0.03f, 0.03f, antLen[i]}, {-90, 0, 0}, {0, 0.5f * antLen[i], 0}, gunMetal, "Radio whip antenna");
    }

    // gun: pitch -> mantlet -> recoiling barrel
    gunPitchNode_ = b.group(t, "Gun pitch", {0, kGunPivotY, kGunPivotZ}, {0, 0, 0}, "Gun trunnion: elevation -8 .. +20 deg");
    gunPitchNode_->animation = "Elevation slews at 20 deg/s";
    camoPart(b.part(gunPitchNode_, "Gun mantlet", P::Cube, {0.8f, 0.58f, 0.45f}, {0, 0, 0}, {0, 0, 0.05f}, camoDark, "Gun mantlet"));
    b.part(gunPitchNode_, "Coaxial MG", P::Cube, {0.09f, 0.09f, 0.5f}, {0, 0, 0}, {0.38f, -0.12f, 0.35f}, gunMetal, "Coaxial machine gun");
    recoilNode_ = b.group(gunPitchNode_, "Barrel recoil", {0, 0, kBarrelBaseZ}, {0, 0, 0}, "Barrel slide");
    recoilNode_->animation = "Slides back 0.35 m when firing, returns over 0.6 s";
    barrelNode_ = b.part(recoilNode_, "Gun barrel", P::Cylinder, {0.17f, 0.17f, 4.2f}, {0, 0, 0}, {0, 0, 2.1f}, gunMetal, "120 mm smoothbore barrel");
    barrelNode_->animation = "Heats up with every shot";
    b.part(recoilNode_, "Thermal sleeve", P::Cylinder, {0.24f, 0.24f, 1.5f}, {0, 0, 0}, {0, 0, 1.0f}, steel, "Barrel thermal sleeve");
    b.part(recoilNode_, "Fume extractor", P::Cylinder, {0.27f, 0.27f, 0.5f}, {0, 0, 0}, {0, 0, 2.7f}, steel, "Bore evacuator");
    b.part(recoilNode_, "Muzzle brake", P::Cylinder, {0.23f, 0.23f, 0.34f}, {0, 0, 0}, {0, 0, 4.3f}, steel, "Muzzle brake");
    muzzleFlashNode_ = b.part(recoilNode_, "Muzzle flash", P::Sphere, {0.5f, 0.5f, 0.9f}, {0, 0, 0}, {0, 0, kMuzzleZ - kBarrelBaseZ + 0.3f},
                              asLight(makeMaterial({1.0f, 0.65f, 0.25f}, 1.0f, Surface::Plain, 0.5f, 0.0f, 3.0f), 0.0f, 22.0f), "Muzzle flash (visible for 0.08 s)");
    muzzleFlashNode_->animation = "Flashes for 0.08 s per shot";
    muzzleFlashNode_->local.scale = {0.0001f, 0.0001f, 0.0001f};
    muzzleFlashNode_->explodable = false;
    muzzleFlashNode_->obstacle = false;

    place(layout::kMotorPool.x, layout::kMotorPool.z, layout::kTankParkYawDeg);
    turretYaw_ = turretCmd_ = yaw_;
    mgYawWorld_ = yaw_;
    for (int i = 0; i < 4; ++i) applyTerrain(0.016f);
    animateTracks(0.0f);
}

void Tank::place(float x, float z, float yawDeg) {
    x_ = x; z_ = z; yaw_ = radians(yawDeg);
    turretYaw_ = turretCmd_ = yaw_;
    vl_ = vr_ = 0.0f;
    // snap the tilt to the terrain
    pitchTerrain_ = rollTerrain_ = 0.0f;
    applyTerrain(1.0f);
    pitchWorld_ = worldOf(gunPitchNode_);
    turretWorld_ = worldOf(turret_);
}

// ---------------------------------------------------------------- simulation

bool Tank::blocked(float x, float z, float yaw, const std::vector<Box>* obstacles) const {
    if (!obstacles) return false;
    const float y = root_->local.translation.y;
    for (const Box& b : *obstacles) {
        if (b.max.y < y + 0.35f || b.min.y > y + 2.2f) continue;
        if (obbHitsBox(x, z, yaw, kHalfWid, kHalfLen, b)) return true;
    }
    return false;
}

void Tank::applyTerrain(float dt) {
    const float s = std::sin(yaw_), c = std::cos(yaw_);
    const Vec3 f{s, 0, c}, l{c, 0, -s};
    const Vec3 p{x_, 0, z_};
    auto hAt = [&](float zf, float xl) { const Vec3 q = p + f * zf + l * xl; return terrainHeight(q.x, q.z); };
    const float hFL = hAt(kCornerZ, 1.1f), hFR = hAt(kCornerZ, -1.1f), hBL = hAt(-kCornerZ, 1.1f), hBR = hAt(-kCornerZ, -1.1f);
    const float meanH = 0.25f * (hFL + hFR + hBL + hBR);
    const float pitchT = std::atan2(0.5f * (hFL + hFR) - 0.5f * (hBL + hBR), 2.0f * kCornerZ);
    const float rollT = std::atan2(0.5f * (hFL + hBL) - 0.5f * (hFR + hBR), 2.2f);
    const float k = std::min(1.0f, 8.0f * dt);
    pitchTerrain_ += (pitchT - pitchTerrain_) * k;
    rollTerrain_ += (rollT - rollTerrain_) * k;
    root_->local.translation = {x_, meanH + heave_, z_};
    root_->local.rotationDeg = {-degrees(pitchTerrain_), degrees(yaw_), degrees(rollTerrain_) + 1.2f * shake_ * std::sin(37.0f * time_)};
}

void Tank::updateTurret(const WorldContext& w, const ControlInput* in) {
    const float dt = w.dt;
    if (in) {
        turretCmd_ -= radians(in->aimYaw * kTurretSlewDeg * dt + in->aimYawDelta);
        gunCmd_ = clampf(gunCmd_ + in->aimPitch * kGunSlewDeg * dt + in->aimPitchDelta, kGunMinDeg, kGunMaxDeg);
    } else {
        // idle: the turret slowly scans +-70 deg around the hull heading, gun slightly raised
        float rel = wrapPi(turretCmd_ - yaw_);
        if (rel > radians(70.0f)) scanDir_ = -1.0f;
        if (rel < -radians(70.0f)) scanDir_ = 1.0f;
        turretCmd_ += scanDir_ * radians(8.0f) * dt;
        gunCmd_ += clampf(2.0f - gunCmd_, -3.0f * dt, 3.0f * dt);
    }
    // the command never runs more than 45 deg ahead of the turret (no wind-up)
    float diff = clampf(wrapPi(turretCmd_ - turretYaw_), -radians(45.0f), radians(45.0f));
    turretCmd_ = turretYaw_ + diff;
    turretYaw_ += clampf(diff, -radians(kTurretSlewDeg) * dt, radians(kTurretSlewDeg) * dt);
    turretYaw_ = wrapPi(turretYaw_);
    gunCmd_ = clampf(gunCmd_, gunPitch_ - 10.0f, gunPitch_ + 10.0f);
    gunCmd_ = clampf(gunCmd_, kGunMinDeg, kGunMaxDeg);
    gunPitch_ = clampf(gunPitch_ + clampf(gunCmd_ - gunPitch_, -kGunSlewDeg * dt, kGunSlewDeg * dt), kGunMinDeg, kGunMaxDeg);
}

void Tank::update(const WorldContext& w, const ControlInput* in) {
    const float dt = clampf(w.dt, 0.0f, 0.05f);
    time_ = w.time;
    possessed_ = in != nullptr;
    WorldContext wc = w;
    wc.dt = dt;

    // ---- skid-steer drive
    float tl = 0.0f, tr = 0.0f;
    if (in) {
        const float top = kMaxForward * (in->boost ? kBoost : 1.0f);
        const float f = in->forward >= 0.0f ? in->forward * top : in->forward * kMaxReverse;
        const float k = 1.2f * in->turn;
        tl = clampf(f + k, -kMaxReverse, top);
        tr = clampf(f - k, -kMaxReverse, top);
    }
    auto approach = [&](float v, float target) {
        const bool speeding = std::fabs(target) > std::fabs(v) && target * v >= 0.0f;
        const float step = (speeding ? 3.5f : 7.0f) * dt;
        return v + clampf(target - v, -step, step);
    };
    vl_ = approach(vl_, tl);
    vr_ = approach(vr_, tr);

    const float v = 0.5f * (vl_ + vr_);
    yawRate_ = (vr_ - vl_) / kTrackSeparation;
    float nyaw = yaw_ + yawRate_ * dt;
    float nx = x_ + std::sin(nyaw) * v * dt, nz = z_ + std::cos(nyaw) * v * dt;
    // perimeter: stay inside r <= kPerimeterRadius - 3
    const float rr = std::sqrt(nx * nx + nz * nz);
    if (rr > kMaxRadius) { nx *= kMaxRadius / rr; nz *= kMaxRadius / rr; }
    if (!blocked(nx, nz, nyaw, w.obstacles) || blocked(x_, z_, yaw_, w.obstacles)) {
        x_ = nx; z_ = nz; yaw_ = nyaw;
    } else if (!blocked(x_, z_, nyaw, w.obstacles)) {   // pivot in place
        yaw_ = nyaw; vl_ -= v; vr_ -= v;   // keep only the turning part of the track speeds
    } else if (!blocked(nx, nz, yaw_, w.obstacles)) {   // slide straight
        x_ = nx; z_ = nz; vl_ = vr_ = v;   // no turning into the obstacle
    } else {                                              // stopped by the obstacle
        vl_ = vr_ = 0.0f;
    }
    yaw_ = wrapPi(yaw_);

    const float vNow = 0.5f * (vl_ + vr_);
    accel_ = dt > 1e-5f ? (vNow - prevSpeed_) / dt : 0.0f;
    prevSpeed_ = vNow;
    lightsOn_ = std::fabs(vl_) + std::fabs(vr_) > 0.5f;

    // ---- turret, gun, firing
    updateTurret(wc, in);
    reload_ = std::max(0.0f, reload_ - dt);
    recoilT_ += dt;
    flashT_ += dt;
    if (in && in->firePressed && reload_ <= 0.0f && rounds_ > 0) {
        reload_ = kReload;
        --rounds_;
        recoilT_ = 0.0f;
        flashT_ = 0.0f;
        barrelHeat_ = 1.0f;
        dynPitchVel_ -= 0.25f;   // hull rocks back (nose up)
        shake_ = std::min(1.0f, shake_ + 0.35f);
        if (w.combat) {
            pitchWorld_ = worldOf(gunPitchNode_);
            const Vec3 m = muzzlePosition(), d = muzzleDirection();
            w.combat->fireShell(m, d, kMuzzleSpeed, ownerId);
            w.combat->muzzleFlash(m, d, 1.6f);
            w.combat->emitDust({m.x + d.x * 1.5f, terrainHeight(m.x, m.z) + 0.1f, m.z + d.z * 1.5f}, 2.5f, 22);
        }
    }
    recoilNow_ = recoilT_ < 0.06f ? kRecoilDistance * recoilT_ / 0.06f
                                  : kRecoilDistance * (1.0f - smooth01((recoilT_ - 0.06f) / kRecoilReturn));
    barrelHeat_ = std::max(0.0f, barrelHeat_ - dt / 40.0f);

    // ---- suspension
    const float aTarget = clampf(-0.012f * accel_, -0.06f, 0.06f);
    spring(dynPitch_, dynPitchVel_, aTarget, 2.0f * PI * 1.6f, 0.35f, dt);
    shake_ = std::max(0.0f, shake_ - 2.0f * dt);
    const float speedFrac = clampf(std::fabs(vNow) / kMaxForward, 0.0f, 1.0f);
    heave_ = 0.0025f * std::sin(2.0f * PI * 11.0f * time_) +
             speedFrac * 0.012f * std::sin(2.0f * PI * 2.2f * (phase_[0] + phase_[1]) * 0.5f) +
             shake_ * 0.03f * std::sin(41.0f * time_);

    // ---- node transforms
    applyTerrain(dt);
    hull_->local.rotationDeg = {degrees(dynPitch_), 0.0f, 0.0f};
    animateTracks(dt);
    const float rel = wrapPi(turretYaw_ - yaw_);
    turret_->local.rotationDeg = {0.0f, degrees(rel), 0.0f};
    gunPitchNode_->local.rotationDeg = {-gunPitch_, 0.0f, 0.0f};
    recoilNode_->local.translation = {0.0f, 0.0f, kBarrelBaseZ - recoilNow_};

    // commander MG: follows the aim direction at 90 deg/s, sweeps a little while idle
    const float mgTarget = possessed_ ? turretYaw_ : turretYaw_ + radians(50.0f) * std::sin(0.7f * time_);
    mgYawWorld_ += clampf(wrapPi(mgTarget - mgYawWorld_), -radians(90.0f) * dt, radians(90.0f) * dt);
    mgYaw_->local.rotationDeg = {0.0f, degrees(wrapPi(mgYawWorld_ - turretYaw_)), 0.0f};
    mgPitch_->local.rotationDeg = {-std::max(gunPitch_, 0.0f), 0.0f, 0.0f};

    // antennas: lean opposite to the acceleration; lateral from v * yaw rate
    const float aLat = vNow * yawRate_;
    spring(antSwayX_, antVelX_, clampf(-0.025f * aLat, -0.15f, 0.15f) + 0.01f * std::sin(1.9f * time_), 2.0f * PI * 2.2f, 0.12f, dt);
    spring(antSwayZ_, antVelZ_, clampf(-0.03f * accel_, -0.2f, 0.2f) + 0.012f * std::sin(2.3f * time_ + 1.0f), 2.0f * PI * 2.2f, 0.12f, dt);
    for (int i = 0; i < 2; ++i) {
        const float tx = antSwayX_ * (i == 0 ? 1.0f : 0.8f), tz = antSwayZ_ * (i == 0 ? 1.0f : 0.8f);
        const float x2 = tx * std::cos(rel) - tz * std::sin(rel), z2 = tx * std::sin(rel) + tz * std::cos(rel);
        antenna_[i]->local.rotationDeg = {degrees(z2), 0.0f, degrees(-x2)};
    }

    // ---- materials: heat, lights, flash, scorch
    const float load = clampf(std::fabs(vNow) / kMaxForward + 0.4f * std::fabs(yawRate_), 0.0f, 1.0f);
    grille_->material.heat = 0.55f + 0.4f * load;
    barrelNode_->material.heat = 0.35f + 0.5f * barrelHeat_;
    for (int s = 0; s < 2; ++s)
        for (SceneNode* l : links_[s]) l->material.heat = 0.30f + 0.15f * speedFrac;
    for (SceneNode* hl : headlight_) {
        hl->material.emissive = lightsOn_ ? 2.0f : 0.0f;
        hl->material.lightIntensity = lightsOn_ ? 9.0f : 0.0f;
        hl->material.lightRange = 26.0f;
        hl->material.lightConeDeg = 30.0f;
    }
    const bool flash = flashT_ < 0.08f;
    const float fs = flash ? 1.0f : 0.0001f;
    muzzleFlashNode_->local.scale = {0.5f * fs, 0.5f * fs, 0.9f * fs};
    muzzleFlashNode_->material.lightIntensity = flash ? 40.0f : 0.0f;
    for (size_t i = 0; i < camoParts_.size(); ++i)
        camoParts_[i]->material.baseColor = camoBase_[i] * (1.0f - 0.6f * scorch_);

    // dust thrown up by the tracks
    if (w.combat && std::fabs(vNow) > 1.0f) {
        dustAcc_ += std::fabs(vNow) * dt;
        if (dustAcc_ > 2.0f) {
            dustAcc_ = 0.0f;
            const Vec3 fwd{std::sin(yaw_), 0, std::cos(yaw_)};
            const Vec3 q = Vec3{x_, 0, z_} - fwd * (vNow > 0 ? 3.2f : -3.2f);
            w.combat->emitDust({q.x, terrainHeight(q.x, q.z) + 0.15f, q.z}, 0.8f, 3);
        }
    }

    turretWorld_ = worldOf(turret_);
    pitchWorld_ = worldOf(gunPitchNode_);
}

void Tank::animateTracks(float dt) {
    const float speeds[2] = {vl_, vr_};
    const float P = tankpath::perimeter();
    for (int side = 0; side < 2; ++side) {
        phase_[side] += speeds[side] * dt;
        if (phase_[side] > 1000.0f * P) phase_[side] = std::fmod(phase_[side], P);
        const float base = std::fmod(phase_[side], P);
        for (int i = 0; i < kLinksPerSide; ++i) {
            float z, y, phi;
            tankpath::point(base + P * static_cast<float>(i) / kLinksPerSide, z, y, phi);
            SceneNode* n = links_[side][i];
            n->local.translation = {0.0f, y, z};
            n->local.rotationDeg = {degrees(phi), 0.0f, 0.0f};
        }
        for (size_t i = 0; i < roadWheels_[side].size(); ++i)
            roadWheels_[side][i]->local.rotationDeg = {0.0f, 90.0f, std::fmod(degrees(phase_[side] / wheelRadius_[side][i]), 360.0f)};
        for (SceneNode* rr : returnRollers_[side])
            rr->local.rotationDeg = {0.0f, 90.0f, std::fmod(degrees(phase_[side] / 0.09f), 360.0f)};
    }
}

// ---------------------------------------------------------------- queries

Vec3 Tank::muzzlePosition() const { return transformPoint(pitchWorld_, {0.0f, 0.0f, kMuzzleZ - recoilNow_}); }
Vec3 Tank::muzzleDirection() const { return normalize(transformDirection(pitchWorld_, {0.0f, 0.0f, 1.0f})); }

CameraHint Tank::camera(bool firstPerson) const {
    CameraHint h;
    if (firstPerson) {
        const Vec3 d = muzzleDirection();
        h.eye = transformPoint(pitchWorld_, {0.0f, 0.32f, 0.1f});
        h.target = h.eye + d * 60.0f;
        h.fovDeg = 28.0f;
        h.sight = true;
    } else {
        const Vec3 o = transformPoint(turretWorld_, {0, 0.6f, 0});
        const Vec3 d{std::sin(turretYaw_), 0.0f, std::cos(turretYaw_)};
        h.eye = o - d * 9.0f + Vec3{0.0f, 3.2f, 0.0f};
        h.target = o + d * 12.0f + Vec3{0.0f, 0.8f, 0.0f};
        h.fovDeg = 55.0f;
    }
    return h;
}

std::string Tank::status() const {
    char buf[160];
    const int hdg = (static_cast<int>(std::lround(degrees(yaw_))) % 360 + 360) % 360;
    const int tur = (static_cast<int>(std::lround(degrees(turretYaw_))) % 360 + 360) % 360;
    char gun[24];
    if (reload_ <= 0.0f) std::snprintf(gun, sizeof(gun), "LOADED");
    else std::snprintf(gun, sizeof(gun), "RELOAD %.1f S", reload_);
    std::snprintf(buf, sizeof(buf), "SPEED %.1f M/S  HDG %03d  TURRET %03d  GUN %+03d  %s  ROUNDS %d", speed(), hdg, tur,
                  static_cast<int>(std::lround(gunPitch_)), gun, rounds_);
    return buf;
}

std::vector<Box> Tank::dynamicObstacles() const {
    const float s = std::fabs(std::sin(yaw_)), c = std::fabs(std::cos(yaw_));
    const float hx = c * kHalfWid + s * kHalfLen, hz = s * kHalfWid + c * kHalfLen;
    const float y = root_->local.translation.y;
    return {Box{{x_ - hx, y, z_ - hz}, {x_ + hx, y + 1.5f, z_ + hz}}};
}

bool Tank::onBlast(const Vec3& point, float power) {
    const float d = length(point - root_->local.translation);
    if (d > 10.0f + 6.0f * power) return false;
    const float k = power / (1.0f + 0.08f * d * d);
    shake_ = std::min(1.0f, shake_ + 1.5f * k);
    dynPitchVel_ -= 0.08f * k;
    scorch_ = std::min(0.5f, scorch_ + 0.15f * k);
    return true;
}

std::unique_ptr<Controllable> makeTank() { return std::make_unique<Tank>(); }

}  // namespace ts
