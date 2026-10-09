#include "objects/BarracksHall.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "objects/BaseLayout.h"
#include "objects/Builder.h"
#include "objects/PropKit.h"

namespace ts {

namespace {

using P = PrimitiveType;
namespace H = layout::hall;
constexpr float F = H::kFloorY;
constexpr float kWallTop = F + H::kCeiling + 0.1f;   // walls carry the ceiling joists
constexpr float kRidge = 1.0f;                        // ridge above the wall top

struct Opening {
    float centre, width, sill, top;   // along the wall; sill / top above the floor
};

SceneNode* box(Builder& b, SceneNode* p, const std::string& name, const Vec3& lo, const Vec3& hi, const Material& m,
               const std::string& purpose) {
    return b.part(p, name, P::Cube, hi - lo, {0, 0, 0}, (lo + hi) * 0.5f, m, purpose);
}

// Wall between u0 and u1 along X (alongX) or Z, thickness between w0 and w1 on the other axis, with openings.
void wall(Builder& b, SceneNode* p, const char* name, bool alongX, float u0, float u1, float w0, float w1,
          std::vector<Opening> open, const Material& m, const Material& frame) {
    std::sort(open.begin(), open.end(), [](const Opening& a, const Opening& c) { return a.centre < c.centre; });
    auto piece = [&](float a0, float a1, float y0, float y1) {
        if (a1 - a0 < 1e-3f || y1 - y0 < 1e-3f) return;
        const Vec3 lo = alongX ? Vec3{a0, y0, w0} : Vec3{w0, y0, a0};
        const Vec3 hi = alongX ? Vec3{a1, y1, w1} : Vec3{w1, y1, a1};
        box(b, p, name, lo, hi, m, "Plywood wall panel on studs (painted); openings are real gaps");
    };
    float u = u0;
    for (const Opening& o : open) {
        const float a0 = o.centre - 0.5f * o.width, a1 = o.centre + 0.5f * o.width;
        piece(u, a0, F - 0.02f, kWallTop);
        piece(a0, a1, F - 0.02f, F + o.sill);            // under the window
        piece(a0, a1, F + o.top, kWallTop);              // lintel
        // frame: jambs, head and sill board, a little proud of both faces
        const float t0 = w0 - 0.02f, t1 = w1 + 0.02f;
        auto fr = [&](float b0, float b1, float y0, float y1) {
            const Vec3 lo = alongX ? Vec3{b0, y0, t0} : Vec3{t0, y0, b0};
            const Vec3 hi = alongX ? Vec3{b1, y1, t1} : Vec3{t1, y1, b1};
            box(b, p, "Opening frame", lo, hi, frame, "Painted timber frame around the opening");
        };
        fr(a0 - 0.06f, a0, F + o.sill, F + o.top);
        fr(a1, a1 + 0.06f, F + o.sill, F + o.top);
        fr(a0 - 0.06f, a1 + 0.06f, F + o.top, F + o.top + 0.06f);
        if (o.sill > 0.0f) fr(a0 - 0.06f, a1 + 0.06f, F + o.sill - 0.05f, F + o.sill);
        u = a1;
    }
    piece(u, u1, F - 0.02f, kWallTop);
}

// One bunk frame at z centre zc: steel posts and rails, two mattresses with pillows and blankets, a ladder.
void bunk(Builder& b, SceneNode* p, float zc, int k, const Material& steel, const Material& mattress, const Material& blanket,
          const Material& pillow) {
    const float x0 = H::kBunkX0, x1 = H::kBunkX0 + H::kBunkLen, hw = 0.5f * H::kBunkW;
    SceneNode* g = b.group(p, "Bunk bed " + std::to_string(k + 1), {0, 0, 0}, {0, 0, 0}, "Two-tier steel bunk bed");
    for (float x : {x0 + 0.03f, x1 - 0.03f})
        for (float z : {zc - hw + 0.03f, zc + hw - 0.03f})
            box(b, g, "Bunk post", {x - 0.025f, F, z - 0.025f}, {x + 0.025f, F + 1.92f, z + 0.025f}, steel, "Steel tube post");
    for (float top : {H::kBunkLow, H::kBunkHigh}) {
        const float y = F + top;
        for (float z : {zc - hw + 0.03f, zc + hw - 0.03f})
            box(b, g, "Bunk rail", {x0, y - 0.20f, z - 0.02f}, {x1, y - 0.14f, z + 0.02f}, steel, "Side rail under the mattress");
        box(b, g, "Bunk springs", {x0 + 0.04f, y - 0.17f, zc - hw + 0.06f}, {x1 - 0.04f, y - 0.14f, zc + hw - 0.06f}, steel,
            "Wire spring base");
        box(b, g, "Mattress", {x0 + 0.05f, y - 0.14f, zc - hw + 0.07f}, {x1 - 0.05f, y, zc + hw - 0.07f}, mattress,
            "Foam mattress in a canvas cover");
        detail(b, g, "Pillow", P::Cube, {0.34f, 0.09f, 0.62f}, {0, 0, 0}, {x0 + 0.25f, y + 0.045f, zc}, pillow, "Pillow at the head end");
        detail(b, g, "Blanket", P::Cube, {1.25f, 0.035f, H::kBunkW - 0.10f}, {0, 0, 0}, {x1 - 0.68f, y + 0.017f, zc}, blanket,
               "Folded-back wool blanket");
        box(b, g, "Head rail", {x0, y - 0.05f, zc - hw + 0.03f}, {x0 + 0.04f, y + 0.25f, zc + hw - 0.03f}, steel, "Head board rail");
    }
    // ladder at the foot end, aisle side
    const float lz = zc + hw - 0.12f;
    detail(b, g, "Ladder rail", P::Cube, {0.03f, 1.55f, 0.03f}, {0, 0, 0}, {x1 + 0.02f, F + 0.78f, lz - 0.17f}, steel, "Ladder rail");
    detail(b, g, "Ladder rail", P::Cube, {0.03f, 1.55f, 0.03f}, {0, 0, 0}, {x1 + 0.02f, F + 0.78f, lz + 0.17f}, steel, "Ladder rail");
    for (int r = 0; r < 4; ++r)
        detail(b, g, "Ladder rung", P::Cylinder, {0.025f, 0.025f, 0.34f}, {0, 0, 0}, {x1 + 0.02f, F + 0.35f + 0.35f * r, lz}, steel,
               "Ladder rung");
}

}  // namespace

HingeParams hallDoorHinge() {
    HingeParams p;
    const float jamb = H::kDoorX + 0.5f * H::kDoorW;   // east side of the opening (x 22.85 .. 24.15)
    p.hinge = {jamb, F, H::kZ1 + 0.015f};               // just outside the outer wall face (z = kZ1)
    p.closedYawDeg = -90.0f;                            // closed: hinge -> latch edge points west across the opening
    p.swing = 1;                                        // opening turns the leaf north (outwards), then east onto the wall
    p.openDeg = 180.0f;                                 // the door holder: parallel to the wall, 2.5 cm in front of it
    p.u0 = 0.012f;
    p.width = H::kDoorW - 0.012f - 0.01f;               // 1 cm gap to the west jamb
    p.bottom = 0.012f;
    p.height = H::kDoorH - 0.02f;
    p.n0 = -0.075f;                                     // 50 mm leaf, inside the opening when closed
    p.n1 = -0.025f;
    p.omega = 4.2f;                                     // 95 % of the swing after 4.74 / 4.2 = 1.13 s
    p.autoRadius = 4.5f;
    p.closeDelay = 2.5f;
    p.segments = 4;
    return p;
}

HallRig buildBarracksHall() {
    HallRig rig;
    Builder b("Barracks hall", "HAL", ShadingModel::Phong);
    rig.root = b.root("Barracks hall", {0, 0, 0}, "Enterable prefab barracks: bunks, lockers, mess table, gym corner");
    SceneNode* r = rig.root.get();

    const Material slab = mats::concrete();
    const Material siding = makeMaterial({0.46f, 0.43f, 0.33f}, 0.26f, Surface::Paint, 0.78f, 0.0f);    // tan paint
    const Material inner = makeMaterial({0.62f, 0.60f, 0.53f}, 0.26f, Surface::Wood, 0.80f, 0.0f);     // plywood
    const Material frame = makeMaterial({0.22f, 0.26f, 0.19f}, 0.25f, Surface::Paint, 0.62f, 0.0f);    // olive trim
    const Material roof = makeMaterial({0.30f, 0.33f, 0.28f}, 0.30f, Surface::Corrugated, 0.55f, 0.55f);
    const Material planks = makeMaterial({0.36f, 0.27f, 0.17f}, 0.24f, Surface::Wood, 0.70f, 0.0f);
    const Material steel = makeMaterial({0.24f, 0.27f, 0.22f}, 0.24f, Surface::Paint, 0.50f, 0.6f);
    const Material locker = makeMaterial({0.33f, 0.37f, 0.31f}, 0.24f, Surface::Paint, 0.45f, 0.5f);
    const Material mattress = makeMaterial({0.50f, 0.52f, 0.44f}, 0.26f, Surface::Sand, 0.95f, 0.0f);
    const Material blanket = makeMaterial({0.24f, 0.27f, 0.17f}, 0.26f, Surface::Sand, 0.97f, 0.0f);
    const Material pillow = makeMaterial({0.74f, 0.74f, 0.70f}, 0.26f, Surface::Sand, 0.95f, 0.0f);
    const Material rubber = makeMaterial({0.05f, 0.07f, 0.10f}, 0.25f, Surface::Rubber, 0.85f, 0.0f);
    const Material iron = makeMaterial({0.07f, 0.07f, 0.075f}, 0.24f, Surface::Metal, 0.45f, 0.85f);
    const Material chrome = makeMaterial({0.62f, 0.63f, 0.65f}, 0.24f, Surface::Metal, 0.30f, 1.0f);
    const Material wood = mats::crateWood();

    // ---- shell: slab, floor, walls, ceiling, gable roof
    box(b, r, "Floor slab", {H::kX0 - 0.1f, layout::kApronLevel - 0.06f, H::kZ0 - 0.1f}, {H::kX1 + 0.1f, F - 0.01f, H::kZ1 + 0.1f},
        slab, "Raised concrete floor slab");
    detail(b, r, "Plank floor", P::Cube, {H::kIX1 - H::kIX0, 0.012f, H::kIZ1 - H::kIZ0}, {0, 0, 0},
           {0.5f * (H::kIX0 + H::kIX1), F - 0.006f, 0.5f * (H::kIZ0 + H::kIZ1)}, planks, "Wooden plank floor");
    wall(b, r, "North wall", true, H::kX0, H::kX1, H::kIZ1, H::kZ1,
         {{H::kDoorX, H::kDoorW, 0.0f, H::kDoorH}, {26.4f, 1.2f, 1.0f, 1.9f}, {20.0f, 0.9f, 1.95f, 2.45f}}, siding, frame);
    wall(b, r, "South wall", true, H::kX0, H::kX1, H::kZ0, H::kIZ0, {{22.4f, 1.0f, 1.0f, 1.9f}, {25.9f, 1.2f, 1.0f, 1.9f}}, siding, frame);
    wall(b, r, "East wall", false, H::kZ0 + H::kWall, H::kZ1 - H::kWall, H::kIX1, H::kX1, {{-13.0f, 1.2f, 1.0f, 1.9f}, {-10.4f, 1.2f, 1.0f, 1.9f}},
         siding, frame);
    wall(b, r, "West wall", false, H::kZ0 + H::kWall, H::kZ1 - H::kWall, H::kX0, H::kIX0, {{-13.25f, 0.7f, 2.0f, 2.45f}, {-11.75f, 0.7f, 2.0f, 2.45f}},
         siding, frame);
    // inner lining (lighter plywood) just inside the walls: a thin skin so the room reads warm and bright
    detail(b, r, "Wall lining", P::Cube, {0.012f, 0.6f, H::kIZ1 - H::kIZ0}, {0, 0, 0}, {H::kIX0 + 0.006f, F + 0.3f, 0.5f * (H::kIZ0 + H::kIZ1)}, inner,
           "Skirting / dado lining");
    detail(b, r, "Wall lining", P::Cube, {0.012f, 0.6f, H::kIZ1 - H::kIZ0}, {0, 0, 0}, {H::kIX1 - 0.006f, F + 0.3f, 0.5f * (H::kIZ0 + H::kIZ1)}, inner,
           "Skirting / dado lining");
    box(b, r, "Ceiling", {H::kIX0, F + H::kCeiling, H::kIZ0}, {H::kIX1, F + H::kCeiling + 0.08f, H::kIZ1}, inner, "Plywood ceiling");
    const float zc = 0.5f * (H::kZ0 + H::kZ1), half = 0.5f * (H::kZ1 - H::kZ0), over = 0.35f;
    const float pitch = std::atan2(kRidge, half), slope = std::hypot(half + over, (half + over) * kRidge / half);
    for (int side = -1; side <= 1; side += 2) {
        const float zm = zc + side * 0.5f * (half + over), ym = kWallTop + kRidge - 0.5f * (half + over) * kRidge / half + 0.04f;
        b.part(r, "Roof sheet", P::Cube, {H::kX1 - H::kX0 + 0.6f, 0.05f, slope}, {degrees(side * pitch), 0, 0},
               {0.5f * (H::kX0 + H::kX1), ym, zm}, roof, "Corrugated roof sheet on purlins");
    }
    box(b, r, "Ridge cap", {H::kX0 - 0.3f, kWallTop + kRidge, zc - 0.12f}, {H::kX1 + 0.3f, kWallTop + kRidge + 0.08f, zc + 0.12f}, roof,
        "Ridge capping");
    for (float x : {H::kX0, H::kX1 - H::kWall})   // stepped gable ends
        for (int k = 0; k < 5; ++k) {
            const float y0 = kWallTop + 0.2f * k, w = half * (1.0f - (0.2f * k + 0.1f) / kRidge);
            box(b, r, "Gable", {x, y0, zc - w}, {x + H::kWall, y0 + 0.2f, zc + w}, siding, "Gable end boarding");
        }

    // ---- door: open leaf against the outer wall, step, lamp, sign
    box(b, r, "Door step", {H::kDoorX - 0.9f, layout::kApronLevel - 0.04f, H::kZ1}, {H::kDoorX + 0.9f, F - 0.02f, H::kZ1 + 0.7f}, slab,
        "Concrete step in front of the door");
    // v9 M4: the hinged door (objects/HingedDoor.h) replaces the v8 leaf that stood open against the wall: it hangs on
    // the east jamb and opens outwards through 180 deg onto the same spot of the wall.
    {
        HingedDoorLook look;
        look.leaf = makeMaterial({0.19f, 0.25f, 0.18f}, 0.25f, Surface::Paint, 0.55f, 0.0f);    // dark olive door paint
        look.trim = makeMaterial({0.24f, 0.30f, 0.22f}, 0.25f, Surface::Paint, 0.50f, 0.0f);
        look.metal = chrome;
        look.plate = makeMaterial({0.48f, 0.49f, 0.50f}, 0.25f, Surface::Metal, 0.38f, 1.0f);
        const Vec3 eye{H::kDoorX - 2.2f, F + 1.75f, H::kZ1 + 3.4f}, at{H::kDoorX, F + 1.1f, H::kZ1};
        rig.door = std::make_unique<HingedDoor>(b, r, "BARRACKS DOOR", hallDoorHinge(), look, eye, at);
    }
    detail(b, r, "Door lamp hood", P::Cube, {0.30f, 0.04f, 0.24f}, {0, 0, 0}, {H::kDoorX, F + 2.55f, H::kZ1 + 0.14f}, steel, "Hood over the door lamp");
    b.part(r, "Hall door lamp", P::Sphere, {0.13f, 0.08f, 0.13f}, {0, 0, 0}, {H::kDoorX, F + 2.48f, H::kZ1 + 0.14f},
           asLight(makeMaterial({1.0f, 0.78f, 0.52f}, 0.55f, Surface::Glass, 0.2f, 0.0f, 1.0f), 1.2f, 6.0f), "Door lamp (practical light, off by day)");
    detail(b, r, "Sign board", P::Cube, {1.0f, 0.32f, 0.03f}, {0, 0, 0}, {H::kDoorX - 1.25f, F + 2.2f, H::kZ1 + 0.03f},
           decal({0.12f, 0.20f, 0.12f}, 0.26f), "BARRACKS B sign");
    detail(b, r, "Sign letters", P::Cube, {0.80f, 0.10f, 0.01f}, {0, 0, 0}, {H::kDoorX - 1.25f, F + 2.2f, H::kZ1 + 0.05f},
           decal({0.85f, 0.82f, 0.62f}, 0.26f), "Stencilled lettering");
    // AC unit on the east wall, cable tray
    box(b, r, "Hall AC unit", {H::kX1, F + 1.9f, -12.2f}, {H::kX1 + 0.45f, F + 2.5f, -11.3f}, mats::galvanized(), "Split air conditioner");
    detail(b, r, "Hall AC fan", P::Cylinder, {0.45f, 0.45f, 0.04f}, {0, 90, 0}, {H::kX1 + 0.47f, F + 2.2f, -11.75f}, iron, "Condenser fan grille");

    // ---- bunks (west)
    for (int k = 0; k < 3; ++k) bunk(b, r, H::kBunkZ[k], k, steel, mattress, blanket, pillow);

    // ---- lockers (north-west), rifle rack and notice board by the door
    const int nLock = 6;
    const float lw = (H::kLockerX1 - H::kLockerX0) / nLock;
    for (int i = 0; i < nLock; ++i) {
        const float x0 = H::kLockerX0 + lw * i;
        box(b, r, "Locker", {x0 + 0.01f, F, H::kIZ1 - 0.48f}, {x0 + lw - 0.01f, F + 1.85f, H::kIZ1}, locker, "Steel wall locker");
        detail(b, r, "Locker vents", P::Cube, {lw * 0.5f, 0.12f, 0.01f}, {0, 0, 0}, {x0 + 0.5f * lw, F + 1.6f, H::kIZ1 - 0.485f}, iron,
               "Louvred vents");
        detail(b, r, "Locker handle", P::Cube, {0.03f, 0.12f, 0.03f}, {0, 0, 0}, {x0 + lw - 0.08f, F + 1.05f, H::kIZ1 - 0.495f}, chrome,
               "Handle");
        detail(b, r, "Name tag", P::Cube, {0.14f, 0.05f, 0.005f}, {0, 0, 0}, {x0 + 0.5f * lw, F + 1.4f, H::kIZ1 - 0.483f},
               decal({0.82f, 0.80f, 0.72f}, 0.26f), "Name tag");
    }
    detail(b, r, "Duffel bag", P::Cylinder, {0.32f, 0.32f, 0.7f}, {0, 90, 0}, {H::kLockerX0 + 0.6f, F + 2.0f, H::kIZ1 - 0.25f},
           makeMaterial({0.28f, 0.30f, 0.20f}, 0.26f, Surface::Sand, 0.95f, 0.0f), "Kit bag on top of the lockers");
    box(b, r, "Rifle rack", {24.45f, F, H::kIZ1 - 0.25f}, {25.25f, F + 0.12f, H::kIZ1}, wood, "Floor rack for rifles");
    for (int i = 0; i < 3; ++i) {
        const float x = 24.6f + 0.25f * i;
        detail(b, r, "Racked rifle", P::Cube, {0.05f, 0.95f, 0.06f}, {-8, 0, 0}, {x, F + 0.6f, H::kIZ1 - 0.14f}, iron, "Rifle in the rack");
    }
    detail(b, r, "Notice board", P::Cube, {0.9f, 0.6f, 0.02f}, {0, 0, 0}, {24.85f, F + 1.65f, H::kIZ1 - 0.012f}, wood, "Cork notice board");
    for (int i = 0; i < 4; ++i)
        detail(b, r, "Notice", P::Cube, {0.18f, 0.24f, 0.004f}, {0, 0, i * 4.0f - 6.0f},
               {24.55f + 0.2f * i, F + 1.68f + 0.06f * (i % 2), H::kIZ1 - 0.024f}, decal({0.86f, 0.85f, 0.78f}, 0.26f), "Pinned notice");

    // ---- mess table with benches and what lies on it
    const float tx0 = H::kTableX - 0.5f * H::kTableLen, tx1 = H::kTableX + 0.5f * H::kTableLen;
    box(b, r, "Mess table top", {tx0, F + 0.72f, H::kTableZ - 0.5f * H::kTableDepth}, {tx1, F + 0.77f, H::kTableZ + 0.5f * H::kTableDepth}, wood,
        "Plank table top");
    for (float x : {tx0 + 0.08f, tx1 - 0.08f})
        for (float z : {H::kTableZ - 0.3f, H::kTableZ + 0.3f})
            box(b, r, "Table leg", {x - 0.04f, F, z - 0.04f}, {x + 0.04f, F + 0.72f, z + 0.04f}, wood, "Table leg");
    for (float bz : {H::kBenchNZ, H::kBenchSZ}) {
        box(b, r, "Bench seat", {tx0, F + 0.42f, bz - 0.15f}, {tx1, F + 0.46f, bz + 0.15f}, wood, "Bench seat");
        for (float x : {tx0 + 0.1f, tx1 - 0.1f})
            box(b, r, "Bench leg", {x - 0.04f, F, bz - 0.12f}, {x + 0.04f, F + 0.42f, bz + 0.12f}, wood, "Bench leg");
    }
    const Material mug = makeMaterial({0.20f, 0.27f, 0.20f}, 0.45f, Surface::Paint, 0.40f, 0.3f);
    for (int i = 0; i < 4; ++i)
        detail(b, r, "Mug", P::Cylinder, {0.08f, 0.08f, 0.10f}, {90, 0, 0},
               {H::kSeatX[i % 2] + (i < 2 ? 0.0f : 0.15f), F + 0.82f, H::kTableZ + (i < 2 ? 0.22f : -0.24f)}, mug, "Tin mug");
    detail(b, r, "Map sheet", P::Cube, {0.6f, 0.004f, 0.45f}, {0, 12, 0}, {H::kTableX - 0.1f, F + 0.772f, H::kTableZ}, decal({0.72f, 0.70f, 0.55f}, 0.26f),
           "Folded map");
    detail(b, r, "Field radio", P::Cube, {0.30f, 0.16f, 0.22f}, {0, -10, 0}, {tx1 - 0.25f, F + 0.85f, H::kTableZ + 0.05f},
           makeMaterial({0.16f, 0.19f, 0.14f}, 0.40f, Surface::Paint, 0.6f, 0.2f), "Field radio");
    detail(b, r, "Radio antenna", P::Cylinder, {0.01f, 0.01f, 0.45f}, {90, 0, 0}, {tx1 - 0.15f, F + 1.15f, H::kTableZ + 0.1f}, iron, "Whip");
    detail(b, r, "Playing cards", P::Cube, {0.09f, 0.012f, 0.06f}, {0, 25, 0}, {H::kTableX + 0.45f, F + 0.776f, H::kTableZ - 0.1f},
           decal({0.85f, 0.83f, 0.80f}, 0.26f), "Deck of cards");

    // ---- gym corner: pull-up bar, mats, dumbbell rack, punching bag
    for (float x : {H::kBarX - 0.6f, H::kBarX + 0.6f}) {
        b.part(r, "Pull-up post", P::Cylinder, {0.06f, 0.06f, H::kBarH + 0.1f}, {90, 0, 0}, {x, F + 0.5f * (H::kBarH + 0.1f), H::kBarZ}, steel,
               "Pull-up frame post");
        detail(b, r, "Post foot", P::Cube, {0.30f, 0.02f, 0.30f}, {0, 0, 0}, {x, F + 0.01f, H::kBarZ}, steel, "Base plate");
    }
    b.part(r, "Pull-up bar", P::Cylinder, {0.035f, 0.035f, 1.3f}, {0, 90, 0}, {H::kBarX, F + H::kBarH, H::kBarZ}, chrome, "Chin-up bar");
    for (float mz : {H::kMatAZ, H::kMatBZ})
        detail(b, r, "Exercise mat", P::Cube, {H::kMatLen, 0.02f, 0.8f}, {0, 0, 0}, {H::kMatX, F + 0.01f, mz}, rubber, "Rubber exercise mat");
    box(b, r, "Dumbbell rack", {H::kIX1 - 0.38f, F, -13.75f}, {H::kIX1 - 0.02f, F + 0.75f, -12.75f}, steel, "Dumbbell rack");
    for (int i = 0; i < 4; ++i)
        for (int lvl = 0; lvl < 2; ++lvl) {
            const float z = -13.6f + 0.25f * i, y = F + 0.42f + 0.3f * lvl;
            detail(b, r, "Dumbbell", P::Cylinder, {0.11f + 0.02f * i, 0.11f + 0.02f * i, 0.30f}, {0, 90, 0}, {H::kIX1 - 0.2f, y, z}, iron, "Dumbbell");
        }
    b.part(r, "Punching bag", P::Cylinder, {0.36f, 0.36f, 1.0f}, {90, 0, 0}, {H::kIX1 - 0.45f, F + 1.15f, H::kIZ0 + 0.45f},
           makeMaterial({0.20f, 0.10f, 0.07f}, 0.26f, Surface::Rubber, 0.6f, 0.0f), "Heavy bag on a chain");
    detail(b, r, "Bag chain", P::Cylinder, {0.02f, 0.02f, 0.9f}, {90, 0, 0}, {H::kIX1 - 0.45f, F + 2.2f, H::kIZ0 + 0.45f}, iron, "Chain");
    detail(b, r, "Kettlebell", P::Sphere, {0.22f, 0.22f, 0.22f}, {0, 0, 0}, {H::kIX1 - 0.35f, F + 0.11f, -12.45f}, iron, "Kettlebell");

    // ---- walls: map board, flag, clock; ceiling lights
    detail(b, r, "Map board", P::Cube, {0.02f, 0.8f, 1.2f}, {0, 0, 0}, {H::kIX1 - 0.012f, F + 1.55f, -11.75f}, decal({0.58f, 0.62f, 0.50f}, 0.26f),
           "Area map");
    detail(b, r, "Map pins", P::Cube, {0.01f, 0.4f, 0.6f}, {0, 0, 0}, {H::kIX1 - 0.025f, F + 1.6f, -11.7f}, decal({0.65f, 0.25f, 0.18f}, 0.26f),
           "Marked positions");
    detail(b, r, "Wall flag", P::Cube, {0.02f, 0.6f, 0.9f}, {0, 0, 0}, {H::kIX0 + 0.012f, F + 2.25f, -12.5f}, decal({0.20f, 0.35f, 0.62f}, 0.26f),
           "Unit flag");
    detail(b, r, "Wall clock", P::Cylinder, {0.28f, 0.28f, 0.03f}, {0, 0, 0}, {H::kTableX, F + 2.2f, H::kIZ1 - 0.02f}, pillow, "Clock");
    const Material tube = asLight(makeMaterial({1.0f, 0.95f, 0.86f}, 0.50f, Surface::Glass, 0.2f, 0.0f, 1.0f), 2.4f, 7.5f);
    for (float x : {20.6f, 23.3f, 26.0f}) {
        box(b, r, "Light fitting", {x - 0.62f, F + H::kCeiling - 0.06f, -11.9f}, {x + 0.62f, F + H::kCeiling, -11.7f}, steel, "Fluorescent fitting");
        b.part(r, "Ceiling tube (light)", P::Cylinder, {0.05f, 0.05f, 1.15f}, {0, 90, 0}, {x, F + H::kCeiling - 0.08f, -11.8f}, tube,
               "Fluorescent tube (practical light, on day and night)");
    }

    // ---- outdoor PT area north of the hall
    for (float x : {H::kPtBarX - 0.7f, H::kPtBarX + 0.7f})
        b.part(r, "PT frame post", P::Cylinder, {0.08f, 0.08f, 2.5f}, {90, 0, 0}, {x, layout::kApronLevel + 1.25f, H::kPtBarZ}, steel,
               "Outdoor pull-up frame post");
    b.part(r, "PT bar", P::Cylinder, {0.04f, 0.04f, 1.5f}, {0, 90, 0}, {H::kPtBarX, layout::kApronLevel + 2.35f, H::kPtBarZ}, chrome, "Outdoor bar");
    for (int i = 0; i < 2; ++i)
        b.part(r, "PT tyre", P::Cylinder, {0.9f, 0.9f, 0.28f}, {90, 0, 0}, {27.6f + 0.15f * i, layout::kApronLevel + 0.14f + 0.28f * i, -5.6f}, rubber,
               "Training tyre");

    // ---- walk data and the indoor light volume
    rig.floors.push_back({H::kIX0, H::kIZ0, H::kIX1, H::kIZ1, F, F, 0, F + H::kCeiling});
    rig.floors.push_back({H::kDoorX - 0.5f * H::kDoorW, H::kIZ1, H::kDoorX + 0.5f * H::kDoorW, H::kZ1, F, F, 0, F + H::kDoorH});
    rig.indoor = {{H::kIX0 - 0.05f, F - 0.3f, H::kIZ0 - 0.05f}, {H::kIX1 + 0.05f, F + H::kCeiling + 0.05f, H::kIZ1 + 0.05f}};
    return rig;
}

}  // namespace ts
