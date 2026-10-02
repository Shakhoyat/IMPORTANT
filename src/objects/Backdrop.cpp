#include "objects/Backdrop.h"

#include "objects/BaseLayout.h"
#include "objects/Builder.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

using P = PrimitiveType;

constexpr int kNearHills = 12, kFarHills = 9, kTreelinePines = 46;
constexpr float kRoadGapDeg = 14.0f;  // the treeline leaves the track's continuation (+Z) open

float farGroundTop() { return layout::kApronLevel - kFarGroundGap; }

// Yaw that lines a primitive's local X axis up with the tangent of a circle at polar angle a (from +X to +Z).
float tangentYawDeg(float a) { return degrees(std::atan2(-std::cos(a), -std::sin(a))); }

void addHills(Builder& b, SceneNode* root) {
    const Material nearHill = makeMaterial({0.16f, 0.19f, 0.15f}, 0.10f, Surface::Ground, 0.95f, 0.0f);
    const Material farHill = makeMaterial({0.13f, 0.15f, 0.15f}, 0.09f, Surface::Ground, 0.95f, 0.0f);
    SceneNode* hills = b.group(root, "Distant hills", {0, 0, 0}, {0, 0, 0}, "Flattened spheres, half buried");
    PropRng rng(97u);
    for (int ring = 0; ring < 2; ++ring) {
        const int count = ring == 0 ? kNearHills : kFarHills;
        for (int i = 0; i < count; ++i) {
            const float a = radians(360.0f * static_cast<float>(i) / static_cast<float>(count) + rng.range(-8.0f, 8.0f) +
                                    (ring == 0 ? 0.0f : 20.0f));
            const float d = ring == 0 ? rng.range(55.0f, 70.0f) : rng.range(85.0f, 100.0f);
            const float width = ring == 0 ? rng.range(40.0f, 58.0f) : rng.range(70.0f, 100.0f);
            const float depth = ring == 0 ? std::min(26.0f, 2.0f * (d - 40.0f)) : 40.0f;
            const float height = ring == 0 ? rng.range(5.0f, 9.0f) : rng.range(12.0f, 22.0f);
            b.part(hills, ring == 0 ? "Hill" : "Far hill", P::Sphere, {width, 2.0f * height, depth},
                   {0, tangentYawDeg(a), 0}, {d * std::cos(a), farGroundTop() - 0.3f, d * std::sin(a)},
                   ring == 0 ? nearHill : farHill, "Horizon silhouette");
        }
    }
}

void addTreeline(Builder& b, SceneNode* root) {
    const Material needles = mats::pineNeedles();
    SceneNode* line = b.group(root, "Treeline", {0, 0, 0}, {0, 0, 0}, "Ring of tall pines behind the perimeter wall");
    PropRng rng(131u);
    for (int i = 0; i < kTreelinePines; ++i) {
        const float deg = 360.0f * static_cast<float>(i) / static_cast<float>(kTreelinePines) + rng.range(-4.0f, 4.0f);
        const float d = rng.range(38.0f, 45.0f), h = rng.range(6.0f, 10.0f);
        if (std::fabs(deg - 90.0f) < kRoadGapDeg) continue;
        const float a = radians(deg);
        SceneNode* pine = b.group(line, "Treeline pine", {d * std::cos(a), farGroundTop(), d * std::sin(a)},
                                  {0, rng.range(0.0f, 360.0f), 0}, "Two stacked cones");
        const float r = 0.28f * h;
        b.part(pine, "Pine crown", P::Cone, {2.0f * r, 2.0f * r, 0.62f * h}, {-90, 0, 0}, {0, 0.31f * h, 0}, needles,
               "Lower tier");
        b.part(pine, "Pine crown", P::Cone, {1.3f * r, 1.3f * r, 0.55f * h}, {-90, 30, 0}, {0, 0.725f * h, 0}, needles,
               "Upper tier");
    }
}

void addFarTower(Builder& b, SceneNode* root) {
    const Material paint = makeMaterial({0.55f, 0.22f, 0.18f}, 0.14f, Surface::Paint, 0.5f, 0.6f);
    const float a = radians(140.0f), d = 100.0f, top = 38.0f;
    SceneNode* t = b.group(root, "Far radio tower", {d * std::cos(a), farGroundTop(), d * std::sin(a)}, {0, 20, 0},
                           "Distant lattice tower");
    auto leg = [top](float sx, float sz, float y) {
        const float off = lerpf(2.0f, 0.3f, y / top);
        return Vec3{sx * off, y, sz * off};
    };
    const float corners[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
    for (const auto& c : corners) strut(b, t, "Tower leg", P::Cylinder, leg(c[0], c[1], 0), leg(c[0], c[1], top), 0.3f, paint, "Leg");
    for (int i = 0; i < 4; ++i) {
        const auto& c0 = corners[i];
        const auto& c1 = corners[(i + 1) % 4];
        strut(b, t, "Tower brace", P::Cylinder, leg(c0[0], c0[1], 2.0f), leg(c1[0], c1[1], 0.5f * top), 0.15f, paint, "Brace");
    }
    b.part(t, "Tower antenna", P::Cylinder, {0.12f, 0.12f, 4.0f}, {90, 0, 0}, {0, top + 2.0f, 0}, paint, "Antenna");
    const Material red = mats::aviationLight();
    for (float y : {11.0f, 22.0f, top + 4.1f}) {
        SceneNode* l = b.part(t, "Aviation light", P::Sphere, {0.6f, 0.6f, 0.6f}, {0, 0, 0}, {0, y, 0}, red,
                              "Blinking red obstruction light");
        l->animation = "Blinks 0.5 s on / 1.0 s off";
    }
}

void markDecoration(SceneNode& n) {
    n.obstacle = false;
    n.occluder = false;
    n.explodable = false;
    for (auto& c : n.children) markDecoration(*c);
}

}  // namespace

std::unique_ptr<SceneNode> buildBackdrop() {
    Builder b("Backdrop", "BKG", ShadingModel::Gouraud);
    auto root = b.root("Backdrop", {0, 0, 0}, "Distant world outside the playable disc");
    SceneNode* r = root.get();

    // Far ground: a flat 400 m cylinder (axis vertical) just below the terrain rim, so the horizon between the
    // 36 m terrain mesh and the hills is solid ground. The terrain never dips below the rim, so it stays hidden inside.
    const Material ground = makeMaterial({0.30f, 0.29f, 0.24f}, 0.12f, Surface::Ground, 0.95f, 0.0f);
    b.part(r, "Far ground", P::Cylinder, {400.0f, 400.0f, 0.1f}, {90, 0, 0}, {0, farGroundTop() - 0.05f, 0}, ground,
           "Ground plane beyond the disc");
    addHills(b, r);
    addTreeline(b, r);
    addFarTower(b, r);
    markDecoration(*r);
    return root;
}

}  // namespace ts
