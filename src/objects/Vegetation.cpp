#include "objects/Vegetation.h"

#include "objects/BaseLayout.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

using P = PrimitiveType;

constexpr float kPineSkirt = 0.6f;         // lowest branches above the ground (rover fits under them)
constexpr float kBunkerClearance = 5.5f;   // no trees on the bunker pad
constexpr float kTreeMaxRadius = 18.0f;    // trees stay inside the berm crest / fence
constexpr float kTrackClearance = 2.8f;    // metres either side of the track centre line
constexpr float kCreekClearance = kCreekHalfWidth + 0.7f;

SceneNode* solid(SceneNode* n) {
    n->occluder = true;
    return n;
}

}  // namespace

// Deterministic per-position seed so every tree/rock is different but identical on every run.
static uint32_t seedAt(float x, float z) {
    return static_cast<uint32_t>(static_cast<int>(x * 97.0f) * 73856093) ^ static_cast<uint32_t>(static_cast<int>(z * 89.0f) * 19349663);
}

void addPine(Builder& b, SceneNode* parent, float x, float z, float height, float yawDeg) {
    const Material bark = mats::bark(), needles = mats::pineNeedles();
    PropRng rng(seedAt(x, z));
    SceneNode* tree = b.group(parent, "Pine", onGround(x, 0.0f, z), {0, yawDeg, 0},
                              "Spruce = cylinder trunk + drooping cone whorls");
    const float trunk = 0.045f * height + 0.05f;
    solid(b.part(tree, "Pine trunk", P::Cylinder, {trunk, trunk, height * 0.8f}, {90, 0, 0}, {0, height * 0.4f, 0},
                 bark, "Trunk"));
    // Branch whorls: wide, flat cones from the skirt to the tip, shrinking upwards, each turned and tilted
    // a little so the silhouette is irregular (a real spruce is never perfectly symmetric).
    const int whorls = height > 3.8f ? 7 : (height > 3.0f ? 6 : 5);
    const float span = height - kPineSkirt, baseRadius = 0.33f * height;
    for (int i = 0; i < whorls; ++i) {
        const float k = static_cast<float>(i) / static_cast<float>(whorls - 1);   // 0 bottom .. 1 top
        const float r = baseRadius * (1.0f - 0.78f * k) * rng.range(0.9f, 1.08f);
        const float tierH = (i == whorls - 1) ? span * 0.30f : r * rng.range(0.85f, 1.05f);
        const float base = kPineSkirt + span * 0.80f * std::pow(k, 0.9f);
        const Vec3 tilt{-90.0f + rng.range(-6.0f, 6.0f), rng.range(0.0f, 360.0f), rng.range(-6.0f, 6.0f)};
        // The draws are made in named steps: inside one call the compiler chooses the order of the arguments, and GCC and
        // Clang choose differently, which gave the Windows build other trees than the Linux build.
        const float offX = rng.range(-0.04f, 0.04f), offZ = rng.range(-0.04f, 0.04f);
        const float depth = 2.0f * r * rng.range(0.9f, 1.1f);
        solid(b.part(tree, "Pine crown", P::Cone, {2.0f * r, depth, tierH}, tilt, {offX, base + 0.5f * tierH, offZ}, needles,
                     "Branch whorl (cone, apex up)"));
    }
}

void addRoundTree(Builder& b, SceneNode* parent, float x, float z, float size) {
    const Material bark = mats::bark(), leaves = mats::leaves();
    PropRng rng(seedAt(x, z) ^ 0x51ED27u);
    SceneNode* tree = b.group(parent, "Tree", onGround(x, 0.0f, z), {0, x * 37.0f, 0},
                              "Broadleaf tree = trunk + limbs + clustered foliage clumps");
    solid(b.part(tree, "Trunk", P::Cylinder, {0.22f * size, 0.22f * size, 1.9f * size}, {90, 0, 0},
                 {0, 0.95f * size, 0}, bark, "Tree trunk"));
    // Three limbs leaving the trunk at about 1.5 m.
    for (int i = 0; i < 3; ++i) {
        const float a = radians(120.0f * static_cast<float>(i) + rng.range(-20.0f, 20.0f));
        const Vec3 from{0, 1.45f * size, 0};
        const Vec3 to{std::cos(a) * 0.75f * size, rng.range(2.1f, 2.5f) * size, std::sin(a) * 0.75f * size};
        strut(b, tree, "Limb", P::Cylinder, from, to, 0.10f * size, bark, "Branch");
    }
    // Crown: 12 overlapping clumps on an ellipsoid shell + 2 inner fillers.
    const Vec3 centre{0, 2.55f * size, 0};
    const int clumps = 12;
    for (int i = 0; i < clumps; ++i) {
        const float u = (static_cast<float>(i) + 0.5f) / clumps;
        const float polar = std::acos(1.0f - 1.6f * u);          // favour the upper half
        const float azim = 2.39996f * static_cast<float>(i) + rng.range(-0.3f, 0.3f);  // golden-angle spiral
        const Vec3 dir{std::sin(polar) * std::cos(azim), std::cos(polar) * 0.8f, std::sin(polar) * std::sin(azim)};
        const float r = rng.range(0.45f, 0.75f) * size;
        solid(b.part(tree, "Canopy clump", P::Sphere, {2 * r, 1.7f * r, 2 * r}, {0, rng.range(0.0f, 360.0f), 0},
                     centre + dir * (0.95f * size), leaves, "Foliage clump"));
    }
    solid(b.part(tree, "Canopy low", P::Sphere, {1.9f * size, 1.3f * size, 1.9f * size}, {0, 0, 0}, centre,
                 leaves, "Inner foliage"));
    solid(b.part(tree, "Canopy top", P::Sphere, {1.3f * size, 1.0f * size, 1.3f * size}, {0, 0, 0},
                 centre + Vec3{0.1f * size, 0.6f * size, -0.1f * size}, leaves, "Inner foliage"));
}

void addBush(Builder& b, SceneNode* parent, float x, float z, float size, float yawDeg) {
    const Material leaves = mats::leaves();
    PropRng rng(seedAt(x, z) ^ 0xB005u);
    SceneNode* bush = b.group(parent, "Bush", onGround(x, 0.0f, z), {0, yawDeg, 0}, "Bush = cluster of flattened spheres");
    const int n = 5 + static_cast<int>(rng.next() * 3.0f);
    for (int i = 0; i < n; ++i) {
        const float a = rng.range(0.0f, 2.0f * PI), d = rng.range(0.0f, 0.38f) * size;
        const float r = rng.range(0.28f, 0.48f) * size;
        const float lift = rng.range(0.0f, 0.12f) * size;      // drawn before the yaw (see addPine)
        const float clumpYaw = rng.range(0.0f, 360.0f);
        solid(b.part(bush, "Bush", P::Sphere, {2 * r, 1.4f * r, 2 * r}, {0, clumpYaw, 0},
                     {std::cos(a) * d, r * 0.55f + lift, std::sin(a) * d}, leaves, "Shrub clump"));
    }
}

void addRock(Builder& b, SceneNode* parent, float x, float z, const Vec3& s, float yaw) {
    // Boulder = 3-4 overlapping, rotated, squashed spheres, partly buried: an irregular silhouette.
    PropRng rng(seedAt(x, z) ^ 0x70C4u);
    SceneNode* rock = b.group(parent, "Boulder", onGround(x, 0.0f, z), {0, yaw, 0}, "Boulder = cluster of spheres");
    const int n = 3 + static_cast<int>(rng.next() * 2.0f);
    for (int i = 0; i < n; ++i) {
        const float k = i == 0 ? 1.0f : rng.range(0.45f, 0.75f);
        const Vec3 sz{s.x * k * rng.range(0.8f, 1.1f), s.y * k * rng.range(0.7f, 1.0f), s.z * k * rng.range(0.8f, 1.1f)};
        const Vec3 off = i == 0 ? Vec3{0, 0, 0}
                                : Vec3{rng.range(-0.35f, 0.35f) * s.x, rng.range(-0.1f, 0.15f) * s.y, rng.range(-0.35f, 0.35f) * s.z};
        solid(b.part(rock, "Rock", P::Sphere, sz, {rng.range(-25.0f, 25.0f), rng.range(0.0f, 360.0f), rng.range(-25.0f, 25.0f)},
                     off + Vec3{0, sz.y * 0.22f, 0}, mats::stone(), "Boulder lobe"));
    }
}

bool vegetationAllowed(float x, float z, const std::vector<Footprint>& keepClear, float margin) {
    const float r = std::sqrt(x * x + z * z);
    if (r > kTreeMaxRadius || r < kBunkerClearance) return false;
    // Underground bunker footprint and the entrance trench strip (BaseLayout), with a margin.
    if (x > layout::kBunkerMinX - 1.0f && x < layout::kBunkerMaxX + 1.0f && z > layout::kBunkerMinZ - 1.0f && z < layout::kBunkerMaxZ + 1.0f) return false;
    if (std::fabs(x) < 4.2f && z > 3.0f && z < 14.5f) return false;
    if (z > 1.0f && std::fabs(x - trackCentreX(z)) < kTrackClearance) return false;
    if (std::fabs(z - creekCentreZ(x)) < kCreekClearance) return false;
    for (const Footprint& f : keepClear) {
        const float dx = x - f.x, dz = z - f.z;
        if (dx * dx + dz * dz < (f.radius + margin) * (f.radius + margin)) return false;
    }
    return true;
}

int addForestPatch(Builder& b, SceneNode* parent, float cx, float cz, float radius, int count, int roundTrees,
                   uint32_t seed, std::vector<Footprint>& keepClear) {
    PropRng rng(seed);
    SceneNode* patch = b.group(parent, "Forest patch", {0, 0, 0}, {0, 0, 0}, "Cluster of pines and round trees");
    int placed = 0;
    for (int attempt = 0; attempt < count * 40 && placed < count; ++attempt) {
        const float a = rng.range(0.0f, 2.0f * PI), d = radius * std::sqrt(rng.next());
        const float x = cx + d * std::cos(a), z = cz + d * std::sin(a);
        const bool round = placed >= count - roundTrees;
        const float own = round ? 1.3f : 1.0f;
        if (!vegetationAllowed(x, z, keepClear, own)) continue;
        if (round) addRoundTree(b, patch, x, z, rng.range(0.85f, 1.1f));
        else {
            const float pineYaw = rng.range(0.0f, 360.0f);   // yaw first, then height: a fixed order for every compiler
            const float pineHeight = rng.range(3.0f, 4.6f);
            addPine(b, patch, x, z, pineHeight, pineYaw);
        }
        keepClear.push_back({x, z, own});
        ++placed;
    }
    return placed;
}

}  // namespace ts
