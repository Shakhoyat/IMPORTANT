#include "objects/Structures.h"

#include <algorithm>
#include <cmath>

#include "objects/PropKit.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

using P = PrimitiveType;

constexpr int kFencePosts = 20;           // one every 18 degrees
constexpr float kFenceFirstDeg = 99.0f;   // the bay 81..99 degrees (centred on +Z) holds the gate
constexpr float kWireHeights[3] = {0.35f, 0.75f, 1.15f};

SceneNode* solid(SceneNode* n) {
    n->occluder = true;
    return n;
}

Vec3 horizontalUnit(const Vec3& v) { return normalize(Vec3{v.x, 0.0f, v.z}); }

// One leaf of the double gate, hinged at 'hinge' (ground point) and swung to direction 'dir'.
void addGateLeaf(Builder& b, SceneNode* fence, const Vec3& hinge, const Vec3& dir, float len) {
    const Material steel = mats::galvanized();
    SceneNode* leaf = b.group(fence, "Gate leaf", hinge, {0, degrees(std::atan2(dir.x, dir.z)), 0},
                              "Swing gate hinged on the gate post");
    b.part(leaf, "Gate stile", P::Cylinder, {0.06f, 0.06f, 1.2f}, {90, 0, 0}, {0, 0.8f, len}, steel, "Outer upright");
    strut(b, leaf, "Gate rail top", P::Cylinder, {0, 1.35f, 0.1f}, {0, 1.35f, len}, 0.05f, steel, "Top rail");
    strut(b, leaf, "Gate rail bottom", P::Cylinder, {0, 0.25f, 0.1f}, {0, 0.25f, len}, 0.05f, steel, "Bottom rail");
    strut(b, leaf, "Gate brace", P::Cylinder, {0, 0.25f, 0.1f}, {0, 1.35f, len}, 0.04f, steel, "Diagonal brace");
}

}  // namespace

void addPerimeterFence(Builder& b, SceneNode* parent) {
    SceneNode* fence = b.group(parent, "Perimeter fence", {0, 0, 0}, {0, 0, 0}, "Posts, three wire strands, gate");
    const Material wood = mats::fenceWood(), steel = mats::galvanized(), wire = mats::wire();

    Vec3 base[kFencePosts];
    for (int i = 0; i < kFencePosts; ++i) {
        const float a = radians(kFenceFirstDeg + 18.0f * static_cast<float>(i));
        base[i] = onGround(kFenceRadius * std::cos(a), 0.0f, kFenceRadius * std::sin(a));
        const bool gatePost = (i == 0 || i == kFencePosts - 1);
        const float h = gatePost ? 1.75f : 1.55f;  // 0.3 m of each post is buried
        b.part(fence, gatePost ? "Gate post" : "Fence post", P::Cylinder,
               {gatePost ? 0.18f : 0.10f, gatePost ? 0.18f : 0.10f, h}, {90, 0, 0},
               base[i] + Vec3{0, 0.5f * h - 0.3f, 0}, gatePost ? steel : wood, "Perimeter post");
    }
    // Wires across every bay except the gate bay (between the last and the first post).
    for (int i = 0; i + 1 < kFencePosts; ++i) {
        for (float h : kWireHeights) {
            SceneNode* w = strut(b, fence, "Fence wire", P::Cylinder, base[i] + Vec3{0, h, 0},
                                 base[i + 1] + Vec3{0, h, 0}, 0.012f, wire, "Wire strand");
            w->obstacle = false;
        }
    }

    // Double gate across the vehicle track: one leaf swung wide open into the compound, the other ajar.
    const Vec3 hingeE = base[kFencePosts - 1], hingeW = base[0];
    const Vec3 inward = horizontalUnit(Vec3{0, 0, 0} - (hingeE + hingeW) * 0.5f);
    const float leafLen = 0.5f * length(hingeW - hingeE) - 0.15f;
    const Vec3 closedE = horizontalUnit(hingeW - hingeE), closedW = closedE * -1.0f;
    const float openE = radians(70.0f), openW = radians(15.0f);
    addGateLeaf(b, fence, hingeE, closedE * std::cos(openE) + inward * std::sin(openE), leafLen);
    addGateLeaf(b, fence, hingeW, closedW * std::cos(openW) + inward * std::sin(openW), leafLen);

    const Vec3 outward = inward * -1.0f;
    b.part(fence, "Warning sign", P::Cube, {0.55f, 0.38f, 0.03f}, {0, degrees(std::atan2(outward.x, outward.z)), 0},
           hingeE + outward * 0.12f + Vec3{0, 1.05f, 0}, mats::warningSign(), "Restricted area sign");
}

void addWatchtower(Builder& b, SceneNode* parent, float x, float z, const Vec3& aimDir) {
    const Material steel = mats::towerSteel(), roof = mats::galvanized(), planks = mats::crateWood();
    SceneNode* t = b.group(parent, "Watchtower", onGround(x, 0.0f, z), {0, 0, 0}, "Guard tower with floodlight");
    const float sx = aimDir.x >= 0.0f ? 1.0f : -1.0f, sz = aimDir.z >= 0.0f ? 1.0f : -1.0f;

    // Tapered legs from 1.05 m (below ground) to 0.85 m (under the roof) off the centre line.
    auto legPos = [](float lx, float lz, float y) {
        const float off = 1.05f - 0.2f * (y + 0.3f) / 5.3f;
        return Vec3{lx * off, y, lz * off};
    };
    const float corners[4][2] = {{1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
    for (const auto& c : corners)
        solid(strut(b, t, "Tower leg", P::Cylinder, legPos(c[0], c[1], -0.3f), legPos(c[0], c[1], 5.0f), 0.14f, steel,
                    "Tapered steel leg"));
    // One diagonal brace per face.
    for (int i = 0; i < 4; ++i) {
        const auto& c0 = corners[i];
        const auto& c1 = corners[(i + 1) % 4];
        strut(b, t, "Tower brace", P::Cylinder, legPos(c0[0], c0[1], 0.2f), legPos(c1[0], c1[1], 3.0f), 0.08f, steel,
              "Cross brace");
    }

    solid(b.part(t, "Platform", P::Cube, {2.4f, 0.12f, 2.4f}, {0, 0, 0}, {0, 3.1f, 0}, planks, "Guard platform"));
    solid(b.part(t, "Parapet front", P::Cube, {2.3f, 0.9f, 0.06f}, {0, 0, 0}, {0, 3.61f, sz * 1.12f}, planks,
                 "Plank parapet"));
    solid(b.part(t, "Parapet side", P::Cube, {0.06f, 0.9f, 2.3f}, {0, 0, 0}, {1.12f, 3.61f, 0}, planks, "Plank parapet"));
    solid(b.part(t, "Parapet side", P::Cube, {0.06f, 0.9f, 2.3f}, {0, 0, 0}, {-1.12f, 3.61f, 0}, planks, "Plank parapet"));
    strut(b, t, "Hand rail", P::Cylinder, {-1.1f, 4.0f, -sz * 1.12f}, {1.1f, 4.0f, -sz * 1.12f}, 0.05f, steel,
          "Rail above the ladder");
    solid(b.part(t, "Roof slab", P::Cube, {2.5f, 0.08f, 2.5f}, {0, 0, 0}, {0, 5.02f, 0}, roof, "Roof plate"));
    solid(b.part(t, "Roof cone", P::Cone, {3.4f, 3.4f, 0.9f}, {-90, 45, 0}, {0, 5.51f, 0}, roof, "Conical roof"));

    // Ladder on the face away from the floodlight.
    for (float side : {-0.25f, 0.25f})
        strut(b, t, "Ladder rail", P::Cylinder, {side, -0.2f, -sz * 1.45f}, {side, 3.15f, -sz * 1.08f}, 0.05f, steel,
              "Ladder stringer");
    for (int i = 0; i < 6; ++i) {
        const float k = (static_cast<float>(i) + 0.7f) / 6.5f;
        const Vec3 c{0.0f, lerpf(-0.2f, 3.15f, k), -sz * lerpf(1.45f, 1.08f, k)};
        b.part(t, "Ladder rung", P::Cylinder, {0.035f, 0.035f, 0.5f}, {0, 90, 0}, c, steel, "Rung");
    }

    // Floodlight on the platform corner facing the aim direction.
    const Vec3 corner{sx * 0.95f, 4.06f, sz * 0.95f};
    const Vec3 aim = normalize(aimDir);
    b.part(t, "Floodlight bracket", P::Cylinder, {0.06f, 0.06f, 0.44f}, {90, 0, 0}, corner + Vec3{0, 0.22f, 0}, steel,
           "Lamp post");
    const Vec3 head = corner + Vec3{0, 0.6f, 0};
    b.part(t, "Floodlight housing", P::Cylinder, {0.34f, 0.34f, 0.32f}, alignZ(aim), head, steel, "Lamp body");
    b.part(t, "Floodlight lens", P::Sphere, {0.30f, 0.30f, 0.06f}, alignZ(aim), head + aim * 0.17f, mats::floodLens(),
           "Floodlight lens (emissive)");
}

void addSandbagArc(Builder& b, SceneNode* parent, float cx, float cz, float radius, float startDeg, float endDeg,
                   int bagsBottom, int rows) {
    const Material sand = mats::sandbag();
    SceneNode* wall = b.group(parent, "Sandbag wall", {0, 0, 0}, {0, 0, 0}, "Curved sandbag wall");
    const float step = (endDeg - startDeg) / static_cast<float>(bagsBottom);
    for (int r = 0; r < rows; ++r) {
        for (int k = 0; k < bagsBottom - r; ++k) {
            const float a = radians(startDeg + step * (static_cast<float>(k) + 0.5f + 0.5f * static_cast<float>(r)));
            const float x = cx + radius * std::cos(a), z = cz + radius * std::sin(a);
            const float yaw = degrees(std::atan2(-std::sin(a), std::cos(a)));  // bag axis along the arc tangent
            solid(b.part(wall, "Sandbag", P::Capsule, {0.26f, 0.17f, 0.5f}, {0, yaw, 0},
                         onGround(x, 0.08f + 0.14f * static_cast<float>(r), z), sand, "Sandbag"));
        }
    }
}

void addPracticalLights(Builder& b, SceneNode* parent) {
    const Material steel = mats::towerSteel();
    // Hooded dim red lamps pointing straight down (light discipline); only the one at the gate end is a practical light.
    const Material redLamp = asLight(makeMaterial({1.00f, 0.20f, 0.08f}, 0.45f, Surface::Glass, 0.1f, 0.0f, 1.0f), 1.1f, 6.0f, 50.0f);
    const Material redMark = makeMaterial({1.00f, 0.20f, 0.08f}, 0.40f, Surface::Glass, 0.1f, 0.0f, 0.5f);
    SceneNode* lamps = b.group(parent, "Lamp posts", {0, 0, 0}, {0, 0, 0}, "Track lighting");
    for (float z : {6.5f, 13.0f}) {
        const float x = 3.6f;  // east of the trench strip (|x| < 2.5) that holds the bunker ramp
        SceneNode* post = b.group(lamps, "Lamp post", onGround(x, 0.0f, z), {0, 0, 0}, "Sodium street lamp");
        solid(b.part(post, "Lamp pole", P::Cylinder, {0.09f, 0.09f, 3.2f}, {90, 0, 0}, {0, 1.6f, 0}, steel, "Pole"));
        b.part(post, "Lamp arm", P::Cube, {0.4f, 0.05f, 0.05f}, {0, 0, 0}, {-0.2f, 3.15f, 0}, steel, "Arm over the track");
        b.part(post, "Lamp head", P::Cube, {0.34f, 0.08f, 0.2f}, {0, 0, 0}, {-0.42f, 3.12f, 0}, steel, "Lamp head");
        b.part(post, "Dim red lamp", P::Sphere, {0.16f, 0.16f, 0.05f}, {90, 0, 0}, {-0.42f, 3.05f, 0}, z > 10.0f ? redLamp : redMark,
               z > 10.0f ? "Dim red hooded lamp pointing down (practical light)" : "Dim red hooded lamp (emissive only)");
    }
    // Fire barrel inside the guard post's sandbag arc: warm, flickering, visible in both views.
    SceneNode* fire = b.group(parent, "Fire barrel", onGround(5.3f, 0.0f, 15.35f), {0, 0, 0}, "Guards' brazier");
    solid(b.part(fire, "Barrel", P::Cylinder, {0.5f, 0.5f, 0.78f}, {90, 0, 0}, {0, 0.39f, 0}, mats::rustDrum(), "Brazier drum"));
    Material embers = asLight(makeMaterial({1.00f, 0.42f, 0.10f}, 0.74f, Surface::Plain, 0.9f, 0.0f, 1.0f), 1.5f, 5.0f);  // banked embers, hidden in the sandbag arc
    b.part(fire, "Fire embers", P::Sphere, {0.42f, 0.16f, 0.42f}, {0, 0, 0}, {0, 0.8f, 0}, embers,
           "Burning wood (flickering practical light)");
}

void addCamoTent(Builder& b, SceneNode* parent, float x, float z, float yawDeg) {
    const Material canvas = mats::canvas(), net = mats::camoNet(), pole = mats::fenceWood();
    SceneNode* t = b.group(parent, "Camouflaged tent", onGround(x, 0.0f, z), {0, yawDeg, 0}, "A-frame tent under a net");
    strut(b, t, "Ridge pole", P::Cylinder, {-1.6f, 1.7f, 0}, {1.6f, 1.7f, 0}, 0.07f, pole, "Ridge pole");
    for (float px : {-1.5f, 1.5f})
        solid(b.part(t, "Tent pole", P::Cylinder, {0.07f, 0.07f, 1.9f}, {90, 0, 0}, {px, 0.75f, 0}, pole, "Upright"));
    // Roof panels from the ridge (y 1.7) to the eaves (y 0.25, z +-1.15).
    for (float side : {-1.0f, 1.0f}) {
        const Vec3 ridge{0, 1.7f, 0}, eave{0, 0.25f, side * 1.15f};
        solid(b.part(t, "Tent roof", P::Cube, {3.1f, 0.04f, length(eave - ridge)}, alignZ(eave - ridge),
                     (ridge + eave) * 0.5f, canvas, "Canvas roof panel"));
    }
    solid(b.part(t, "Tent back wall", P::Cube, {0.04f, 1.2f, 1.2f}, {45, 0, 0}, {-1.55f, 0.85f, 0}, canvas,
                 "Gable end"));
    // Camouflage net: lumpy flattened spheres on four poles.
    solid(b.part(t, "Camo net", P::Sphere, {4.6f, 1.0f, 3.8f}, {0, 0, 0}, {0, 1.55f, 0}, net, "Net canopy"));
    solid(b.part(t, "Camo net", P::Sphere, {2.4f, 0.7f, 2.2f}, {0, 25, 0}, {1.3f, 1.62f, 0.6f}, net, "Net sag"));
    solid(b.part(t, "Camo net", P::Sphere, {2.2f, 0.6f, 2.0f}, {0, -20, 0}, {-1.2f, 1.58f, -0.7f}, net, "Net sag"));
    for (float px : {-2.1f, 2.1f})
        for (float pz : {-1.7f, 1.7f})
            b.part(t, "Net pole", P::Cylinder, {0.05f, 0.05f, 1.6f}, {90, 0, 0}, {px, 0.6f, pz}, pole, "Net support");
    const Material lantern = makeMaterial({0.10f, 0.08f, 0.05f}, 0.30f, Surface::Glass, 0.2f, 0.0f, 0.0f);
    b.part(t, "Lantern", P::Sphere, {0.12f, 0.16f, 0.12f}, {0, 0, 0}, {0.9f, 1.42f, 0.0f}, lantern,
           "Oil lantern hanging from the ridge, shuttered and dark (light discipline)");
}

namespace {
// Stencilled lettering block on top of a crate: a flush dark-yellow Paint slab.
void stencil(Builder& b, SceneNode* c, const Vec3& crateTop, float yaw) {
    const Material ink = makeMaterial({0.55f, 0.50f, 0.12f}, 0.28f, Surface::Paint, 0.6f, 0.0f);
    b.part(c, "Crate stencil", P::Cube, {0.34f, 0.006f, 0.12f}, {0, yaw, 0}, crateTop + Vec3{0, 0.004f, 0}, ink, "Stencilled ammunition marking");
}
}  // namespace

void addSupplyCache(Builder& b, SceneNode* parent, float x, float z, float yawDeg, int variant) {
    const Material wood = mats::crateWood(), olive = mats::oliveDrum(), rust = mats::rustDrum();
    SceneNode* c = b.group(parent, "Supply cache", onGround(x, 0.0f, z), {0, yawDeg, 0}, "Crates and fuel drums");
    const Vec3 drum{0.4f, 0.4f, 0.6f};
    if (variant == 0) {
        solid(b.part(c, "Pallet", P::Cube, {1.3f, 0.12f, 1.1f}, {0, 0, 0}, {0, 0.06f, 0}, wood, "Wooden pallet"));
        solid(b.part(c, "Crate", P::Cube, {0.6f, 0.6f, 0.6f}, {0, 0, 0}, {-0.32f, 0.42f, -0.22f}, wood, "Ammo crate"));
        solid(b.part(c, "Crate", P::Cube, {0.6f, 0.6f, 0.6f}, {0, 6, 0}, {0.32f, 0.42f, -0.2f}, wood, "Ammo crate"));
        solid(b.part(c, "Crate", P::Cube, {0.55f, 0.45f, 0.55f}, {0, -10, 0}, {0.0f, 0.945f, -0.2f}, wood, "Ammo crate"));
        stencil(b, c, {0.0f, 1.17f, -0.2f}, -10.0f);
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {90, 0, 0}, {0.95f, 0.3f, 0.2f}, olive, "Fuel drum"));
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {90, 0, 0}, {0.95f, 0.3f, -0.3f}, rust, "Fuel drum"));
    } else if (variant == 1) {
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {90, 0, 0}, {0.0f, 0.3f, 0.0f}, olive, "Fuel drum"));
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {90, 0, 0}, {0.45f, 0.3f, 0.1f}, olive, "Fuel drum"));
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {90, 0, 0}, {0.2f, 0.3f, -0.42f}, rust, "Fuel drum"));
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {0, 30, 0}, {-0.6f, 0.2f, -0.2f}, rust, "Drum on its side"));
    } else {
        solid(b.part(c, "Crate", P::Cube, {0.6f, 0.6f, 0.6f}, {0, 0, 0}, {0, 0.3f, 0}, wood, "Supply crate"));
        solid(b.part(c, "Crate", P::Cube, {0.6f, 0.6f, 0.6f}, {0, 12, 0}, {0.65f, 0.3f, 0.05f}, wood, "Supply crate"));
        solid(b.part(c, "Crate", P::Cube, {0.55f, 0.5f, 0.55f}, {0, -8, 0}, {0.3f, 0.85f, 0.0f}, wood, "Supply crate"));
        stencil(b, c, {0.3f, 1.10f, 0.0f}, -8.0f);
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {90, 0, 0}, {-0.55f, 0.3f, 0.75f}, olive, "Fuel drum"));
        solid(b.part(c, "Fuel drum", P::Cylinder, drum, {90, 0, 0}, {-0.05f, 0.3f, 1.05f}, olive, "Fuel drum"));
    }
}

void addRadioMast(Builder& b, SceneNode* parent, float x, float z) {
    const Material steel = mats::towerSteel(), wire = mats::wire(), pad = mats::concrete();
    const Material shelter = makeMaterial({0.30f, 0.33f, 0.26f}, 0.45f, Surface::Paint, 0.5f, 0.5f);  // warm electronics
    SceneNode* m = b.group(parent, "Radio mast", onGround(x, 0.0f, z), {0, 0, 0}, "Guyed lattice mast");
    solid(b.part(m, "Mast footing", P::Cube, {1.3f, 0.2f, 1.3f}, {0, 0, 0}, {0, 0.02f, 0}, pad, "Concrete footing"));
    solid(b.part(m, "Equipment shelter", P::Cube, {1.1f, 1.0f, 0.9f}, {0, 0, 0}, {1.5f, 0.45f, 0.2f}, shelter,
                 "Radio equipment shelter"));

    constexpr float kTop = 9.0f;
    auto legPos = [](int k, float y) {
        const float a = radians(90.0f + 120.0f * static_cast<float>(k));
        const float r = lerpf(0.40f, 0.14f, y / kTop);
        return Vec3{r * std::cos(a), y, r * std::sin(a)};
    };
    for (int k = 0; k < 3; ++k)
        solid(strut(b, m, "Mast leg", P::Cylinder, legPos(k, 0.0f), legPos(k, kTop), 0.07f, steel, "Lattice leg"));
    for (int j = 0; j < 6; ++j) {
        const float y0 = 0.4f + 1.4f * static_cast<float>(j);
        strut(b, m, "Mast brace", P::Cylinder, legPos(j % 3, y0), legPos((j + 1) % 3, y0 + 1.4f), 0.04f, steel,
              "Zig-zag lattice brace");
    }
    b.part(m, "Whip antenna", P::Cylinder, {0.03f, 0.03f, 1.4f}, {90, 0, 0}, {0, kTop + 0.7f, 0}, steel, "Antenna");
    const Material red = mats::aviationLight();
    SceneNode* l = b.part(m, "Aviation light", P::Sphere, {0.18f, 0.18f, 0.18f}, {0, 0, 0}, {0, kTop + 0.05f, 0}, red,
                          "Blinking red obstruction light");
    l->animation = "Blinks 0.5 s on / 1.0 s off";
    l = b.part(m, "Aviation light", P::Sphere, {0.15f, 0.15f, 0.15f}, {0, 0, 0}, {0, 4.6f, 0}, red,
               "Blinking red obstruction light");
    l->animation = "Blinks 0.5 s on / 1.0 s off";

    // Three guy wires from 7 m to concrete anchors 3.6 m out.
    for (int k = 0; k < 3; ++k) {
        const float a = radians(30.0f + 120.0f * static_cast<float>(k));
        const float ax = 3.6f * std::cos(a), az = 3.6f * std::sin(a);
        const float ay = terrainHeight(x + ax, z + az) - terrainHeight(x, z);
        SceneNode* w = strut(b, m, "Guy wire", P::Cylinder, {0.0f, 7.0f, 0.0f}, {ax, ay + 0.1f, az}, 0.015f, wire,
                             "Guy wire");
        w->obstacle = false;
        solid(b.part(m, "Guy anchor", P::Cube, {0.3f, 0.25f, 0.3f}, {0, 0, 0}, {ax, ay + 0.05f, az}, pad, "Anchor block"));
    }
}


void addMilitaryDressing(Builder& b, SceneNode* parent) {
    SceneNode* m = b.group(parent, "Military dressing", {0, 0, 0}, {0, 0, 0}, "HESCO blocks, jersey barriers, fighting positions, signs");
    const Material hesco = makeMaterial({0.27f, 0.25f, 0.19f}, 0.22f, Surface::Hesco, 0.92f, 0.0f);
    const Material conc = mats::concrete(), steel = mats::towerSteel();
    // HESCO blocks: a wall of three cells beside the road and a pair near the machine-gun nest.
    struct H { float x, z, yaw; int n, tiers; };
    for (const H& h : {H{-6.4f, 17.0f, 0.0f, 3, 2}, H{-12.8f, 8.2f, 60.0f, 2, 1}}) {
        SceneNode* g = b.group(m, "HESCO blocks", onGround(h.x, 0.0f, h.z), {0, h.yaw, 0}, "Sand-filled bastion cells");
        for (int t = 0; t < h.tiers; ++t)
            for (int i = 0; i < h.n; ++i)
                solid(b.part(g, "HESCO cell", P::Cube, {1.03f, 1.4f, 1.06f}, {0, 0, 0},
                             {(static_cast<float>(i) - 0.5f * (h.n - 1)) * 1.05f, 0.7f + 1.4f * t, 0}, hesco, "Wire-mesh cell filled with sand"));
    }
    // Jersey barriers: chicane inside the inner gate.
    for (const Vec3 p : {Vec3{3.3f, 0, 17.6f}, Vec3{-3.3f, 0, 16.2f}}) {
        SceneNode* j = b.group(m, "Jersey barrier", onGround(p.x, 0.0f, p.z), {0, 0, 0}, "Precast vehicle barrier");
        solid(b.part(j, "Jersey foot", P::Cube, {0.6f, 0.3f, 3.0f}, {0, 0, 0}, {0, 0.15f, 0}, conc, "Wide base"));
        solid(b.part(j, "Jersey body", P::Cube, {0.3f, 0.55f, 2.96f}, {0, 0, 0}, {0, 0.575f, 0}, conc, "Sloped upper part"));
    }
    // Fighting position: sandbag ring with a crate stack, east of the bunker.
    addSandbagArc(b, m, 8.2f, 1.6f, 1.3f, 200.0f, 520.0f, 9, 3);
    addSupplyCache(b, m, 9.9f, -1.6f, 70.0f, 2);
    // Vehicle-track sign beside the entrance ramp.
    SceneNode* sg = b.group(m, "Track sign", onGround(-3.4f, 0.0f, 7.5f), {0, 0, 0}, "TRACKED VEHICLES sign");
    solid(b.part(sg, "Sign post", P::Cylinder, {0.07f, 0.07f, 1.9f}, {90, 0, 0}, {0, 0.95f, 0}, steel, "Sign pole"));
    b.part(sg, "Sign board", P::Cube, {0.8f, 0.55f, 0.03f}, {0, 0, 0}, {0, 1.75f, 0.04f}, mats::warningSign(), "Tracked-vehicle route sign");
    b.part(sg, "Sign arrow", P::Cube, {0.45f, 0.08f, 0.01f}, {0, 0, 0}, {0, 1.75f, 0.065f}, makeMaterial({0.03f, 0.03f, 0.03f}, 0.2f, Surface::Paint, 0.6f, 0.0f), "Direction bar");
    b.part(sg, "Sign arrow head", P::Cube, {0.16f, 0.16f, 0.01f}, {0, 0, 45}, {0.22f, 1.75f, 0.065f}, makeMaterial({0.03f, 0.03f, 0.03f}, 0.2f, Surface::Paint, 0.6f, 0.0f), "Arrow head");
}

}  // namespace ts

// ---------------------------------------------------------------------------------------------------------
// v5 camp helpers
// ---------------------------------------------------------------------------------------------------------
namespace ts {

SceneNode* addCamoNet(Builder& b, SceneNode* parent, const Vec3& ground, float yawDeg, float width, float depth,
                      float poleHeight, float sag, uint32_t seed, float holeHalfX, float holeZ0, float holeZ1) {
    using Pm = PrimitiveType;
    sag = std::max(0.0f, sag);
    poleHeight = std::max(poleHeight, kNetMinHeight + 0.35f + sag);  // lowest panel centre >= 3.55 m
    const Material camo = makeMaterial({0.16f, 0.20f, 0.12f}, 0.20f, Surface::Camo, 0.88f, 0.0f);
    const Material woven = makeMaterial({0.12f, 0.15f, 0.09f}, 0.20f, Surface::Camo, 0.92f, 0.0f);
    const Material pole = mats::fenceWood(), rope = mats::wire();
    SceneNode* g = b.group(parent, "Camouflage net", ground, {0, yawDeg, 0},
                           "Camouflage net on poles: thin cubes sagging between the supports, with gaps");
    PropRng rng(seed);
    const int nx = std::max(2, static_cast<int>(std::lround(width / 1.15f)));
    const int nz = std::max(2, static_cast<int>(std::lround(depth / 1.15f)));
    const float cw = width / static_cast<float>(nx), cd = depth / static_cast<float>(nz);
    auto height = [&](float u, float v) { return poleHeight - sag * std::sin(PI * u) * std::sin(PI * v); };
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < nz; ++j) {
            if (rng.next() < 0.12f) continue;  // open gap in the netting
            const float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(nx);
            const float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(nz);
            const float x = (u - 0.5f) * width, z = (v - 0.5f) * depth;
            if (holeHalfX > 0.0f && std::fabs(x) < holeHalfX && z > holeZ0 && z < holeZ1) continue;
            const float dhdu = -sag * PI * std::cos(PI * u) * std::sin(PI * v);
            const float dhdv = -sag * PI * std::sin(PI * u) * std::cos(PI * v);
            const float pitch = degrees(std::atan(-dhdv / depth));  // about X: raises or lowers +Z
            const float roll = degrees(std::atan(dhdu / width));    // about Z
            const float s = rng.range(0.82f, 0.98f);
            SceneNode* n = b.part(g, "Camo net panel", Pm::Cube, {cw * s, 0.035f, cd * s},
                                  {pitch, rng.range(-6.0f, 6.0f), roll}, {x, height(u, v), z},
                                  (i + j) % 3 == 1 ? woven : camo, "Netting panel between the poles");
            n->obstacle = false;
        }
    // Poles at the four corners (and mid-edge for long sides), ropes along the edges.
    std::vector<Vec3> tops;
    const int px = width > 5.5f ? 2 : 1, pz = depth > 5.5f ? 2 : 1;
    for (int i = 0; i <= px; ++i)
        for (int j = 0; j <= pz; ++j) {
            const bool edge = (i == 0 || i == px || j == 0 || j == pz);
            if (!edge) continue;
            const float x = (static_cast<float>(i) / static_cast<float>(px) - 0.5f) * width;
            const float z = (static_cast<float>(j) / static_cast<float>(pz) - 0.5f) * depth;
            SceneNode* p = b.part(g, "Net pole", Pm::Cylinder, {0.10f, 0.10f, poleHeight + 0.1f}, {90, 0, 0},
                                  {x, 0.5f * (poleHeight + 0.1f), z}, pole, "Net support pole");
            p->obstacle = false;
        }
    const Vec3 c[4] = {{-0.5f * width, poleHeight + 0.05f, -0.5f * depth}, {0.5f * width, poleHeight + 0.05f, -0.5f * depth},
                       {0.5f * width, poleHeight + 0.05f, 0.5f * depth}, {-0.5f * width, poleHeight + 0.05f, 0.5f * depth}};
    for (int k = 0; k < 4; ++k) {
        SceneNode* r = strut(b, g, "Net edge rope", Pm::Cylinder, c[k], c[(k + 1) % 4], 0.02f, rope, "Rope tensioning the net edge");
        r->obstacle = false;
    }
    return g;
}

SceneNode* addRevetment(Builder& b, SceneNode* parent, const Vec3& ground, float yawDeg, float halfW, float halfD,
                        float height, bool frontWall) {
    using Pm = PrimitiveType;
    const Material earth = makeMaterial({0.13f, 0.105f, 0.075f}, 0.15f, Surface::Ground, 0.95f, 0.0f);
    const Material bag = mats::sandbag();
    SceneNode* g = b.group(parent, "Earth revetment", ground, {0, yawDeg, 0},
                           "U-shaped earth wall around a vehicle bay, open at the rear (local -Z)");
    // One straight wall running along local Z centred on (cx, cz): 'len' long, inner face at x = cx - side * 0.45.
    auto wall = [&](float cx, float cz, float len, float outward /* +1: outer face towards +X */) {
        const float h = height;
        SceneNode* core = b.part(g, "Revetment core", Pm::Cube, {0.8f, 0.85f * h, len}, {0, 0, 0}, {cx, 0.425f * h, cz}, earth,
                                 "Compacted earth core of the wall");
        (void)core;
        // Outer slope: a slab leaning against the core, top towards the core (roll about the wall axis).
        b.part(g, "Revetment slope (outer)", Pm::Cube, {0.55f, 1.05f * h, len}, {0, 0, outward * 28.0f},
               {cx + outward * 0.62f, 0.45f * h, cz}, earth, "Outer slope of the earth wall");
        // Inner face: steeper slab.
        b.part(g, "Revetment slope (inner)", Pm::Cube, {0.35f, 0.95f * h, len}, {0, 0, -outward * 10.0f},
               {cx - outward * 0.47f, 0.47f * h, cz}, earth, "Steep inner face of the wall");
        // Sandbag crest, one running course.
        const int n = std::max(2, static_cast<int>(len / 0.55f));
        for (int k = 0; k < n; ++k)
            b.part(g, "Revetment sandbag", Pm::Capsule, {0.28f, 0.17f, 0.5f}, {0, 90, 0},
                   {cx, 0.85f * h + 0.07f, cz + (static_cast<float>(k) + 0.5f) / static_cast<float>(n) * len - 0.5f * len}, bag,
                   "Sandbag on the crest of the revetment");
    };
    const float wx = halfW + 0.45f;
    wall(-wx, 0.0f, 2.0f * halfD + 1.2f, -1.0f);  // left (outer side towards -X)
    wall(wx, 0.0f, 2.0f * halfD + 1.2f, 1.0f);    // right
    // Front wall across +Z (rotate the same profile by 90 deg: built as three cubes along X).
    if (frontWall) {
        const float h = height, zf = halfD + 0.45f, len = 2.0f * wx + 1.2f;
        b.part(g, "Revetment core", Pm::Cube, {len, 0.85f * h, 0.8f}, {0, 0, 0}, {0, 0.425f * h, zf}, earth, "Compacted earth core of the front wall");
        b.part(g, "Revetment slope (outer)", Pm::Cube, {len, 1.05f * h, 0.55f}, {-28, 0, 0}, {0, 0.45f * h, zf + 0.62f}, earth, "Outer slope of the front wall");
        b.part(g, "Revetment slope (inner)", Pm::Cube, {len - 1.0f, 0.95f * h, 0.35f}, {10, 0, 0}, {0, 0.47f * h, zf - 0.47f}, earth, "Steep inner face of the front wall");
        const int n = std::max(2, static_cast<int>(len / 0.55f));
        for (int k = 0; k < n; ++k)
            b.part(g, "Revetment sandbag", Pm::Capsule, {0.28f, 0.17f, 0.5f}, {0, 0, 0},
                   {(static_cast<float>(k) + 0.5f) / static_cast<float>(n) * len - 0.5f * len, 0.85f * h + 0.07f, zf}, bag,
                   "Sandbag on the crest of the revetment");
    }
    return g;
}

void addCutBranches(Builder& b, SceneNode* parent, const Vec3& ground, float yawDeg, float dist, float length,
                    uint32_t seed) {
    using Pm = PrimitiveType;
    PropRng rng(seed);
    const Material leaf = mats::pineNeedles(), wood = mats::bark(), green = mats::leaves();
    SceneNode* g = b.group(parent, "Cut branches", ground, {0, yawDeg, 0}, "Branches and brush propped against a vehicle to break its outline");
    const int n = std::max(3, static_cast<int>(length / 0.9f));
    for (int i = 0; i < n; ++i) {
        const float z = (static_cast<float>(i) / static_cast<float>(n - 1) - 0.5f) * length + rng.range(-0.2f, 0.2f);
        const float lean = rng.range(14.0f, 28.0f);  // top leans towards the vehicle (-X of the group)
        const float len = rng.range(1.5f, 2.2f);
        const Vec3 base{dist + rng.range(-0.15f, 0.3f), 0.0f, z};
        const Vec3 top = base + Vec3{-std::sin(radians(lean)) * len, std::cos(radians(lean)) * len, rng.range(-0.2f, 0.2f)};
        SceneNode* st = strut(b, g, "Cut branch stem", Pm::Cylinder, base, top, 0.05f, wood, "Cut branch leaning on the vehicle");
        st->obstacle = false;
        const Vec3 mid = (base + top) * 0.5f;
        const float r = rng.range(0.45f, 0.65f);
        SceneNode* f = b.part(g, "Cut branch foliage", Pm::Sphere, {r, 1.2f * len * 0.7f, r}, {0, rng.range(0, 90.0f), lean}, mid, i % 2 ? leaf : green,
                              "Leafy crown of the cut branch");
        f->obstacle = false;
        b.part(g, "Cut branch tip", Pm::Cone, {0.35f, 0.35f, 0.5f}, {-90, 0, lean}, top + Vec3{0, 0.1f, 0}, leaf, "Needle tuft at the tip")->obstacle = false;
    }
    // Brush pile at the foot.
    for (int i = 0; i < 3; ++i) {
        const float r = rng.range(0.35f, 0.55f);
        b.part(g, "Brush clump", Pm::Sphere, {2.0f * r, 1.2f * r, 2.0f * r}, {0, rng.range(0, 360.0f), 0},
               {dist + 0.45f, 0.3f * r, (static_cast<float>(i) - 1.0f) * length * 0.35f}, green, "Low brush at the foot of the branches")->obstacle = false;
    }
}

}  // namespace ts
