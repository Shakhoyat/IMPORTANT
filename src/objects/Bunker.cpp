#include "objects/Bunker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "objects/BaseLayout.h"
#include "objects/Builder.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

using P = PrimitiveType;
namespace L = layout;

constexpr float kF = L::kBunkerFloorY;     // underground floor
constexpr float kC = L::kBunkerCeilingY;   // ceiling
constexpr float kRoofTop = L::kBunkerRoofTopY;
constexpr float kTopZ = L::kRampTopZ, kBottomZ = L::kRampBottomZ;
constexpr float kDoorZ0 = 3.65f, kDoorZ1 = 4.0f;  // blast door leaf / front wall thickness 0.35 m
constexpr float kDoorHeight = 2.6f;
constexpr float kSlide = 1.5f;                    // leaf travel into the wall pockets
constexpr float kWallT = 0.35f;                   // trench retaining wall thickness

float smooth01(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Things the door controller animates besides the leaves.
struct Animated {
    std::vector<SceneNode*> leds;
    std::vector<SceneNode*> screens;
    std::vector<float> screenBase;
};

class BlastDoors : public Controllable {
public:
    enum class State { Closed, Opening, Open, Closing };

    BlastDoors(SceneNode* root, SceneNode* leafL, SceneNode* leafR, SceneNode* spinL, SceneNode* spinR,
               std::vector<SceneNode*> beaconLamps, SceneNode* status, Animated anim, Vec3 eye, Vec3 target)
        : root_(root), leafL_(leafL), leafR_(leafR), spin_{spinL, spinR}, beaconLamps_(std::move(beaconLamps)),
          status_(status), anim_(std::move(anim)), eye_(eye), target_(target) {
        apply(0.0f);
    }

    const char* name() const override { return "BLAST DOORS"; }
    const char* controls() const override { return "ENTER or \\  open / close the blast doors (about 4 s)"; }
    SceneNode* root() override { return root_; }

    void update(const WorldContext& w, const ControlInput* in) override {
        if (in && (in->actionPressed || in->firePressed)) {
            if (state_ == State::Closed || state_ == State::Closing) state_ = State::Opening;
            else state_ = State::Closing;
        }
        const float dt = std::max(w.dt, 0.0f);
        if (state_ == State::Opening) {
            t_ += dt / bunker::kDoorTravelSeconds;
            if (t_ >= 1.0f) { t_ = 1.0f; state_ = State::Open; }
        } else if (state_ == State::Closing) {
            t_ -= dt / bunker::kDoorTravelSeconds;
            if (t_ <= 0.0f) { t_ = 0.0f; state_ = State::Closed; }
        }
        if (moving()) spinDeg_ = std::fmod(spinDeg_ + 300.0f * dt, 360.0f);
        apply(w.time);
    }

    CameraHint camera(bool) const override {
        CameraHint h;
        h.eye = eye_;
        h.target = target_;
        h.fovDeg = 62.0f;
        return h;
    }
    std::string status() const override {
        const char* s = state_ == State::Open ? "OPEN" : state_ == State::Closed ? "CLOSED"
                        : state_ == State::Opening ? "OPENING" : "CLOSING";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "BLAST DOORS %s %d%%", s, static_cast<int>(std::lround(100.0f * smooth01(t_))));
        return buf;
    }
    Vec3 position() const override { return {0.0f, kF + 1.3f, 0.5f * (kDoorZ0 + kDoorZ1)}; }
    std::vector<Box> dynamicObstacles() const override {
        const float e = smooth01(t_) * kSlide, wd = bunker::kLeafWidth;
        return {Box{{-wd - e, kF, kDoorZ0}, {-e, kF + kDoorHeight, kDoorZ1}},
                Box{{e, kF, kDoorZ0}, {wd + e, kF + kDoorHeight, kDoorZ1}}};
    }

private:
    bool moving() const { return state_ == State::Opening || state_ == State::Closing; }

    void apply(float time) {
        const float e = smooth01(t_) * kSlide;
        leafL_->local.translation.x = -0.5f * bunker::kLeafWidth - e;
        leafR_->local.translation.x = 0.5f * bunker::kLeafWidth + e;
        for (int i = 0; i < 2; ++i) spin_[i]->local.rotationDeg.y = i == 0 ? spinDeg_ : -spinDeg_ + 90.0f;
        const float on = moving() ? 1.0f : 0.0f;
        for (SceneNode* n : beaconLamps_) n->material.emissive = on;
        if (state_ == State::Open) {
            status_->material.baseColor = {0.05f, 1.0f, 0.15f};
        } else {
            status_->material.baseColor = {1.0f, 0.05f, 0.03f};
        }
        // Server rack LEDs: deterministic pseudo-random blinking at 5 Hz; screens breathe slowly.
        const int tick = static_cast<int>(std::floor(time * 5.0f));
        for (size_t i = 0; i < anim_.leds.size(); ++i) {
            const float s = std::sin(static_cast<float>(i) * 12.9898f + static_cast<float>(tick) * 78.233f) * 43758.5453f;
            anim_.leds[i]->material.emissive = (s - std::floor(s)) > 0.45f ? 1.0f : 0.08f;
        }
        for (size_t i = 0; i < anim_.screens.size(); ++i)
            anim_.screens[i]->material.emissive = anim_.screenBase[i] * (0.9f + 0.1f * std::sin(1.3f * time + static_cast<float>(i)));
    }

    SceneNode* root_;
    SceneNode* leafL_;
    SceneNode* leafR_;
    SceneNode* spin_[2];
    std::vector<SceneNode*> beaconLamps_;
    SceneNode* status_;
    Animated anim_;
    Vec3 eye_, target_;
    State state_ = State::Open;
    float t_ = 1.0f, spinDeg_ = 0.0f;
};

}  // namespace

BunkerRig buildBunker() {
    Builder b("Command Bunker", "BNK", ShadingModel::Flat);
    BunkerRig rig;
    rig.root = b.root("Command Bunker", {0, 0, 0}, "Underground command bunker with its surface installations");
    SceneNode* r = rig.root.get();
    WalkSpace& walk = rig.walk;
    Animated anim;

    // ---------------------------------------------------------------- materials (night: dark albedos)
    using S = Surface;
    const Material concreteOut = makeMaterial({0.30f, 0.30f, 0.28f}, 0.28f, S::Concrete, 0.88f, 0.0f);
    const Material concreteIn = makeMaterial({0.36f, 0.36f, 0.33f}, 0.30f, S::Concrete, 0.85f, 0.0f);
    const Material coping = makeMaterial({0.27f, 0.27f, 0.25f}, 0.26f, S::Concrete, 0.80f, 0.0f);
    const Material floorMat = makeMaterial({0.19f, 0.19f, 0.18f}, 0.28f, S::Concrete, 0.65f, 0.0f);
    const Material steelPlate = makeMaterial({0.24f, 0.26f, 0.25f}, 0.35f, S::SteelPlate, 0.50f, 0.80f);
    const Material hazard = makeMaterial({0.60f, 0.50f, 0.05f}, 0.30f, S::Hazard, 0.60f, 0.0f);
    const Material paintOlive = makeMaterial({0.17f, 0.22f, 0.15f}, 0.45f, S::Paint, 0.50f, 0.5f);
    const Material paintGrey = makeMaterial({0.30f, 0.31f, 0.32f}, 0.35f, S::Paint, 0.55f, 0.4f);
    const Material paintYellow = makeMaterial({0.55f, 0.44f, 0.05f}, 0.30f, S::Paint, 0.60f, 0.2f);
    const Material paintRed = makeMaterial({0.45f, 0.07f, 0.05f}, 0.32f, S::Paint, 0.55f, 0.3f);
    const Material paintBlue = makeMaterial({0.08f, 0.16f, 0.34f}, 0.30f, S::Paint, 0.55f, 0.3f);
    const Material galv = makeMaterial({0.50f, 0.52f, 0.55f}, 0.28f, S::Metal, 0.40f, 0.90f);
    const Material darkMetal = makeMaterial({0.11f, 0.11f, 0.12f}, 0.35f, S::Metal, 0.55f, 0.80f);
    const Material rubber = makeMaterial({0.04f, 0.04f, 0.04f}, 0.30f, S::Rubber, 0.80f, 0.0f);
    const Material wood = makeMaterial({0.30f, 0.22f, 0.13f}, 0.25f, S::Wood, 0.75f, 0.0f);
    const Material fabric = makeMaterial({0.16f, 0.19f, 0.13f}, 0.35f, S::Paint, 0.85f, 0.0f);
    const Material earth = makeMaterial({0.17f, 0.14f, 0.10f}, 0.15f, S::Ground, 0.95f, 0.0f);
    const Material sand = makeMaterial({0.40f, 0.36f, 0.26f}, 0.27f, S::Sand, 0.95f, 0.0f);
    const Material screenGreen = makeMaterial({0.15f, 0.95f, 0.35f}, 0.45f, S::Screen, 0.30f, 0.0f, 0.85f);
    const Material screenAmber = makeMaterial({1.00f, 0.60f, 0.12f}, 0.45f, S::Screen, 0.30f, 0.0f, 0.80f);
    const Material ledMat = makeMaterial({0.20f, 1.00f, 0.30f}, 0.40f, S::Plain, 0.30f, 0.0f, 1.0f);
    const Material exitSign = makeMaterial({0.10f, 0.95f, 0.30f}, 0.40f, S::Plain, 0.30f, 0.0f, 0.9f);
    const Material fluorLit = asLight(makeMaterial({0.85f, 0.95f, 1.00f}, 0.42f, S::Plain, 0.40f, 0.0f, 1.0f), 1.7f, 6.5f);
    const Material fluorDim = makeMaterial({0.85f, 0.95f, 1.00f}, 0.42f, S::Plain, 0.40f, 0.0f, 0.9f);
    const Material redLit = asLight(makeMaterial({1.00f, 0.05f, 0.03f}, 0.45f, S::Glass, 0.20f, 0.0f, 1.0f), 0.9f, 3.5f);
    const Material sodium = asLight(makeMaterial({1.00f, 0.62f, 0.28f}, 0.52f, S::Glass, 0.10f, 0.0f, 1.0f), 2.4f, 6.5f);
    const Material beaconAmberLit =
        asLight(makeMaterial({1.00f, 0.55f, 0.05f}, 0.55f, S::Glass, 0.10f, 0.0f, 0.0f), 3.0f, 7.0f);
    const Material beaconAmber = makeMaterial({1.00f, 0.55f, 0.05f}, 0.55f, S::Glass, 0.10f, 0.0f, 0.0f);
    const Material statusMat =
        asLight(makeMaterial({0.05f, 1.00f, 0.15f}, 0.45f, S::Glass, 0.10f, 0.0f, 1.0f), 0.7f, 3.5f);
    const Material beaconRed = makeMaterial({1.00f, 0.18f, 0.08f}, 0.60f, S::Glass, 0.10f, 0.0f, 1.0f);
    const Material generatorMat = makeMaterial({0.28f, 0.33f, 0.24f}, 0.70f, S::Paint, 0.50f, 0.5f);
    const Material warmMetal = makeMaterial({0.30f, 0.20f, 0.12f}, 0.80f, S::Metal, 0.55f, 0.8f);

    // ---------------------------------------------------------------- helpers
    auto cube = [&](SceneNode* p, const char* name, Vec3 mn, Vec3 mx, const Material& m, const char* purpose,
                    bool underground = true) {
        SceneNode* n = b.part(p, name, P::Cube, mx - mn, {0, 0, 0}, (mn + mx) * 0.5f, m, purpose);
        if (underground) n->obstacle = false;
        return n;
    };
    // Solid box: visible part + collision box for the walker.
    auto solid = [&](SceneNode* p, const char* name, Vec3 mn, Vec3 mx, const Material& m, const char* purpose) {
        walk.addWall(Box{mn, mx});
        return cube(p, name, mn, mx, m, purpose);
    };
    auto under = [](SceneNode* n) {
        n->obstacle = false;
        return n;
    };
    auto pipe = [&](SceneNode* p, const char* name, Vec3 a, Vec3 c, float diameter, const Material& m, const char* purpose) {
        return under(strut(b, p, name, P::Cylinder, a, c, diameter, m, purpose));
    };
    auto vcyl = [&](SceneNode* p, const char* name, Vec3 centre, float diameter, float height, const Material& m,
                    const char* purpose) {  // vertical cylinder (axis Z rotated onto Y)
        return under(b.part(p, name, P::Cylinder, {diameter, diameter, height}, {90, 0, 0}, centre, m, purpose));
    };
    auto fixture = [&](SceneNode* p, float x, float z, bool lit, const char* name) {
        under(b.part(p, "Fixture housing", P::Cube, {1.3f, 0.05f, 0.30f}, {0, 0, 0}, {x, kC - 0.03f, z}, galv,
                     "Fluorescent fixture body"));
        under(b.part(p, name, P::Cube, {1.2f, 0.05f, 0.22f}, {0, 0, 0}, {x, kC - 0.07f, z}, lit ? fluorLit : fluorDim,
                     lit ? "Fluorescent tube (practical light)" : "Fluorescent tube"));
    };
    auto tallyScreen = [&](SceneNode* n, float base) {
        anim.screens.push_back(n);
        anim.screenBase.push_back(base);
    };
    auto G = [](float x, float z) { return terrainHeight(x, z); };

    // ---------------------------------------------------------------- entrance trench geometry
    const float rampTopY = G(0.0f, kTopZ);
    auto rampY = [&](float z) { return z <= kBottomZ ? kF : lerpf(kF, rampTopY, clampf((z - kBottomZ) / (kTopZ - kBottomZ), 0, 1)); };
    // Highest ground beside the trench between z0 and z1 (the retaining wall reaches it).
    auto groundBeside = [&](float z0, float z1) {
        float m = -10.0f;
        for (int i = 0; i <= 6; ++i) {
            const float z = lerpf(z0, z1, i / 6.0f);
            for (float x : {1.7f, 2.0f, 2.3f}) m = std::max(m, std::max(G(x, z), G(-x, z)));
        }
        return m;
    };

    SceneNode* trench = b.group(r, "Entrance trench", {0, 0, 0}, {0, 0, 0}, "Open ramp cut into the ground leading to the blast doors");
    {
        const float dy = rampTopY - kF, dz = kTopZ - kBottomZ, len = std::sqrt(dy * dy + dz * dz);
        const Vec3 rot = alignZ({0, dy, dz});
        const Vec3 mid{0.0f, 0.5f * (kF + rampTopY), 0.5f * (kBottomZ + kTopZ)};
        const Vec3 centre = mid - Vec3{0.0f, dz / len, -dy / len} * 0.15f;
        under(b.part(trench, "Ramp floor", P::Cube, {3.2f, 0.30f, len}, rot, centre, floorMat, "Concrete ramp from the ground down to the doors"));
        for (int i = 0; i < 7; ++i) {
            const float z = kBottomZ + 0.7f + 1.0f * i;
            under(b.part(trench, "Ramp grip strip", P::Cube, {2.9f, 0.03f, 0.10f}, rot, {0.0f, rampY(z) + 0.02f, z}, darkMetal,
                         "Anti-slip steel strip across the ramp"));
        }
        under(b.part(trench, "Landing slab", P::Cube, {3.2f, 0.30f, kBottomZ - kDoorZ0}, {0, 0, 0},
                     {0.0f, kF - 0.15f, 0.5f * (kBottomZ + kDoorZ0)}, floorMat, "Flat landing in front of the doors"));
        walk.addFloor({-1.5f, kBottomZ, 1.5f, kTopZ, kF, rampTopY, 2, 1e9f});
        walk.addFloor({-1.5f, kDoorZ1, 1.5f, kBottomZ, kF, kF, 0, 1e9f});
        walk.addFloor({-1.5f, kDoorZ0, 1.5f, kDoorZ1, kF, kF, 0, kC});
    }
    // Retaining walls with coping slabs and counterfort ribs, 8 segments per side following the ground.
    {
        const int n = 8;
        const float z0 = kDoorZ0, z1 = kTopZ + 0.45f, seg = (z1 - z0) / n;
        float maxTop = -10.0f;
        for (int s = -1; s <= 1; s += 2) {
            for (int i = 0; i < n; ++i) {
                const float za = z0 + seg * i, zb = za + seg;
                const float top = groundBeside(za, zb);
                maxTop = std::max(maxTop, top);
                const float bottom = rampY(za) - 0.35f;
                const float xi = s * 1.5f, xo = s * (1.5f + kWallT);
                under(b.part(trench, s < 0 ? "Retaining wall W" : "Retaining wall E", P::Cube, {kWallT, top - bottom, zb - za + 0.02f},
                             {0, 0, 0}, {0.5f * (xi + xo), 0.5f * (top + bottom), 0.5f * (za + zb)}, concreteIn,
                             "Reinforced concrete retaining wall (0.35 m)"));
                under(b.part(trench, s < 0 ? "Coping slab W" : "Coping slab E", P::Cube, {0.85f, 0.16f, zb - za + 0.04f}, {0, 0, 0},
                             {s * 1.82f, top + 0.06f, 0.5f * (za + zb)}, coping, "Coping slab hiding the ground edge"));
            }
            for (float zc : {5.4f, 7.9f, 10.4f, 12.6f}) {
                const float top = groundBeside(zc - 0.3f, zc + 0.3f), bottom = rampY(zc) - 0.05f;
                under(b.part(trench, "Counterfort rib", P::Cube, {0.18f, top - bottom, 0.30f}, {0, 0, 0},
                             {s * 1.41f, 0.5f * (top + bottom), zc}, concreteIn, "Buttress rib stiffening the wall"));
            }
        }
        walk.addWall(Box{{-1.85f, kF - 0.3f, kDoorZ0}, {-1.5f, maxTop + 0.12f, z1}});
        walk.addWall(Box{{1.5f, kF - 0.3f, kDoorZ0}, {1.85f, maxTop + 0.12f, z1}});
    }
    // Guard rails along both top edges.
    {
        const float pz[4] = {4.9f, 7.2f, 9.5f, 11.8f};
        for (int s = -1; s <= 1; s += 2) {
            float baseY[4];
            for (int i = 0; i < 4; ++i) {
                baseY[i] = groundBeside(pz[i] - 0.3f, pz[i] + 0.3f) + 0.14f;
                b.part(trench, "Guard rail post", P::Cylinder, {0.06f, 0.06f, 1.0f}, {90, 0, 0}, {s * 1.95f, baseY[i] + 0.5f, pz[i]},
                       paintYellow, "Steel post of the guard rail");
            }
            for (int i = 0; i < 3; ++i) {
                for (float h : {1.0f, 0.5f})
                    strut(b, trench, "Guard rail", P::Cylinder, {s * 1.95f, baseY[i] + h, pz[i]}, {s * 1.95f, baseY[i + 1] + h, pz[i + 1]},
                          0.04f, paintYellow, "Horizontal steel rail");
            }
        }
    }
    // Drainage grate across the ramp bottom (gutter, frame, bars).
    {
        under(b.part(trench, "Drain gutter", P::Cube, {2.9f, 0.02f, 0.45f}, {0, 0, 0}, {0.0f, kF + 0.005f, 6.5f}, rubber, "Dark drain channel"));
        under(b.part(trench, "Drain frame", P::Cube, {3.0f, 0.04f, 0.55f}, {0, 0, 0}, {0.0f, kF + 0.02f, 6.5f}, darkMetal, "Grate frame"));
        for (int i = 0; i < 6; ++i)
            under(b.part(trench, "Drain grate bar", P::Cube, {0.03f, 0.04f, 0.50f}, {0, 0, 0}, {-1.2f + 0.48f * i, kF + 0.045f, 6.5f}, galv,
                         "Grate bar"));
    }
    // Sodium wall lamps (practical lights) low on the retaining walls.
    {
        const struct { float x, z; } lamps[4] = {{-1.5f, 5.0f}, {1.5f, 7.6f}, {-1.5f, 10.2f}, {1.5f, 5.0f}};
        for (const auto& l : lamps) {
            const float y = std::min(rampY(l.z) + 2.2f, groundBeside(l.z - 0.3f, l.z + 0.3f) - 0.45f);
            const float s = l.x < 0 ? 1.0f : -1.0f;
            under(b.part(trench, "Wall lamp housing", P::Cube, {0.18f, 0.12f, 0.22f}, {0, 0, 0}, {l.x + 0.09f * s, y, l.z}, darkMetal, "Cage lamp bracket"));
            under(b.part(trench, "Wall lamp", P::Sphere, {0.12f, 0.12f, 0.12f}, {0, 0, 0}, {l.x + 0.20f * s, y - 0.02f, l.z}, sodium,
                         "Low wattage sodium lamp (practical light)"));
        }
    }
    // Portal: head wall with earth berm, hazard frame, signage.
    {
        const float headTop = std::max(groundBeside(kDoorZ0, kDoorZ1 + 0.4f), kRoofTop) + 0.10f;
        under(b.part(trench, "Portal head wall", P::Cube, {4.8f, headTop - kF, 0.55f}, {0, 0, 0}, {0.0f, 0.5f * (headTop + kF), 3.9f},
                     concreteOut, "Concrete head wall above the doors"));
        under(b.part(trench, "Earth berm", P::Sphere, {5.4f, 1.0f, 2.0f}, {0, 0, 0}, {0.0f, headTop, 2.8f}, earth,
                     "Earth berm camouflaging the portal head wall"));
        for (int s = -1; s <= 1; s += 2)
            under(b.part(trench, "Hazard jamb", P::Cube, {0.20f, kDoorHeight, 0.05f}, {0, 0, 0}, {s * 1.40f, kF + 0.5f * kDoorHeight, kDoorZ1 + 0.025f},
                         hazard, "Hazard striped portal frame"));
        under(b.part(trench, "Hazard header", P::Cube, {3.0f, 0.30f, 0.05f}, {0, 0, 0}, {0.0f, kC + 0.15f, kDoorZ1 + 0.025f}, hazard,
                     "Hazard striped header above the doors"));
        under(b.part(trench, "Warning sign", P::Cube, {0.9f, 0.35f, 0.03f}, {0, 0, 0}, {0.0f, kC + 0.60f, kDoorZ1 + 0.02f}, screenAmber,
                     "Restricted area sign (lit panel)"));
        tallyScreen(trench->children.back().get(), 0.35f);
        trench->children.back()->material.emissive = 0.35f;
    }

    // ---------------------------------------------------------------- blast doors (animated by BlastDoors)
    SceneNode* doorGroup = b.group(r, "Blast door assembly", {0, 0, 0}, {0, 0, 0}, "Two sliding steel leaves, drives, beacons and status lamp");
    SceneNode *leafL = nullptr, *leafR = nullptr, *spin[2] = {nullptr, nullptr}, *statusLamp = nullptr;
    std::vector<SceneNode*> beaconLamps;
    {
        const float zc = 0.5f * (kDoorZ0 + kDoorZ1);
        for (int s = -1; s <= 1; s += 2) {
            SceneNode* leaf = b.group(doorGroup, s < 0 ? "Door leaf W" : "Door leaf E", {s * 0.5f * bunker::kLeafWidth, kF + 0.5f * kDoorHeight, zc},
                                      {0, 0, 0}, "Heavy steel blast door leaf (0.35 m plate) sliding sideways into the wall pocket");
            leaf->animation = "Slides along X by 1.50 m with ease-in/ease-out in 4 s";
            (s < 0 ? leafL : leafR) = leaf;
            under(b.part(leaf, "Leaf plate", P::Cube, {bunker::kLeafWidth, kDoorHeight, 0.35f}, {0, 0, 0}, {0, 0, 0}, steelPlate, "Armoured plate"));
            for (int i = 0; i < 3; ++i)
                under(b.part(leaf, "Leaf stiffener", P::Cube, {0.10f, kDoorHeight - 0.3f, 0.05f}, {0, 0, 0},
                             {-0.45f + 0.45f * i, 0.0f, 0.195f}, steelPlate, "Vertical stiffening rib on the outer face"));
            under(b.part(leaf, "Leaf edge stripe", P::Cube, {0.12f, kDoorHeight, 0.04f}, {0, 0, 0}, {-s * (0.5f * bunker::kLeafWidth - 0.06f), 0.0f, 0.196f},
                         hazard, "Hazard stripe on the closing edge"));
            under(b.part(leaf, "Locking bolt", P::Cylinder, {0.12f, 0.12f, 0.20f}, {0, 0, 0}, {-s * 0.55f, 0.55f, 0.20f}, darkMetal, "Locking bolt head"));
        }
        // Rails and drive housings (inside, on the tunnel side of the front wall).
        under(b.part(doorGroup, "Door top rail", P::Cube, {6.4f, 0.14f, 0.20f}, {0, 0, 0}, {0.0f, kC - 0.07f, kDoorZ0 - 0.10f}, darkMetal, "Overhead guide rail"));
        under(b.part(doorGroup, "Door floor rail", P::Cube, {6.4f, 0.04f, 0.20f}, {0, 0, 0}, {0.0f, kF + 0.02f, zc}, darkMetal, "Floor guide rail"));
        for (int s = -1; s <= 1; s += 2)
            under(b.part(doorGroup, "Door drive motor", P::Cube, {0.6f, 0.45f, 0.35f}, {0, 0, 0}, {s * 2.4f, kC - 0.55f, kDoorZ0 - 0.18f}, paintOlive,
                         "Drive motor housing"));
        // Rotating amber warning beacons above the portal (emissive + light only while the doors move).
        for (int s = -1; s <= 1; s += 2) {
            const float bx = s * 1.10f;
            under(b.part(doorGroup, "Beacon base", P::Cylinder, {0.22f, 0.22f, 0.10f}, {90, 0, 0}, {bx, kC + 0.07f, kDoorZ1 + 0.10f}, darkMetal, "Beacon mount"));
            SceneNode* sp = b.group(doorGroup, s < 0 ? "Beacon spinner W" : "Beacon spinner E", {bx, kC + 0.26f, kDoorZ1 + 0.10f}, {0, 0, 0},
                                    "Rotating head of the warning beacon");
            sp->animation = "Rotates about the vertical axis at 300 deg/s while the doors move";
            spin[s < 0 ? 0 : 1] = sp;
            SceneNode* a = under(b.part(sp, "Beacon lamp A", P::Sphere, {0.16f, 0.16f, 0.16f}, {0, 0, 0}, {0.10f, 0, 0}, beaconAmberLit, "Amber lamp (practical light while moving)"));
            SceneNode* c = under(b.part(sp, "Beacon lamp B", P::Sphere, {0.16f, 0.16f, 0.16f}, {0, 0, 0}, {-0.10f, 0, 0}, beaconAmber, "Second amber lamp"));
            SceneNode* rf = under(b.part(sp, "Beacon reflector", P::Cube, {0.02f, 0.14f, 0.18f}, {0, 0, 0}, {0, 0, 0}, galv, "Reflector plate between the lamps"));
            (void)rf;
            beaconLamps.push_back(a);
            beaconLamps.push_back(c);
        }
        under(b.part(doorGroup, "Status lamp housing", P::Cube, {0.18f, 0.28f, 0.08f}, {0, 0, 0}, {-1.40f, kF + 1.7f, kDoorZ1 + 0.04f}, darkMetal, "Status lamp box"));
        statusLamp = under(b.part(doorGroup, "Status lamp", P::Sphere, {0.12f, 0.12f, 0.08f}, {0, 0, 0}, {-1.40f, kF + 1.7f, kDoorZ1 + 0.09f}, statusMat,
                                  "Door status lamp: green open, red closed or moving"));
        statusLamp->animation = "Green when open, red when closed or moving";
        // Front wall of the bunker around the opening (2.6 m clear width).
        solid(doorGroup, "Front wall W", {-5.5f, kF, kDoorZ0}, {-bunker::kDoorOpeningHalf, kC, kDoorZ1}, concreteOut, "Front wall with the leaf pocket");
        solid(doorGroup, "Front wall E", {bunker::kDoorOpeningHalf, kF, kDoorZ0}, {5.5f, kC, kDoorZ1}, concreteOut, "Front wall with the leaf pocket");
    }

    // ---------------------------------------------------------------- bunker shell
    SceneNode* shell = b.group(r, "Bunker shell", {0, 0, 0}, {0, 0, 0}, "Floor slab, outer walls and roof slab of the buried structure");
    const float shaftTop = G(4.55f, -7.55f) + 0.30f;
    {
        cube(shell, "Floor slab", {-5.5f, kF - 0.30f, -8.5f}, {5.5f, kF, 4.0f}, floorMat, "Foundation / floor slab");
        solid(shell, "West wall", {-5.5f, kF, -8.5f}, {-5.2f, kC, kDoorZ1}, concreteIn, "Outer west wall");
        solid(shell, "East wall", {5.2f, kF, -6.6f}, {5.5f, kC, kDoorZ1}, concreteIn, "Outer east wall");
        solid(shell, "South wall", {-5.5f, kF, -8.5f}, {3.6f, kC, -8.2f}, concreteIn, "Outer south wall");
        cube(shell, "Roof slab A", {-5.5f, kC, -6.6f}, {5.5f, kRoofTop, kDoorZ1}, concreteOut, "Roof slab (north part)");
        cube(shell, "Roof slab B", {-5.5f, kC, -8.5f}, {3.6f, kRoofTop, -6.6f}, concreteOut, "Roof slab (south part, beside the exit shaft)");
        // Inner walls: tunnel sides with doorways, cross wall with the CIC opening.
        solid(shell, "Tunnel wall W1", {-1.8f, kF, -1.8f}, {-1.5f, kC, 0.9f}, concreteIn, "Tunnel west wall");
        solid(shell, "Tunnel wall W2", {-1.8f, kF, 1.9f}, {-1.5f, kC, kDoorZ0}, concreteIn, "Tunnel west wall");
        cube(shell, "Doorway lintel W", {-1.8f, kF + 2.1f, 0.9f}, {-1.5f, kC, 1.9f}, concreteIn, "Lintel over the bunk room door");
        solid(shell, "Tunnel wall E1", {1.5f, kF, -1.8f}, {1.8f, kC, 0.6f}, concreteIn, "Tunnel east wall");
        solid(shell, "Tunnel wall E2", {1.5f, kF, 1.8f}, {1.8f, kC, kDoorZ0}, concreteIn, "Tunnel east wall");
        cube(shell, "Doorway lintel E", {1.5f, kF + 2.1f, 0.6f}, {1.8f, kC, 1.8f}, concreteIn, "Lintel over the generator room door");
        solid(shell, "Cross wall W", {-5.5f, kF, -2.1f}, {-0.9f, kC, -1.8f}, concreteIn, "Wall between the rooms and the command room");
        solid(shell, "Cross wall E", {0.9f, kF, -2.1f}, {5.5f, kC, -1.8f}, concreteIn, "Wall between the rooms and the command room");
        cube(shell, "Doorway lintel CIC", {-0.9f, kF + 2.2f, -2.1f}, {0.9f, kC, -1.8f}, concreteIn, "Lintel over the command room opening");
        // Exit shaft tube (rises through the earth cover to the surface hatch).
        solid(shell, "Shaft wall W", {3.6f, kF, -8.5f}, {3.9f, shaftTop, -6.6f}, concreteOut, "Exit shaft west wall");
        solid(shell, "Shaft wall E", {5.2f, kF, -8.5f}, {5.5f, shaftTop, -6.6f}, concreteOut, "Exit shaft east wall");
        solid(shell, "Shaft wall S", {3.9f, kF, -8.5f}, {5.2f, shaftTop, -8.2f}, concreteOut, "Exit shaft south wall");
        solid(shell, "Shaft wall N1", {3.9f, kF, -6.9f}, {4.0f, shaftTop, -6.6f}, concreteOut, "Exit shaft north wall");
        solid(shell, "Shaft wall N2", {4.9f, kF, -6.9f}, {5.2f, shaftTop, -6.6f}, concreteOut, "Exit shaft north wall");
        cube(shell, "Shaft door lintel", {4.0f, kF + 2.0f, -6.9f}, {4.9f, shaftTop, -6.6f}, concreteOut, "Lintel over the shaft entrance");
    }
    // Floors for the walker.
    walk.addFloor({-1.5f, -1.8f, 1.5f, kDoorZ0, kF, kF, 0, kC});             // tunnel
    walk.addFloor({-0.9f, -2.1f, 0.9f, -1.8f, kF, kF, 0, kC});               // opening to the CIC
    walk.addFloor({-5.2f, -8.2f, 5.2f, -2.1f, kF, kF, 0, kC});               // command room
    walk.addFloor({-5.2f, -1.8f, -1.8f, kDoorZ0, kF, kF, 0, kC});            // bunk room
    walk.addFloor({-1.8f, 0.9f, -1.5f, 1.9f, kF, kF, 0, kC});                // its doorway
    walk.addFloor({1.8f, -1.8f, 5.2f, kDoorZ0, kF, kF, 0, kC});              // generator room
    walk.addFloor({1.5f, 0.6f, 1.8f, 1.8f, kF, kF, 0, kC});                  // its doorway
    walk.addFloor({4.0f, -6.9f, 4.9f, -6.6f, kF, kF, 0, kC});                // shaft entrance
    walk.addFloor({3.9f, -8.2f, 5.2f, -6.9f, kF, kF, 0, shaftTop});          // exit shaft (ladder up to the hatch)
    for (const Box& v : {Box{{-1.5f, kF - 0.1f, -1.8f}, {1.5f, kC, kDoorZ1}}, Box{{-5.2f, kF - 0.1f, -1.8f}, {-1.8f, kC, kDoorZ0}},
                         Box{{1.8f, kF - 0.1f, -1.8f}, {5.2f, kC, kDoorZ0}}, Box{{-5.2f, kF - 0.1f, -8.2f}, {5.2f, kC, -1.8f}},
                         Box{{3.9f, kF - 0.1f, -8.2f}, {5.2f, shaftTop, -6.6f}}})
        walk.addIndoor(v);

    // ---------------------------------------------------------------- tunnel
    SceneNode* tun = b.group(r, "Tunnel", {0, 0, 0}, {0, 0, 0}, "Entrance tunnel from the blast doors to the command room");
    {
        fixture(tun, 0.0f, 3.0f, false, "Tunnel tube");
        fixture(tun, 0.0f, 1.0f, true, "Tunnel tube (light)");
        fixture(tun, 0.0f, -0.9f, true, "Tunnel tube (light)");
        for (float z : {3.1f, 0.2f, -1.3f}) {
            cube(tun, "Wall rib W", {-1.5f, kF, z - 0.15f}, {-1.38f, kC, z + 0.15f}, concreteIn, "Pilaster stiffening the wall");
        }
        for (float z : {3.1f, 0.2f, -1.3f})
            cube(tun, "Wall rib E", {1.38f, kF, z - 0.15f}, {1.5f, kC, z + 0.15f}, concreteIn, "Pilaster stiffening the wall");
        for (float z : {2.3f, 0.0f, -1.5f})
            cube(tun, "Ceiling beam", {-1.5f, kC - 0.20f, z - 0.12f}, {1.5f, kC, z + 0.12f}, concreteIn, "Concrete ceiling rib");
        cube(tun, "Cable tray", {-1.45f, kC - 0.32f, -1.7f}, {-1.05f, kC - 0.26f, 3.5f}, galv, "Cable tray along the ceiling");
        cube(tun, "Cable bundle", {-1.35f, kC - 0.26f, -1.7f}, {-1.15f, kC - 0.16f, 3.5f}, rubber, "Power cables in the tray");
        pipe(tun, "Water pipe", {1.15f, kC - 0.15f, -1.7f}, {1.15f, kC - 0.15f, 3.5f}, 0.14f, paintBlue, "Water supply pipe");
        pipe(tun, "Air duct pipe", {0.70f, kC - 0.15f, -1.7f}, {0.70f, kC - 0.15f, 3.5f}, 0.10f, paintRed, "Fire main pipe");
        for (int s = -1; s <= 1; s += 2)
            cube(tun, "Floor line", {s * 1.12f - 0.04f, kF, -1.6f}, {s * 1.12f + 0.04f, kF + 0.012f, 3.4f}, paintYellow, "Painted walkway line");
        cube(tun, "Door sill hazard", {-1.3f, kF, 3.2f}, {1.3f, kF + 0.012f, 3.6f}, hazard, "Hazard marking in front of the doors");
        cube(tun, "Floor drain", {-0.3f, kF, -0.4f}, {0.3f, kF + 0.014f, 0.0f}, darkMetal, "Floor drain grate");
        cube(tun, "Emergency exit sign", {-0.35f, kC - 0.42f, 3.40f}, {0.35f, kC - 0.20f, 3.44f}, exitSign, "Illuminated exit sign");
        under(b.part(tun, "Emergency lamp", P::Sphere, {0.12f, 0.10f, 0.12f}, {0, 0, 0}, {1.35f, kC - 0.45f, 1.0f}, redLit, "Red emergency lamp"));
        // Fire extinguisher and sign board on the east wall.
        vcyl(tun, "Fire extinguisher", {1.40f, kF + 0.70f, 2.6f}, 0.16f, 0.45f, paintRed, "Wall mounted extinguisher");
        cube(tun, "Extinguisher bracket", {1.40f, kF + 0.9f, 2.5f}, {1.50f, kF + 1.0f, 2.7f}, darkMetal, "Wall bracket");
        cube(tun, "Sign board", {1.46f, kF + 1.5f, 2.1f}, {1.50f, kF + 2.0f, 3.1f}, paintBlue, "Sign: COMMAND CENTRE >>>");
    }

    // ---------------------------------------------------------------- command room (CIC)
    SceneNode* cic = b.group(r, "Command room", {0, 0, 0}, {0, 0, 0}, "Command information centre with consoles, plotting table and servers");
    {
        for (float z : {-3.2f, -7.4f})
            for (float x : {-3.2f, 3.2f}) fixture(cic, x, z, z > -5.0f ? true : x < 0, z > -5.0f ? "CIC tube (light)" : "CIC tube");
        // Third light over the table, red emergency lamps.
        fixture(cic, 0.0f, -5.4f, true, "CIC tube (light)");
        under(b.part(cic, "Emergency lamp", P::Sphere, {0.14f, 0.12f, 0.14f}, {0, 0, 0}, {-5.05f, kC - 0.45f, -2.4f}, redLit, "Red emergency lamp"));
        under(b.part(cic, "Emergency lamp", P::Sphere, {0.14f, 0.12f, 0.14f}, {0, 0, 0}, {5.05f, kC - 0.45f, -7.9f}, fluorDim, "Dark emergency lamp"));
        cic->children.back()->material = makeMaterial({1.0f, 0.05f, 0.03f}, 0.4f, S::Glass, 0.2f, 0.0f, 1.0f);
        for (float z : {-2.4f, -6.5f, -7.9f})
            cube(cic, "Wall rib W", {-5.2f, kF, z - 0.15f}, {-5.08f, kC, z + 0.15f}, concreteIn, "Pilaster");
        for (float z : {-2.6f, -4.7f, -6.4f})
            cube(cic, "Wall rib E", {5.08f, kF, z - 0.15f}, {5.2f, kC, z + 0.15f}, concreteIn, "Pilaster");
        for (float x : {-4.6f, -2.8f, 2.2f})
            cube(cic, "Wall rib S", {x - 0.15f, kF, -8.2f}, {x + 0.15f, kC, -8.08f}, concreteIn, "Pilaster");
        for (float x : {-3.6f, -1.2f, 1.2f, 3.6f})
            cube(cic, "Ceiling beam", {x - 0.12f, kC - 0.22f, -8.2f}, {x + 0.12f, kC, -2.1f}, concreteIn, "Concrete ceiling rib");
        cube(cic, "Cable tray", {-5.1f, kC - 0.34f, -7.0f}, {5.1f, kC - 0.28f, -6.6f}, galv, "Cable tray along the ceiling");
        cube(cic, "Cable bundle", {-5.0f, kC - 0.28f, -6.9f}, {5.0f, kC - 0.18f, -6.7f}, rubber, "Power and data cables");
        pipe(cic, "Ventilation duct", {-5.0f, kC - 0.30f, -2.7f}, {5.0f, kC - 0.30f, -2.7f}, 0.30f, galv, "Round ventilation duct");
        pipe(cic, "Pipe", {-5.0f, kC - 0.12f, -2.35f}, {5.0f, kC - 0.12f, -2.35f}, 0.09f, paintBlue, "Water pipe");
        // Floor markings and drain.
        for (int s = -1; s <= 1; s += 2) cube(cic, "Floor line", {s * 0.95f - 0.04f, kF, -4.1f}, {s * 0.95f + 0.04f, kF + 0.012f, -2.2f}, paintYellow, "Walkway line");
        cube(cic, "Floor line ring N", {-1.8f, kF, -5.0f}, {1.8f, kF + 0.012f, -4.92f}, paintYellow, "Marking around the plotting table");
        cube(cic, "Floor drain", {-2.0f, kF, -7.6f}, {-1.4f, kF + 0.014f, -7.2f}, darkMetal, "Floor drain grate");
        cube(cic, "Cable cover", {-3.4f, kF, -4.6f}, {3.2f, kF + 0.03f, -4.5f}, rubber, "Floor cable cover");
        // Plotting table with a lit map top.
        solid(cic, "Plotting table base", {-1.2f, kF, -7.0f}, {1.2f, kF + 0.85f, -5.8f}, wood, "Central plotting table");
        {
            SceneNode* top = cube(cic, "Plotting table top", {-1.25f, kF + 0.85f, -7.05f}, {1.25f, kF + 0.90f, -5.75f}, screenGreen, "Glowing map table top");
            tallyScreen(top, 0.7f);
        }
        // Wall map on the south wall.
        cube(cic, "Wall map frame", {-2.7f, kF + 1.05f, -8.2f}, {1.7f, kF + 2.45f, -8.13f}, darkMetal, "Frame of the situation map");
        tallyScreen(cube(cic, "Wall map screen", {-2.6f, kF + 1.12f, -8.13f}, {1.6f, kF + 2.38f, -8.10f}, screenGreen, "Wall situation map"), 0.8f);
        // Four operator consoles with a desk, a tilted keyboard screen and an upright monitor; chairs.
        const struct { float x, z; bool amber; } cons[4] = {{-3.2f, -3.4f, false}, {3.2f, -3.4f, true}, {-3.2f, -5.6f, true}, {3.0f, -5.4f, false}};
        for (const auto& c : cons) {
            solid(cic, "Console desk", {c.x - 0.8f, kF, c.z - 0.35f}, {c.x + 0.8f, kF + 0.75f, c.z + 0.35f}, paintGrey, "Operator console desk");
            SceneNode* tilt = b.part(cic, "Console keyboard screen", P::Cube, {1.3f, 0.04f, 0.45f}, {-18, 0, 0}, {c.x, kF + 0.78f, c.z + 0.05f},
                                     c.amber ? screenAmber : screenGreen, "Sloped console display");
            tilt->obstacle = false;
            tallyScreen(tilt, 0.75f);
            SceneNode* mon = cube(cic, "Console monitor", {c.x - 0.5f, kF + 0.95f, c.z - 0.30f}, {c.x + 0.5f, kF + 1.50f, c.z - 0.25f},
                                  c.amber ? screenGreen : screenAmber, "Upright monitor");
            tallyScreen(mon, 0.85f);
            const float cz = c.z + 0.75f;
            cube(cic, "Chair seat", {c.x - 0.25f, kF + 0.42f, cz - 0.25f}, {c.x + 0.25f, kF + 0.48f, cz + 0.25f}, rubber, "Operator chair seat");
            cube(cic, "Chair back", {c.x - 0.25f, kF + 0.48f, cz + 0.22f}, {c.x + 0.25f, kF + 0.95f, cz + 0.27f}, rubber, "Operator chair back");
            vcyl(cic, "Chair post", {c.x, kF + 0.21f, cz}, 0.07f, 0.42f, darkMetal, "Gas lift of the chair");
        }
        // Server racks against the west wall with blinking LED rows.
        for (int i = 0; i < 3; ++i) {
            const float z = -3.6f - 1.0f * i;
            solid(cic, "Server rack", {-5.2f, kF, z - 0.4f}, {-4.6f, kF + 2.0f, z + 0.4f}, darkMetal, "19 inch server rack");
            for (int k = 0; k < 3; ++k) {
                SceneNode* led = cube(cic, "Rack LED row", {-4.60f, kF + 0.6f + 0.45f * k, z - 0.30f}, {-4.57f, kF + 0.66f + 0.45f * k, z + 0.30f}, ledMat,
                                      "Blinking status LEDs");
                led->animation = "LEDs blink pseudo-randomly at 5 Hz";
                anim.leds.push_back(led);
            }
        }
        // Notice board and signs.
        cube(cic, "Notice board", {5.1f, kF + 1.2f, -3.6f}, {5.2f, kF + 1.9f, -2.8f}, wood, "Cork notice board");
        cube(cic, "Room sign", {-0.6f, kC - 0.45f, -1.82f}, {0.6f, kC - 0.20f, -1.78f}, paintBlue, "Sign: COMMAND CENTRE");
        vcyl(cic, "Fire extinguisher", {-5.0f, kF + 0.40f, -1.95f}, 0.16f, 0.45f, paintRed, "Extinguisher by the door");
    }

    // ---------------------------------------------------------------- bunk / armory room (west)
    SceneNode* bunk = b.group(r, "Bunk room", {0, 0, 0}, {0, 0, 0}, "Crew bunks, weapon rack and lockers");
    {
        fixture(bunk, -3.5f, 1.0f, true, "Bunk room tube (light)");
        for (float z : {-0.7f, 2.0f})
            cube(bunk, "Ceiling beam", {-5.2f, kC - 0.20f, z - 0.12f}, {-1.8f, kC, z + 0.12f}, concreteIn, "Concrete ceiling rib");
        for (float z : {-1.0f, 1.2f, 3.1f})
            cube(bunk, "Wall rib", {-5.2f, kF, z - 0.15f}, {-5.08f, kC, z + 0.15f}, concreteIn, "Pilaster");
        for (float z : {0.3f, 2.5f}) {
            solid(bunk, "Bunk frame", {-5.15f, kF, z - 0.5f}, {-4.25f, kF + 1.7f, z + 0.5f}, darkMetal, "Steel bunk bed frame");
            cube(bunk, "Lower mattress", {-5.12f, kF + 0.35f, z - 0.48f}, {-4.28f, kF + 0.52f, z + 0.48f}, fabric, "Mattress");
            cube(bunk, "Upper mattress", {-5.12f, kF + 1.25f, z - 0.48f}, {-4.28f, kF + 1.42f, z + 0.48f}, fabric, "Mattress");
            cube(bunk, "Pillow", {-5.0f, kF + 1.42f, z - 0.45f}, {-4.5f, kF + 1.50f, z - 0.15f}, paintGrey, "Pillow");
        }
        solid(bunk, "Weapon rack", {-4.0f, kF, -1.8f}, {-2.2f, kF + 1.3f, -1.55f}, wood, "Armory weapon rack");
        for (int i = 0; i < 3; ++i)
            pipe(bunk, "Rifle", {-3.8f + 0.55f * i, kF + 0.35f, -1.5f}, {-3.8f + 0.55f * i, kF + 1.15f, -1.5f}, 0.06f, rubber, "Stored rifle");
        solid(bunk, "Lockers", {-4.9f, kF, 3.15f}, {-3.0f, kF + 1.9f, 3.65f}, paintOlive, "Row of personal lockers");
        for (int i = 0; i < 3; ++i)
            cube(bunk, "Locker door line", {-4.5f + 0.63f * i - 0.01f, kF + 0.1f, 3.14f}, {-4.5f + 0.63f * i + 0.01f, kF + 1.8f, 3.15f}, darkMetal, "Locker door gap");
        solid(bunk, "Table", {-3.5f, kF + 0.0f, 0.5f}, {-2.5f, kF + 0.75f, 1.4f}, wood, "Mess table");
        cube(bunk, "Bench", {-3.6f, kF, 1.5f}, {-2.4f, kF + 0.42f, 1.8f}, wood, "Bench");
        cube(bunk, "Room sign", {-1.78f, kF + 2.15f, 1.0f}, {-1.74f, kF + 2.4f, 1.8f}, paintBlue, "Sign: BUNKS");
        vcyl(bunk, "Fire extinguisher", {-2.0f, kF + 0.40f, 3.4f}, 0.16f, 0.45f, paintRed, "Extinguisher");
        cube(bunk, "Floor line", {-4.0f, kF, -1.4f}, {-2.0f, kF + 0.012f, -1.3f}, paintYellow, "Painted line in front of the rack");
    }

    // ---------------------------------------------------------------- generator / plant room (east)
    SceneNode* plant = b.group(r, "Generator room", {0, 0, 0}, {0, 0, 0}, "Diesel generator, fuel tank and ventilation plant");
    {
        fixture(plant, 3.5f, 0.6f, true, "Plant room tube (light)");
        for (float z : {-0.9f, 2.0f})
            cube(plant, "Ceiling beam", {1.8f, kC - 0.20f, z - 0.12f}, {5.2f, kC, z + 0.12f}, concreteIn, "Concrete ceiling rib");
        for (float z : {-1.2f, 0.0f, 3.2f})
            cube(plant, "Wall rib", {5.08f, kF, z - 0.15f}, {5.2f, kC, z + 0.15f}, concreteIn, "Pilaster");
        // Diesel generator: skid, block, alternator, radiator, exhaust and silencer.
        solid(plant, "Generator skid", {2.3f, kF, 2.0f}, {5.0f, kF + 0.18f, 3.3f}, darkMetal, "Steel skid frame");
        cube(plant, "Generator engine block", {2.3f, kF + 0.18f, 2.2f}, {4.0f, kF + 1.0f, 3.1f}, generatorMat, "Diesel engine block");
        under(b.part(plant, "Generator alternator", P::Cylinder, {0.7f, 0.7f, 0.9f}, {0, 90, 0}, {4.45f, kF + 0.65f, 2.65f}, generatorMat, "Alternator"));
        cube(plant, "Generator radiator", {2.0f, kF + 0.25f, 2.2f}, {2.3f, kF + 1.05f, 3.1f}, paintGrey, "Radiator");
        under(b.part(plant, "Generator cylinder head", P::Cylinder, {0.25f, 0.25f, 0.30f}, {90, 0, 0}, {3.1f, kF + 1.15f, 2.6f}, warmMetal, "Hot cylinder head"));
        under(b.part(plant, "Generator cylinder head", P::Cylinder, {0.25f, 0.25f, 0.30f}, {90, 0, 0}, {3.6f, kF + 1.15f, 2.6f}, warmMetal, "Hot cylinder head"));
        pipe(plant, "Exhaust pipe", {3.9f, kF + 1.1f, 2.6f}, {4.55f, kF + 1.9f, 2.4f}, 0.12f, warmMetal, "Exhaust pipe leaving the engine");
        vcyl(plant, "Exhaust silencer", {4.55f, kF + 2.1f, 2.4f}, 0.30f, 0.60f, darkMetal, "Silencer");
        vcyl(plant, "Exhaust riser", {4.55f, 0.5f * (kF + 2.4f + kRoofTop + 0.6f), 2.4f}, 0.12f, (kRoofTop + 0.6f) - (kF + 2.4f), warmMetal,
             "Exhaust riser up through the roof to the vent stack");
        solid(plant, "Control cabinet", {4.7f, kF, 1.0f}, {5.2f, kF + 1.9f, 1.8f}, paintOlive, "Generator control cabinet");
        solid(plant, "Switchgear cabinet", {4.7f, kF, 0.0f}, {5.2f, kF + 1.9f, 0.8f}, paintGrey, "Switchgear");
        {
            SceneNode* d = cube(plant, "Cabinet display", {4.66f, kF + 1.3f, 1.1f}, {4.70f, kF + 1.6f, 1.7f}, screenAmber, "Generator status display");
            tallyScreen(d, 0.7f);
        }
        // Fuel tank (horizontal cylinder along X) on two saddles.
        under(b.part(plant, "Fuel tank", P::Cylinder, {1.0f, 1.0f, 2.2f}, {0, 90, 0}, {3.4f, kF + 0.85f, -1.0f}, paintOlive, "Day fuel tank"));
        walk.addWall(Box{{2.3f, kF, -1.5f}, {4.5f, kF + 1.35f, -0.5f}});
        cube(plant, "Tank saddle", {2.6f, kF, -1.4f}, {2.9f, kF + 0.45f, -0.6f}, darkMetal, "Saddle");
        cube(plant, "Tank saddle", {3.9f, kF, -1.4f}, {4.2f, kF + 0.45f, -0.6f}, darkMetal, "Saddle");
        pipe(plant, "Fuel line", {2.3f, kF + 0.9f, -1.0f}, {2.3f, kF + 0.9f, 2.2f}, 0.05f, paintYellow, "Fuel line to the engine");
        // Ventilation fan unit and its riser to the intake stack.
        cube(plant, "Ventilation unit", {4.2f, kC - 0.55f, -0.7f}, {5.0f, kC - 0.15f, 0.0f}, galv, "Ventilation fan housing");
        vcyl(plant, "Ventilation riser", {4.6f, 0.5f * (kC - 0.15f + kRoofTop + 0.6f), -0.4f}, 0.45f, (kRoofTop + 0.6f) - (kC - 0.15f), galv,
             "Intake duct up through the roof to the vent stack");
        // Floor markings, drain, signs.
        cube(plant, "Floor line", {1.9f, kF, -0.1f}, {2.0f, kF + 0.012f, 1.9f}, paintYellow, "Painted line");
        cube(plant, "Floor drain", {3.0f, kF, 0.6f}, {3.5f, kF + 0.014f, 1.0f}, darkMetal, "Floor drain grate");
        cube(plant, "Hazard marking", {2.3f, kF, 1.5f}, {5.0f, kF + 0.012f, 1.9f}, hazard, "Hazard strip in front of the generator");
        cube(plant, "Room sign", {1.74f, kF + 2.15f, 0.6f}, {1.78f, kF + 2.4f, 1.8f}, paintYellow, "Sign: GENERATOR");
        vcyl(plant, "Fire extinguisher", {2.1f, kF + 0.40f, 3.4f}, 0.16f, 0.45f, paintRed, "Extinguisher");
        pipe(plant, "Ceiling pipe", {1.9f, kC - 0.12f, 3.2f}, {5.0f, kC - 0.12f, 3.2f}, 0.09f, paintBlue, "Water pipe");
    }

    // ---------------------------------------------------------------- exit shaft with ladder
    SceneNode* shaft = b.group(r, "Exit shaft", {0, 0, 0}, {0, 0, 0}, "Emergency exit: ladder up to the surface hatch");
    {
        const float top = shaftTop - 0.15f;
        under(b.part(shaft, "Shaft lamp", P::Sphere, {0.14f, 0.14f, 0.14f}, {0, 0, 0}, {4.0f, kC - 0.3f, -7.6f}, fluorLit, "Caged lamp (practical light)"));
        for (float x : {4.3f, 4.8f})
            vcyl(shaft, "Ladder rail", {x, 0.5f * (kF + top), -8.1f}, 0.05f, top - kF, galv, "Ladder side rail");
        for (float y = kF + 0.35f; y < top - 0.1f; y += 0.30f)
            pipe(shaft, "Ladder rung", {4.3f, y, -8.1f}, {4.8f, y, -8.1f}, 0.03f, galv, "Ladder rung");
        cube(shaft, "Exit sign", {4.3f, kF + 2.3f, -6.95f}, {4.8f, kF + 2.5f, -6.91f}, exitSign, "Illuminated exit sign");
    }

    // ---------------------------------------------------------------- surface installations
    SceneNode* surf = b.group(r, "Surface installations", {0, 0, 0}, {0, 0, 0}, "Vent stacks, beacon mast, masts, hatch and sandbags above the bunker");
    {
        auto occ = [](SceneNode* n) {
            n->occluder = true;
            return n;
        };
        // Two vent stacks with spinning fan groups.
        const Vec3 stacks[2] = {{4.55f, 0.0f, 2.4f}, {4.6f, 0.0f, -0.4f}};
        for (int i = 0; i < 2; ++i) {
            const float gy = G(stacks[i].x, stacks[i].z);
            occ(b.part(surf, i == 0 ? "Vent stack A (exhaust)" : "Vent stack B (intake)", P::Cube, {0.75f, 1.2f, 0.75f}, {0, 0, 0},
                       {stacks[i].x, gy + 0.45f, stacks[i].z}, paintGrey, "Armoured ventilation stack"));
            for (int k = 0; k < 2; ++k)
                b.part(surf, "Stack louvre", P::Cube, {0.78f, 0.05f, 0.78f}, {0, 0, 0}, {stacks[i].x, gy + 0.2f + 0.25f * k, stacks[i].z}, darkMetal, "Louvre band");
            b.part(surf, "Stack rain cap rim", P::Cube, {0.85f, 0.06f, 0.85f}, {0, 0, 0}, {stacks[i].x, gy + 1.08f, stacks[i].z}, darkMetal, "Rim");
            SceneNode* fan = b.group(surf, i == 0 ? "Fan A" : "Fan B", {stacks[i].x, gy + 1.15f, stacks[i].z}, {90, 0, 0}, "Exhaust / intake fan");
            fan->animation = "Fan spins about its axis";
            rig.fanCaps[i] = fan;
            b.part(fan, "Fan disc", P::Cylinder, {0.60f, 0.60f, 0.03f}, {0, 0, 0}, {0, 0, 0}, darkMetal, "Fan disc");
            b.part(fan, "Fan blade", P::Cube, {0.58f, 0.07f, 0.02f}, {0, 0, 20}, {0, 0, 0.03f}, galv, "Fan blade");
            b.part(fan, "Fan blade", P::Cube, {0.58f, 0.07f, 0.02f}, {0, 0, 110}, {0, 0, 0.03f}, galv, "Fan blade");
        }
        // Beacon mast with the pulsing beacon (point light L1).
        {
            const float mx = -3.6f, mz = 1.6f, gy = G(mx, mz);
            b.part(surf, "Beacon mast base", P::Cube, {0.4f, 0.3f, 0.4f}, {0, 0, 0}, {mx, gy + 0.1f, mz}, concreteOut, "Concrete footing");
            b.part(surf, "Beacon mast", P::Cylinder, {0.09f, 0.09f, 4.0f}, {90, 0, 0}, {mx, gy + 2.1f, mz}, darkMetal, "Steel mast");
            b.part(surf, "Beacon housing", P::Cylinder, {0.22f, 0.22f, 0.10f}, {90, 0, 0}, {mx, gy + 4.15f, mz}, darkMetal, "Lamp socket");
            rig.beaconBulb = b.part(surf, "Beacon bulb", P::Sphere, {0.20f, 0.20f, 0.20f}, {0, 0, 0}, {mx, gy + 4.30f, mz}, beaconRed, "Point light source L1");
            rig.beaconBulb->animation = "Emission pulses at 1 Hz";
        }
        // Periscope and antenna masts above the command room.
        {
            const float px = 0.8f, pz = -4.6f, gy = G(px, pz);
            b.part(surf, "Periscope tube", P::Cylinder, {0.16f, 0.16f, 0.9f}, {90, 0, 0}, {px, gy + 0.40f, pz}, darkMetal, "Periscope tube");
            b.part(surf, "Periscope head", P::Cube, {0.30f, 0.18f, 0.30f}, {0, 0, 0}, {px, gy + 0.92f, pz}, paintOlive, "Periscope head");
            b.part(surf, "Periscope lens", P::Cylinder, {0.10f, 0.10f, 0.04f}, {0, 0, 0}, {px, gy + 0.92f, pz + 0.16f}, makeMaterial({0.1f, 0.15f, 0.2f}, 0.3f, S::Glass, 0.1f, 0.0f), "Lens");
            const float ax = -4.0f, az = -6.0f, ay = G(ax, az);
            b.part(surf, "Antenna base", P::Cube, {0.4f, 0.25f, 0.4f}, {0, 0, 0}, {ax, ay + 0.05f, az}, concreteOut, "Footing");
            b.part(surf, "Antenna mast", P::Cylinder, {0.05f, 0.05f, 3.2f}, {90, 0, 0}, {ax, ay + 1.7f, az}, darkMetal, "Whip antenna");
            b.part(surf, "Antenna dish", P::Sphere, {0.7f, 0.7f, 0.12f}, {0, 0, 0}, {ax, ay + 2.6f, az + 0.1f}, paintGrey, "Satellite dish");
        }
        // Emergency hatch on top of the exit shaft.
        {
            const float hx = 4.55f, hz = -7.55f;
            cube(surf, "Hatch collar", {3.65f, shaftTop - 0.30f, -8.45f}, {5.45f, shaftTop, -6.65f}, concreteOut, "Concrete collar of the exit shaft", false);
            b.part(surf, "Hatch lid", P::Cube, {1.45f, 0.10f, 1.45f}, {0, 0, 0}, {hx, shaftTop + 0.05f, hz}, steelPlate, "Steel hatch lid");
            b.part(surf, "Hatch wheel", P::Cylinder, {0.40f, 0.40f, 0.04f}, {90, 0, 0}, {hx, shaftTop + 0.14f, hz}, darkMetal, "Hand wheel disc");
            b.part(surf, "Hatch wheel spoke", P::Cube, {0.44f, 0.05f, 0.05f}, {0, 0, 0}, {hx, shaftTop + 0.18f, hz}, galv, "Wheel spoke");
            b.part(surf, "Hatch wheel spoke", P::Cube, {0.05f, 0.05f, 0.44f}, {0, 0, 0}, {hx, shaftTop + 0.18f, hz}, galv, "Wheel spoke");
            b.part(surf, "Hatch hinge", P::Cube, {1.0f, 0.06f, 0.10f}, {0, 0, 0}, {hx, shaftTop + 0.11f, hz - 0.75f}, darkMetal, "Hinge");
            walk.addWall(Box{{3.65f, shaftTop - 0.6f, -8.45f}, {5.45f, shaftTop + 0.2f, -6.65f}});
        }
        // Sandbags around the hatch and the stacks.
        for (int i = 0; i < 9; ++i) {
            const float a = 0.5f + 0.38f * i;
            const float sx = 4.55f + 1.5f * std::cos(a + 3.3f), sz = -7.55f + 1.4f * std::sin(a + 3.3f);
            occ(b.part(surf, "Sandbag", P::Cylinder, {0.24f, 0.18f, 0.5f}, {0, 90 - degrees(a + 3.3f), 0}, {sx, G(sx, sz) + 0.07f, sz}, sand, "Sandbag"));
        }
    }

    // ---------------------------------------------------------------- controller
    const Vec3 eye{0.0f, rampY(9.0f) + 1.7f, 9.0f}, target{0.0f, kF + 1.3f, kDoorZ1};
    rig.doors = std::make_unique<BlastDoors>(doorGroup, leafL, leafR, spin[0], spin[1], beaconLamps, statusLamp, std::move(anim), eye, target);
    return rig;
}

}  // namespace ts
