// T60-T69: underground bunker, entrance trench hole, blast doors and the walkable space.
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "TestKit.h"
#include "objects/BaseLayout.h"
#include "objects/Bunker.h"
#include "objects/Terrain.h"

using ts::testkit::fmt;
using ts::testkit::record;

namespace {

using namespace ts;

void updateWorld(SceneNode& n, const Mat4& parent) {
    n.world = parent * n.local.matrix();
    for (auto& c : n.children) updateWorld(*c, n.world);
}

void leaves(SceneNode& n, std::vector<SceneNode*>& out) {
    if (n.isLeaf()) out.push_back(&n);
    for (auto& c : n.children) leaves(*c, out);
}

int countLeaves(const SceneNode& n) {
    int c = n.isLeaf() ? 1 : 0;
    for (const auto& ch : n.children) c += countLeaves(*ch);
    return c;
}

void stepDoors(Controllable& d, float seconds, bool press, float dt = 1.0f / 60.0f) {
    WorldContext w;
    w.dt = dt;
    ControlInput in;
    in.actionPressed = press;
    d.update(w, press ? &in : nullptr);
    for (float t = dt; t < seconds; t += dt) {
        w.time += dt;
        d.update(w, nullptr);
    }
}

}  // namespace

TS_TEST_GROUP(Bunker) {
    using namespace ts;
    namespace L = ts::layout;
    BunkerRig rig = buildBunker();
    updateWorld(*rig.root, Mat4::identity());

    // T60 earth cover over every room.
    {
        float minH = 1e9f;
        for (float x = L::kBunkerMinX; x <= L::kBunkerMaxX; x += 0.2f)
            for (float z = L::kBunkerMinZ; z <= L::kBunkerMaxZ; z += 0.2f) minH = std::min(minH, terrainHeight(x, z));
        const float need = L::kBunkerRoofTopY + 0.15f;
        record("T60", "Terrain covers the underground rooms with at least 0.15 m of earth",
               fmt("terrain >= %.2f m over the whole footprint", need), fmt("minimum terrain height %.3f m", minH), minH >= need - 1e-4f);
    }
    // T61 trench hole in the terrain mesh.
    {
        const MeshData disc = createTerrainDisc();
        int inside = 0, nearOutside = 0;
        for (size_t t = 0; t < disc.indices.size(); t += 3) {
            const Vec3 g = (disc.vertices[disc.indices[t]].position + disc.vertices[disc.indices[t + 1]].position +
                            disc.vertices[disc.indices[t + 2]].position) / 3.0f;
            if (std::fabs(g.x) < L::kRampHalfWidth + 0.35f && g.z > L::kBlastDoorZ - 0.3f && g.z < L::kRampTopZ) ++inside;
            else if (std::fabs(g.x) < 2.6f && g.z > 3.0f && g.z < 14.0f) ++nearOutside;
        }
        float rim = 0.0f;
        for (const auto& v : disc.vertices) rim = std::max(rim, std::sqrt(v.position.x * v.position.x + v.position.z * v.position.z));
        record("T61", "Terrain mesh: 36 m radius, hole cut for the entrance trench",
               "0 triangle centroids in the trench footprint, ground around it present, radius 36 m, < 80k vertices",
               fmt("%d inside, %d around, radius %.2f, %zu vertices, %zu triangles", inside, nearOutside, rim, disc.vertexCount(), disc.triangleCount()),
               inside == 0 && nearOutside > 100 && std::fabs(rim - L::kBaseRadius) < 1e-3f && disc.vertexCount() < 80000);
    }
    // T62 walk from the top of the ramp to the CIC centre.
    {
        stepDoors(*rig.doors, 4.5f, true);   // the doors start shut (a gate across the entrance): open them
        const std::vector<Box> doorBoxes = rig.doors->dynamicObstacles();
        const Vec3 pts[] = {{0, 0, L::kRampTopZ + 1.0f}, {0, 0, 3.0f}, {0, 0, -1.0f}, {0, 0, -2.2f}, bunker::kCicCentre};
        float x = 0.0f, z = pts[0].z, y = terrainHeight(x, z), maxStep = 0.0f, minClear = 1e9f;
        int blocked = 0, steps = 0;
        for (size_t s = 1; s < sizeof(pts) / sizeof(pts[0]); ++s) {
            const Vec3 a = pts[s - 1], b = pts[s];
            const float len = std::fabs(b.z - a.z);
            for (float d = 0.0f; d <= len; d += 0.05f, ++steps) {
                x = lerpf(a.x, b.x, d / len);
                z = lerpf(a.z, b.z, d / len);
                const float x0 = x, z0 = z;
                if (rig.walk.collide(x, z, y, y + 1.8f, 0.3f, doorBoxes) && (std::fabs(x - x0) > 1e-4f || std::fabs(z - z0) > 1e-4f)) ++blocked;
                const float fy = rig.walk.floorHeight(x, z, y);
                maxStep = std::max(maxStep, std::fabs(fy - y));
                y = fy;
                minClear = std::min(minClear, rig.walk.ceilingHeight(x, z, y) - y);
            }
        }
        record("T62", "Walk from the ramp top to the command room centre stays on the floors",
               "no wall contact, max floor step <= 0.45 m, ends on the CIC floor, head room >= 2.0 m",
               fmt("%d steps, %d contacts, max step %.3f m, final y %.2f (CIC %.2f), min head room %.2f m", steps, blocked, maxStep, y, bunker::kCicCentre.y,
                   minClear),
               blocked == 0 && maxStep <= 0.45f && std::fabs(y - bunker::kCicCentre.y) < 0.01f && minClear >= 2.0f &&
                   rig.walk.indoor({0, y + 1.0f, -4.2f}) && !rig.walk.indoor({0, y + 1.0f, 8.0f}));
    }
    // T63 walls keep the walker out.
    {
        float x = -1.4f, z = -0.5f;  // 10 cm from the west tunnel wall
        const float y = L::kBunkerFloorY;
        const bool hit1 = rig.walk.collide(x, z, y, y + 1.8f, 0.3f, {});
        const bool west = x >= -1.5f + 0.3f - 1e-3f;
        float rx = 1.2f, rz = -1.7f;  // 10 cm from the cross wall
        const bool hit2 = rig.walk.collide(rx, rz, y, y + 1.8f, 0.3f, {});
        const bool south = rz >= -1.8f + 0.3f - 1e-3f;
        float cx = -3.2f, cz = -3.4f;  // standing in a console desk
        const bool hit3 = rig.walk.collide(cx, cz, y, y + 1.8f, 0.3f, {});
        float sx = 3.0f, sz = 0.0f;  // on the surface (feet above the roof), walls must not matter
        const float sy = terrainHeight(sx, sz);
        const bool hit4 = rig.walk.collide(sx, sz, sy, sy + 1.8f, 0.3f, {});
        record("T63", "Collision pushes the walker out of walls and furniture; the surface above is free",
               "tunnel wall, cross wall and console push out; no contact on the earth roof",
               fmt("wall %d x=%.2f, cross %d, console %d, roof contact %d", hit1, x, hit2, hit3, hit4),
               hit1 && west && hit2 && south && hit3 && !hit4);
    }
    // T64 doors block / pass.
    {
        BunkerRig r4 = buildBunker();   // a fresh rig: the doors are shut
        Controllable& d = *r4.doors;
        auto walkThrough = [&]() {
            float x = 0.0f, z = 5.5f;
            const float y = L::kBunkerFloorY;
            for (int i = 0; i < 400; ++i) {
                z -= 0.02f;
                r4.walk.collide(x, z, y, y + 1.8f, 0.3f, d.dynamicObstacles());
            }
            return z;
        };
        const float zClosed = walkThrough();   // shut at the start
        stepDoors(d, 4.5f, true);
        const float zOpen = walkThrough();
        stepDoors(d, 4.5f, true);
        const float zReopened = walkThrough();   // closed again
        (void)zReopened;
        stepDoors(d, 4.5f, true);
        const float zOpenAgain = walkThrough();
        record("T64", "A closed blast door blocks the walker, an open one lets him pass",
               "shut at the start: z >= 4.2 (leaf face + radius); open: z < 3.6; closed again: z >= 4.2; open again: z < 3.6",
               fmt("shut z=%.2f, open z=%.2f, closed again z=%.2f, open again z=%.2f", zClosed, zOpen, zReopened, zOpenAgain),
               zClosed >= 4.2f && zOpen < 3.6f && zReopened >= 4.2f && zOpenAgain < 3.6f);
    }
    // T65 door state machine: CLOSED at the start, ~4 s travel, reversible; automatic for a soldier at the doors.
    {
        BunkerRig r2 = buildBunker();
        Controllable& d = *r2.doors;
        const std::string s0 = d.status();
        WorldContext w;
        w.dt = 1.0f / 60.0f;
        ControlInput in;
        in.actionPressed = true;
        d.update(w, &in);
        const std::string sOpening = d.status();
        float tOpen = w.dt;
        while (d.status().find("OPEN 100") == std::string::npos && tOpen < 10.0f) {
            w.time += w.dt;
            d.update(w, nullptr);
            tOpen += w.dt;
        }
        in.actionPressed = false;
        in.firePressed = true;
        d.update(w, &in);
        const std::string sClosing = d.status();
        float tClose = w.dt;
        while (d.status().find("CLOSED") == std::string::npos && tClose < 10.0f) {
            w.time += w.dt;
            d.update(w, nullptr);
            tClose += w.dt;
        }
        const std::string sClosed = d.status();
        // Reverse in the middle of a stroke.
        d.update(w, &in);                                     // start opening
        for (int i = 0; i < 120; ++i) { w.time += w.dt; d.update(w, nullptr); }   // 2 s of opening
        const std::string mid = d.status();
        d.update(w, &in);                                     // reverse
        const std::string rev = d.status();
        // Automatic: a soldier in the trench opens the doors; 3 s after he is gone they close again.
        BunkerRig r3 = buildBunker();
        Controllable& a = *r3.doors;
        WorldContext aw;
        aw.dt = 1.0f / 60.0f;
        aw.time = 100.0f;
        aw.soldierAtBlastDoors = true;
        float tAuto = 0.0f;
        while (a.status().find("OPEN 100") == std::string::npos && tAuto < 10.0f) {
            aw.time += aw.dt;
            a.update(aw, nullptr);
            tAuto += aw.dt;
        }
        aw.soldierAtBlastDoors = false;
        float tShut = 0.0f;
        bool stillOpenAt2 = false;
        while (a.status().find("CLOSED") == std::string::npos && tShut < 12.0f) {
            aw.time += aw.dt;
            a.update(aw, nullptr);
            tShut += aw.dt;
            if (tShut > 1.9f && tShut < 2.0f) stillOpenAt2 = a.status().find("OPEN 100") != std::string::npos;
        }
        record("T65", "Blast door state machine CLOSED/OPENING/OPEN/CLOSING, ~4 s travel, reversible; opens for a soldier, closes 3 s after",
               "starts 'CLOSED 0%', opens and closes in 3.9..4.1 s, mid-stroke input reverses; automatic: opens in 3.9..4.1 s, still open 2 s after he leaves, closed by 7.1 s",
               fmt("%s | %s | open in %.2f s | %s | closed in %.2f s | mid '%s' -> '%s' | auto open in %.2f s, closed %.2f s after he left (open at 2 s: %d)",
                   s0.c_str(), sOpening.c_str(), tOpen, sClosing.c_str(), tClose, mid.c_str(), rev.c_str(), tAuto, tShut, stillOpenAt2),
               s0 == "BLAST DOORS CLOSED 0%" && sOpening.find("OPENING") != std::string::npos && sClosing.find("CLOSING") != std::string::npos &&
                   sClosed == "BLAST DOORS CLOSED 0%" && tClose > 3.9f && tClose < 4.1f && tOpen > 3.9f && tOpen < 4.1f &&
                   mid.find("OPENING") != std::string::npos && rev.find("CLOSING") != std::string::npos && d.possessable() &&
                   tAuto > 3.9f && tAuto < 4.1f && stillOpenAt2 && tShut > 6.9f && tShut < 7.3f);
    }
    // T66 part budget per section.
    {
        std::map<std::string, int> sections;
        for (const auto& c : rig.root->children) sections[c->name] = countLeaves(*c);
        const int total = countLeaves(*rig.root);
        std::string txt;
        for (const auto& kv : sections) txt += fmt("%s %d, ", kv.first.c_str(), kv.second);
        record("T66", "Bunker part budget", "250..450 parts", fmt("%d parts: %s", total, txt.c_str()), total >= 250 && total <= 450);
    }
    // T67 only lab primitives, flat shading, documented nodes, underground parts are not occluders.
    {
        std::vector<SceneNode*> ls;
        leaves(*rig.root, ls);
        int bad = 0, undocumented = 0, flat = 0, badOcc = 0, occluders = 0;
        for (SceneNode* n : ls) {
            const bool lab = isLibraryPartPrimitive(n->primitive);   // v10 M3: any library part primitive
            bad += lab ? 0 : 1;
            undocumented += (n->name.empty() || n->purpose.empty() || n->object != "Command Bunker") ? 1 : 0;
            flat += n->shading == ShadingModel::Flat ? 1 : 0;
            if (n->occluder) {
                ++occluders;
                if (worldBounds(n->world).min.y < -0.5f) ++badOcc;
            }
        }
        record("T67", "Bunker parts are lab primitives with name and purpose, Flat shading, occluders only above ground",
               "0 foreign primitives, 0 undocumented, all Flat, 0 buried occluders",
               fmt("%d foreign, %d undocumented, %d/%zu flat, %d occluders (%d buried)", bad, undocumented, flat, ls.size(), occluders, badOcc),
               bad == 0 && undocumented == 0 && flat == static_cast<int>(ls.size()) && badOcc == 0 && occluders > 0);
    }
    // T68 interface members and the ramp geometry (rotated cube really follows the walk floor).
    {
        SceneNode* ramp = nullptr;
        std::vector<SceneNode*> ls;
        leaves(*rig.root, ls);
        for (SceneNode* n : ls)
            if (n->name == "Ramp floor") ramp = n;
        float err = 1e9f;
        if (ramp) {
            err = 0.0f;
            for (float u : {-0.4f, 0.0f, 0.4f}) {  // points on the top face along the slope
                // v10 M3: the ramp is a wedge; its walking surface is the slope face y + z = 0 of the unit wedge.
                const Vec3 local = ramp->primitive == PrimitiveType::Wedge ? Vec3{0.0f, -u, u} : Vec3{0.0f, 0.5f, u};
                const Vec3 p = transformPoint(ramp->world, local);
                err = std::max(err, std::fabs(p.y - rig.walk.floorHeight(p.x, p.z, p.y)));
            }
        }
        const bool fans = rig.fanCaps[0] && rig.fanCaps[1] && rig.beaconBulb;
        const float beaconY = rig.beaconBulb ? transformPoint(rig.beaconBulb->world, {0, 0, 0}).y : -9.0f;
        const CameraHint h = rig.doors->camera(false);
        record("T68", "Rig interface: fans, beacon above ground, ramp cube matches the walk floor, door camera",
               "fan caps + beacon valid, beacon y > 2 m, ramp top face within 3 cm of the walk floor, camera eye above the ramp",
               fmt("fans %d, beacon y %.2f, ramp error %.3f m, eye (%.1f %.1f %.1f)", fans, beaconY, err, h.eye.x, h.eye.y, h.eye.z),
               fans && beaconY > 2.0f && err < 0.03f && h.eye.z > L::kRampBottomZ && h.eye.y > L::kBunkerFloorY + 1.0f);
    }
    // T69 walk space inventory.
    {
        const WalkSpace& w = rig.walk;
        const bool ramp = std::fabs(w.floorHeight(0.0f, L::kRampTopZ, terrainHeight(0.0f, L::kRampTopZ)) - terrainHeight(0.0f, L::kRampTopZ)) < 0.02f;
        const float outside = w.floorHeight(10.0f, 10.0f, terrainHeight(10.0f, 10.0f));
        record("T69", "Walk space inventory", "floors >= 10, walls >= 30, 1..8 indoor volumes, terrain outside the regions, ramp top meets the ground",
               fmt("%zu floors, %zu walls, %zu indoor volumes, outside height %.3f (terrain %.3f)", w.floors().size(), w.walls().size(),
                   w.indoorVolumes().size(), outside, terrainHeight(10.0f, 10.0f)),
               w.floors().size() >= 10 && w.walls().size() >= 30 && !w.indoorVolumes().empty() && w.indoorVolumes().size() <= 8 &&
                   std::fabs(outside - terrainHeight(10.0f, 10.0f)) < 1e-5f && ramp);
    }
}
