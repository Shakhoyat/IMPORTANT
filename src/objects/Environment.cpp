#include "objects/Environment.h"

#include <vector>

#include "objects/Builder.h"
#include "objects/PropKit.h"
#include "objects/Structures.h"
#include "objects/Terrain.h"
#include "objects/Vegetation.h"

namespace ts {

Vec3 moonLightDirection() { return normalize(Vec3{-0.55f, -0.38f, 0.72f}); }

namespace {

using P = PrimitiveType;

// Compound layout (x, z in metres; bunker at the origin, door and track towards +Z, creek in the south).
constexpr float kTowerX = 10.5f, kTowerZ = 10.0f;
constexpr float kMastX = -13.0f, kMastZ = 6.0f;
constexpr float kTentX = -9.2f, kTentZ = -3.8f;

void addStructures(Builder& b, SceneNode* r) {
    addPerimeterFence(b, r);
    addWatchtower(b, r, kTowerX, kTowerZ, Vec3{-kTowerX, -7.0f, -kTowerZ});  // floodlight looks at the bunker
    addSandbagArc(b, r, 5.2f, 15.0f, 1.4f, 25.0f, 165.0f, 7, 3);   // guard post inside the gate
    addSandbagArc(b, r, -10.0f, 10.5f, 1.5f, -40.0f, 250.0f, 8, 2);  // machine-gun nest, opening to the south-east
    addCamoTent(b, r, kTentX, kTentZ, 20.0f);
    addSupplyCache(b, r, -3.9f, 2.0f, 15.0f, 2);   // original cache beside the bunker
    addSupplyCache(b, r, -6.6f, -6.0f, -25.0f, 0);  // pallet next to the tent
    addSupplyCache(b, r, 3.6f, -2.1f, 40.0f, 1);   // fuel for the bunker generator
    addRadioMast(b, r, kMastX, kMastZ);
    addPracticalLights(b, r);
}

std::vector<Footprint> structureFootprints() {
    return {{kTowerX, kTowerZ, 2.2f}, {5.2f, 15.0f, 2.0f}, {-10.0f, 10.5f, 2.2f}, {kTentX, kTentZ, 3.0f},
            {-3.9f, 2.0f, 1.6f},      {-6.6f, -6.0f, 1.5f}, {3.6f, -2.1f, 1.2f},    {kMastX, kMastZ, 4.2f},
            {-6.2f, 3.6f, 1.5f},      {5.9f, 5.1f, 1.5f},   {-4.6f, -6.6f, 1.5f},   {6.0f, 6.0f, 1.0f}};
}

void addVegetation(Builder& b, SceneNode* r) {
    // The three original round trees (kept for continuity with the v1 report figures).
    addRoundTree(b, r, -6.2f, 3.6f, 1.0f);
    addRoundTree(b, r, 5.9f, 5.1f, 0.85f);
    addRoundTree(b, r, -4.6f, -6.6f, 1.1f);

    std::vector<Footprint> clear = structureFootprints();
    addForestPatch(b, r, 14.0f, -3.0f, 3.2f, 7, 2, 11u, clear);   // east wood
    addForestPatch(b, r, -2.0f, -15.0f, 3.0f, 7, 1, 23u, clear);  // south, beyond the creek
    addForestPatch(b, r, -15.5f, -6.0f, 2.8f, 6, 2, 37u, clear);  // west wood
    addForestPatch(b, r, 13.5f, 4.5f, 2.2f, 4, 0, 41u, clear);    // grove below the watchtower
    addForestPatch(b, r, 8.5f, -13.5f, 2.5f, 5, 1, 53u, clear);   // south-east
    SceneNode* lone = b.group(r, "Lone pines", {0, 0, 0}, {0, 0, 0}, "Single trees in the open");
    addPine(b, lone, -6.5f, 13.0f, 4.2f, 10.0f);
    addPine(b, lone, 12.8f, -12.3f, 3.4f, 70.0f);
    addPine(b, lone, -9.5f, -14.5f, 3.8f, 140.0f);

    // Bushes along the creek banks, the berm and around the compound.
    struct BushSpot {
        float x, z, size, yaw;
    };
    const BushSpot bushes[] = {{-12.0f, -8.4f, 1.0f, 10},  {-7.5f, -8.6f, 0.8f, 60},  {-1.0f, -7.6f, 0.9f, 120},
                               {5.5f, -4.6f, 1.1f, 30},    {10.5f, -5.6f, 0.9f, 80},  {12.5f, -7.6f, 0.8f, 150},
                               {-15.8f, 3.5f, 1.0f, 45},   {-12.5f, 13.5f, 0.9f, 15}, {9.0f, 14.6f, 1.0f, 100},
                               {-4.2f, 16.3f, 0.9f, 70},   {16.2f, 1.0f, 1.1f, 20},   {3.2f, 9.5f, 0.7f, 140},
                               {-3.8f, 10.0f, 0.8f, 35},   {7.6f, -3.6f, 0.8f, 95}};
    SceneNode* shrubs = b.group(r, "Shrubs", {0, 0, 0}, {0, 0, 0}, "Bushes (low cover for the rover)");
    for (const BushSpot& s : bushes) addBush(b, shrubs, s.x, s.z, s.size, s.yaw);
}

void addRocks(Builder& b, SceneNode* r) {
    SceneNode* rocks = b.group(r, "Rocks", {0, 0, 0}, {0, 0, 0}, "Boulders and creek stones");
    addRock(b, rocks, 4.3f, -3.7f, {1.1f, 0.7f, 0.9f}, 20.0f);
    addRock(b, rocks, -3.3f, -4.1f, {0.8f, 0.55f, 0.7f}, 65.0f);
    addRock(b, rocks, 6.9f, -1.4f, {0.6f, 0.4f, 0.7f}, 10.0f);
    addRock(b, rocks, -7.1f, -1.1f, {0.9f, 0.6f, 0.8f}, 80.0f);
    addRock(b, rocks, 3.8f, 7.0f, {0.7f, 0.45f, 0.6f}, 35.0f);
    // Stones in the dry creek bed and an outcrop on the berm.
    for (float x : {-11.0f, -5.5f, 0.5f, 6.0f, 11.5f}) {
        const float z = creekCentreZ(x) + 0.3f * std::sin(3.0f * x);
        addRock(b, rocks, x, z, {0.55f, 0.35f, 0.5f}, 25.0f * x);
    }
    addRock(b, rocks, -16.0f, 7.5f, {1.6f, 0.9f, 1.2f}, 40.0f);
    addRock(b, rocks, 11.0f, -15.2f, {1.3f, 0.8f, 1.1f}, 75.0f);
    addRock(b, rocks, 16.4f, 7.2f, {1.0f, 0.6f, 0.9f}, 15.0f);
}

}  // namespace

std::unique_ptr<SceneNode> buildEnvironment() {
    Builder b("Environment", "ENV", ShadingModel::Gouraud);
    auto root = b.root("Environment", {0, 0, 0}, "Props that give cover and depth");
    SceneNode* r = root.get();
    addStructures(b, r);
    addVegetation(b, r);
    addRocks(b, r);

    // The moon: a large emissive sphere far along the reversed moonlight direction.
    Material moon = makeMaterial({0.85f, 0.88f, 0.95f}, 0.25f, Surface::Plain, 0.9f, 0.0f, 1.0f);
    moon.fogged = false;
    SceneNode* m = b.part(r, "Moon", P::Sphere, {14, 14, 14}, {0, 0, 0}, moonLightDirection() * -300.0f, moon,
                          "Visual source of directional light L0");
    m->explodable = false;
    m->obstacle = false;
    return root;
}

}  // namespace ts
