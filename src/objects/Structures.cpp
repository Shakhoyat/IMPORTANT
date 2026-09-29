#include "objects/Structures.h"

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
            solid(b.part(wall, "Sandbag", P::Cylinder, {0.26f, 0.17f, 0.5f}, {0, yaw, 0},
                         onGround(x, 0.08f + 0.14f * static_cast<float>(r), z), sand, "Sandbag"));
        }
    }
}

void addPracticalLights(Builder& b, SceneNode* parent) {
    const Material steel = mats::towerSteel();
    Material sodium = asLight(makeMaterial({1.00f, 0.70f, 0.42f}, 0.52f, Surface::Glass, 0.1f, 0.0f, 1.0f), 3.2f, 7.5f);
    SceneNode* lamps = b.group(parent, "Lamp posts", {0, 0, 0}, {0, 0, 0}, "Track lighting");
    for (float z : {6.5f, 13.0f}) {
        const float x = trackCentreX(z) + 1.75f;
        SceneNode* post = b.group(lamps, "Lamp post", onGround(x, 0.0f, z), {0, 0, 0}, "Sodium street lamp");
        solid(b.part(post, "Lamp pole", P::Cylinder, {0.09f, 0.09f, 3.2f}, {90, 0, 0}, {0, 1.6f, 0}, steel, "Pole"));
        b.part(post, "Lamp arm", P::Cube, {0.7f, 0.05f, 0.05f}, {0, 0, 0}, {-0.33f, 3.15f, 0}, steel, "Arm over the track");
        b.part(post, "Lamp head", P::Cube, {0.34f, 0.08f, 0.2f}, {0, 0, 0}, {-0.62f, 3.12f, 0}, steel, "Lamp head");
        b.part(post, "Sodium lamp", P::Sphere, {0.2f, 0.07f, 0.14f}, {0, 0, 0}, {-0.62f, 3.06f, 0}, sodium,
               "Street lamp (practical light)");
    }
    // Fire barrel inside the guard post's sandbag arc: warm, flickering, visible in both views.
    SceneNode* fire = b.group(parent, "Fire barrel", onGround(5.3f, 0.0f, 15.35f), {0, 0, 0}, "Guards' brazier");
    solid(b.part(fire, "Barrel", P::Cylinder, {0.5f, 0.5f, 0.78f}, {90, 0, 0}, {0, 0.39f, 0}, mats::rustDrum(), "Brazier drum"));
    Material embers = asLight(makeMaterial({1.00f, 0.42f, 0.10f}, 0.74f, Surface::Plain, 0.9f, 0.0f, 1.0f), 5.0f, 9.0f);
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
    Material lantern = asLight(makeMaterial({1.00f, 0.70f, 0.36f}, 0.55f, Surface::Glass, 0.1f, 0.0f, 1.0f), 1.8f, 5.0f);
    b.part(t, "Lantern", P::Sphere, {0.12f, 0.16f, 0.12f}, {0, 0, 0}, {0.9f, 1.42f, 0.0f}, lantern,
           "Oil lantern hanging from the ridge (practical light)");
}

void addSupplyCache(Builder& b, SceneNode* parent, float x, float z, float yawDeg, int variant) {
    const Material wood = mats::crateWood(), olive = mats::oliveDrum(), rust = mats::rustDrum();
    SceneNode* c = b.group(parent, "Supply cache", onGround(x, 0.0f, z), {0, yawDeg, 0}, "Crates and fuel drums");
    const Vec3 drum{0.4f, 0.4f, 0.6f};
    if (variant == 0) {
        solid(b.part(c, "Pallet", P::Cube, {1.3f, 0.12f, 1.1f}, {0, 0, 0}, {0, 0.06f, 0}, wood, "Wooden pallet"));
        solid(b.part(c, "Crate", P::Cube, {0.6f, 0.6f, 0.6f}, {0, 0, 0}, {-0.32f, 0.42f, -0.22f}, wood, "Ammo crate"));
        solid(b.part(c, "Crate", P::Cube, {0.6f, 0.6f, 0.6f}, {0, 6, 0}, {0.32f, 0.42f, -0.2f}, wood, "Ammo crate"));
        solid(b.part(c, "Crate", P::Cube, {0.55f, 0.45f, 0.55f}, {0, -10, 0}, {0.0f, 0.945f, -0.2f}, wood, "Ammo crate"));
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

}  // namespace ts
