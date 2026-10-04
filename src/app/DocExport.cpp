#include "app/DocExport.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "ai/DroneController.h"
#include "objects/AaGun.h"
#include "objects/Base.h"
#include "objects/BaseLayout.h"
#include "objects/Bunker.h"
#include "objects/Helicopter.h"
#include "objects/Radar.h"
#include "objects/Tank.h"
#include "objects/Terrain.h"
#include "objects/Uav.h"
#include "scene/Combat.h"
#include "scene/Control.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/Sphere.h"
#include "scene/Scene.h"

namespace ts {

namespace {

constexpr int kSampleVertices = 12;    // first vertices of the sphere / cylinder written for the workbook
constexpr int kSampleIndices = 36;     // first indices of the sphere / cylinder

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

std::string vec(const Vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[%.4g, %.4g, %.4g]", v.x, v.y, v.z);
    return buf;
}

// Full-precision vector (used for the S / R / T values that the Excel formulas recompute M from).
std::string vecFull(const Vec3& v) { return "[" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + "]"; }

// 4x4 matrix written ROW-MAJOR (row 0 first) although Mat4 stores it column-major in memory.
std::string matRowMajor(const Mat4& m) {
    std::string o = "[";
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) o += num(m.at(r, c)) + (r == 3 && c == 3 ? "]" : ", ");
    return o;
}

const char* surfaceName(Surface s) {
    static const char* names[] = {"Plain", "Concrete", "Ground", "Metal", "Paint", "Foliage",
                                  "Bark",  "Rock",     "Wood",   "Sand",  "Rubber", "Glass",
                                  "Corrugated", "Hesco", "Hazard", "Screen", "Asphalt", "Camo", "SteelPlate"};
    const int i = static_cast<int>(s);
    return (i >= 0 && i < static_cast<int>(sizeof(names) / sizeof(names[0]))) ? names[i] : "Other";
}

struct PrimInfo {
    const char* id;
    PrimitiveType type;
    std::string function;
    MeshData mesh;
};

std::vector<PrimInfo> knownPrimitives() {
    char terrainFn[96];
    std::snprintf(terrainFn, sizeof(terrainFn), "createTerrainDisc(%d, %d, %g)", kTerrainRings, kTerrainSlices,
                  layout::kBaseRadius);
    std::vector<PrimInfo> prims;
    prims.push_back({"P01", PrimitiveType::Cube, "createCube()", createCube()});
    prims.push_back({"P02", PrimitiveType::Sphere, "createSphere(36, 18)", createSphere(36, 18)});
    prims.push_back({"P03", PrimitiveType::Cylinder, "createCylinder(36)", createCylinder(36)});
    prims.push_back({"P04", PrimitiveType::BeamCone, "createCylinder(36, 0.03, 0.5, false)",
                     createCylinder(36, 0.03f, 0.5f, false)});
    prims.push_back({"P05", PrimitiveType::TerrainDisc, terrainFn, createTerrainDisc()});
    prims.push_back({"P06", PrimitiveType::Cone, "createCylinder(36, 0.5, 0, true)", createCylinder(36, 0.5f, 0.0f, true)});
    return prims;
}

// Node ids are unique in the workbook: the three intruder rovers are built by the same factory and share ids
// (ROV-00 ...), so repeats get a "#2" / "#3" suffix (the first keeps the original id).
std::map<const SceneNode*, std::string> g_exportIds;
std::map<std::string, int> g_idSeen;

std::string exportId(const SceneNode& n) {
    auto it = g_exportIds.find(&n);
    if (it != g_exportIds.end()) return it->second;
    const int k = ++g_idSeen[n.id];
    std::string id = k == 1 ? n.id : n.id + "#" + std::to_string(k);
    g_exportIds[&n] = id;
    return id;
}

void writeNode(std::ostream& o, const SceneNode& n, int depth, bool& first, std::map<PrimitiveType, int>& uses) {
    if (!first) o << ",\n";
    first = false;
    if (n.isLeaf()) ++uses[n.primitive];
    const Material& m = n.material;
    o << "    {\"id\": \"" << esc(exportId(n)) << "\", \"object\": \"" << esc(n.object) << "\", \"name\": \"" << esc(n.name)
      << "\", \"primitive\": \"" << primitiveName(n.primitive) << "\", \"parent\": \""
      << (n.parent ? esc(exportId(*n.parent) + " " + n.parent->name) : std::string("World")) << "\", \"parent_id\": \""
      << (n.parent ? esc(exportId(*n.parent)) : std::string()) << "\", \"depth\": " << depth << ", \"shading\": \""
      << (n.isLeaf() ? shadingName(n.shading) : "-") << "\", \"scale\": " << vec(n.local.scale)
      << ", \"rotation\": " << vec(n.local.rotationDeg) << ", \"translation\": " << vec(n.local.translation)
      << ", \"S\": " << vecFull(n.local.scale) << ", \"R_deg\": " << vecFull(n.local.rotationDeg)
      << ", \"T\": " << vecFull(n.local.translation) << ", \"local_matrix\": " << matRowMajor(n.local.matrix())
      << ", \"world_matrix\": " << matRowMajor(n.world) << ", \"heat\": " << m.heat << ", \"emissive\": " << m.emissive
      << ", \"color\": " << vec(m.baseColor) << ", \"specular\": " << m.specular << ", \"shininess\": " << m.shininess
      << ", \"roughness\": " << m.roughness << ", \"roughness_effective\": " << pbrRoughness(m)
      << ", \"metalness\": " << m.metalness << ", \"surface\": \"" << surfaceName(m.surface)
      << "\", \"occluder\": " << (n.occluder ? "true" : "false") << ", \"obstacle\": " << (n.obstacle ? "true" : "false")
      << ", \"purpose\": \"" << esc(n.purpose)
      << "\", \"animation\": \"" << esc(n.animation) << "\"}";
    for (const auto& c : n.children) writeNode(o, *c, depth + 1, first, uses);
}

void writeMeshSample(std::ostream& o, const char* key, const MeshData& mesh, size_t maxVerts, size_t maxIdx,
                     bool last) {
    const size_t nv = std::min(maxVerts, mesh.vertexCount());
    const size_t ni = std::min(maxIdx, mesh.indexCount());
    o << "    \"" << key << "\": {\"total_vertices\": " << mesh.vertexCount() << ", \"total_indices\": "
      << mesh.indexCount() << ",\n      \"vertices\": [";
    for (size_t i = 0; i < nv; ++i) {
        const Vertex& v = mesh.vertices[i];
        o << (i ? ",\n        [" : "\n        [") << num(v.position.x) << ", " << num(v.position.y) << ", "
          << num(v.position.z) << ", " << num(v.normal.x) << ", " << num(v.normal.y) << ", " << num(v.normal.z)
          << "]";
    }
    o << "],\n      \"indices\": [";
    for (size_t i = 0; i < ni; ++i) o << (i ? ", " : "") << mesh.indices[i];
    o << "]}" << (last ? "\n" : ",\n");
}

void writePrimitives(std::ostream& o, const std::vector<PrimInfo>& prims, std::map<PrimitiveType, int>& uses) {
    o << "  \"primitives\": [\n";
    std::vector<std::string> entries;
    std::map<PrimitiveType, bool> known;
    for (const PrimInfo& p : prims) {
        known[p.type] = true;
        std::ostringstream e;
        e << "    {\"id\": \"" << p.id << "\", \"type\": \"" << primitiveName(p.type) << "\", \"function\": \""
          << esc(p.function) << "\", \"vertices\": " << p.mesh.vertexCount() << ", \"indices\": "
          << p.mesh.indexCount() << ", \"triangles\": " << p.mesh.triangleCount() << ", \"vertex_bytes\": "
          << sizeof(Vertex) << ", \"bytes_indexed\": " << p.mesh.indexedBytes() << ", \"bytes_non_indexed\": "
          << p.mesh.nonIndexedBytes() << ", \"instances\": " << uses[p.type] << "}";
        entries.push_back(e.str());
    }
    // Primitive types used by the scene that this exporter has no generator entry for (e.g. a newly added
    // type): listed with their instance count so the workbook never silently drops them.
    int next = static_cast<int>(prims.size()) + 1;
    for (const auto& kv : uses) {
        if (known.count(kv.first) || kv.first == PrimitiveType::None) continue;
        char id[8];
        std::snprintf(id, sizeof(id), "P%02d", next++);
        std::ostringstream e;
        e << "    {\"id\": \"" << id << "\", \"type\": \"" << primitiveName(kv.first)
          << "\", \"function\": \"(not in DocExport table)\", \"vertices\": null, \"indices\": null, \"triangles\": null,"
          << " \"vertex_bytes\": " << sizeof(Vertex) << ", \"bytes_indexed\": null, \"bytes_non_indexed\": null,"
          << " \"instances\": " << kv.second << "}";
        entries.push_back(e.str());
    }
    for (size_t i = 0; i < entries.size(); ++i) o << entries[i] << (i + 1 < entries.size() ? ",\n" : "\n");
    o << "  ],\n";
}

void writeLights(std::ostream& o, const LightRig& L) {
    o << "  \"lights\": {\n";
    o << "    \"moon\": {\"direction\": " << vec(L.moon.direction) << ", \"color\": " << vec(L.moon.color)
      << ", \"intensity\": " << L.moon.intensity << "},\n";
    o << "    \"beacon\": {\"position\": " << vec(L.beacon.position) << ", \"color\": " << vec(L.beacon.color)
      << ", \"intensity\": " << L.beacon.intensity << ", \"intensity_min\": 0.6, \"intensity_max\": 2.0, \"linear\": "
      << L.beacon.linear << ", \"quadratic\": " << L.beacon.quadratic << "},\n";
    o << "    \"spot\": {\"position\": " << vec(L.spot.position) << ", \"direction\": " << vec(L.spot.direction)
      << ", \"color\": " << vec(L.spot.color) << ", \"intensity\": " << L.spot.intensity
      << ", \"inner_deg\": " << L.spot.innerDeg << ", \"outer_deg\": " << L.spot.outerDeg
      << ", \"linear\": " << L.spot.linear << ", \"quadratic\": " << L.spot.quadratic << "},\n";
    o << "    \"ambient\": " << vec(L.ambient) << "\n  },\n";
}

// ---------------------------------------------------------------------------------------------------------
// v4 sections: controllables, walk space, combat constants, per-object part counts, practical lights,
// measured effect census and a short headless simulation (intruder life cycle, rail-round reload).
// ---------------------------------------------------------------------------------------------------------

struct Counts {
    int leaves = 0, groups = 0, animated = 0;
    std::map<std::string, int> prim;
};

void countNode(const SceneNode& n, Counts& c) {
    if (n.isLeaf()) {
        ++c.leaves;
        ++c.prim[primitiveName(n.primitive)];
    } else {
        ++c.groups;
    }
    if (!n.animation.empty()) ++c.animated;
    for (const auto& ch : n.children) countNode(*ch, c);
}

std::string primJson(const Counts& c) {
    std::string o = "{";
    bool first = true;
    for (const auto& kv : c.prim) {
        if (!first) o += ", ";
        first = false;
        o += "\"" + esc(kv.first) + "\": " + std::to_string(kv.second);
    }
    return o + "}";
}

void collectAnimated(const SceneNode& n, std::vector<const SceneNode*>& out) {
    if (!n.animation.empty()) out.push_back(&n);
    for (const auto& ch : n.children) collectAnimated(*ch, out);
}

struct Spec {
    std::string key, value, unit;
};

std::string specsJson(const std::vector<Spec>& specs) {
    std::string o = "[";
    for (size_t i = 0; i < specs.size(); ++i)
        o += std::string(i ? ", " : "") + "{\"key\": \"" + esc(specs[i].key) + "\", \"value\": " + specs[i].value +
             ", \"unit\": \"" + esc(specs[i].unit) + "\"}";
    return o + "]";
}

std::string numShort(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

Spec sp(const char* key, float v, const char* unit) { return {key, numShort(v), unit}; }
Spec sp(const char* key, int v, const char* unit) { return {key, std::to_string(v), unit}; }

std::vector<Spec> specsFor(Controllable* c) {
    std::vector<Spec> s;
    if (auto* t = dynamic_cast<Tank*>(c)) {
        (void)t;
        s = {sp("max forward speed", Tank::kMaxForward, "m/s"), sp("max reverse speed", Tank::kMaxReverse, "m/s"),
             sp("boost factor (Shift)", Tank::kBoost, "x"), sp("track separation", Tank::kTrackSeparation, "m"),
             sp("hull length", Tank::kHullLength, "m"), sp("hull width", Tank::kHullWidth, "m"),
             sp("track links per side", Tank::kLinksPerSide, "links"), sp("road wheels per side", Tank::kRoadWheels, "wheels"),
             sp("turret slew rate", Tank::kTurretSlewDeg, "deg/s"), sp("gun slew rate", Tank::kGunSlewDeg, "deg/s"),
             sp("gun elevation min", Tank::kGunMinDeg, "deg"), sp("gun elevation max", Tank::kGunMaxDeg, "deg"),
             sp("reload time", Tank::kReload, "s"), sp("muzzle speed", Tank::kMuzzleSpeed, "m/s"),
             sp("recoil distance", Tank::kRecoilDistance, "m"), sp("recoil return time", Tank::kRecoilReturn, "s"),
             sp("start rounds", Tank::kStartRounds, "rounds")};
    } else if (dynamic_cast<AaGun*>(c)) {
        s = {sp("barrels", AaGun::kBarrels, "barrels"), sp("elevation min", AaGun::kPitchMinDeg, "deg"),
             sp("elevation max", AaGun::kPitchMaxDeg, "deg"), sp("slew rate", AaGun::kSlewDeg, "deg/s"),
             sp("barrel spool-up time", AaGun::kSpoolUp, "s"), sp("barrel spool-down time", AaGun::kSpoolDown, "s"),
             sp("max barrel spin", AaGun::kMaxSpin, "rev/s"), sp("rate of fire", AaGun::kRoundsPerSecond, "rounds/s"),
             sp("muzzle speed", AaGun::kMuzzleSpeed, "m/s"), sp("dispersion", AaGun::kDispersion, "rad"),
             sp("start rounds", AaGun::kStartRounds, "rounds"), sp("casing pool", AaGun::kCasingPool, "casings")};
    } else if (dynamic_cast<Helicopter*>(c)) {
        s = {sp("main blades", Helicopter::kBlades, "blades"), sp("rotor rpm (nominal)", Helicopter::kRotorRpm, "rpm"),
             sp("tail rotor ratio", Helicopter::kTailRatio, "x main"), sp("spool-up", Helicopter::kSpoolUpSeconds, "s"),
             sp("spool-down", Helicopter::kSpoolDownSeconds, "s"), sp("cruise speed", Helicopter::kMaxSpeed, "m/s"),
             sp("boost speed", Helicopter::kBoostSpeed, "m/s"), sp("climb rate", Helicopter::kClimbRate, "m/s"),
             sp("min altitude AGL (airborne)", Helicopter::kMinAgl, "m"), sp("max altitude AGL", Helicopter::kMaxAgl, "m"),
             sp("max radius from origin", Helicopter::kMaxRadius, "m"),
             sp("landing: max ground speed", Helicopter::kLandMaxSpeed, "m/s"),
             sp("landing: max AGL", Helicopter::kLandMaxAgl, "m"), sp("landing: max slope", Helicopter::kLandMaxSlope, "rise/run"),
             sp("body pivot above skids", Helicopter::kGroundClearance, "m")};
    } else if (dynamic_cast<Radar*>(c)) {
        s = {sp("rotation rate", Radar::kRpm, "rpm"), sp("sector half width", Radar::kSectorHalfWidth, "deg"),
             sp("sector sweep period", Radar::kSectorPeriod, "s"), sp("tilt min", Radar::kTiltMin, "deg"),
             sp("tilt max", Radar::kTiltMax, "deg"), sp("antenna tile columns", Radar::kTileCols, "tiles"),
             sp("antenna tile rows", Radar::kTileRows, "tiles")};
    } else if (dynamic_cast<GateBarrier*>(c)) {
        s = {sp("max boom angle", GateBarrier::kMaxAngleDeg, "deg"), sp("travel time", GateBarrier::kTravelSeconds, "s"),
             sp("blocking below", GateBarrier::kBlockingAngleDeg, "deg")};
    } else if (dynamic_cast<WavingFlag*>(c)) {
        s = {sp("strips in chain", WavingFlag::kStrips, "strips"), sp("strip width", WavingFlag::kStripWidth, "m"),
             sp("flag height", WavingFlag::kFlagHeight, "m")};
    } else if (auto* w = dynamic_cast<Windsock*>(c)) {
        s = {sp("cone segments in chain", w->segmentCount(), "segments")};
    } else if (std::string(c->name()) == "BLAST DOORS") {
        s = {sp("door leaf width", bunker::kLeafWidth, "m"), sp("clear opening half width", bunker::kDoorOpeningHalf, "m"),
             sp("travel time", bunker::kDoorTravelSeconds, "s")};
    } else if (std::string(c->name()).find("UAV") != std::string::npos) {
        s = {sp("rail rounds", kUavMissiles, "rounds"), sp("searchlight inner cone", kSpotInnerDeg, "deg"),
             sp("searchlight outer cone", kSpotOuterDeg, "deg"), sp("thermal core width", kCvWidth, "px"),
             sp("thermal core height", kCvHeight, "px"), sp("thermal vertical FOV", kCvFovYDeg, "deg"),
             sp("lock time before launch", kLockTime, "s"), sp("launch interval", kLaunchInterval, "s"),
             sp("reload time per round", kUavReloadTime, "s")};
    }
    return s;
}

std::string camJson(const CameraHint& h) {
    return "{\"eye\": " + vec(h.eye) + ", \"target\": " + vec(h.target) + ", \"fov_deg\": " + num(h.fovDeg) +
           ", \"sight\": " + (h.sight ? "true" : "false") + "}";
}

void writeControllables(std::ostream& o, Scene& scene) {
    o << "  \"controllables\": [\n";
    const auto& list = scene.controllables();
    for (size_t i = 0; i < list.size(); ++i) {
        Controllable* c = list[i].get();
        SceneNode* r = c->root();
        Counts cn;
        if (r) countNode(*r, cn);
        std::vector<const SceneNode*> anim;
        if (r) collectAnimated(*r, anim);
        char key = '-';
        const int slot = slotOfName(c->name());
        if (slot >= 0) key = slotInfo(static_cast<Slot>(slot)).key;
        const Vec3 p = c->position();
        o << "    {\"index\": " << i << ", \"key\": \"" << key << "\", \"name\": \"" << esc(c->name())
          << "\", \"controls\": \"" << esc(c->controls()) << "\", \"possessable\": " << (c->possessable() ? "true" : "false")
          << ", \"root_id\": \"" << (r ? esc(exportId(*r)) : "") << "\", \"root_name\": \"" << (r ? esc(r->name) : "")
          << "\", \"root_object\": \"" << (r ? esc(r->object) : "") << "\", \"parts\": " << cn.leaves
          << ", \"groups\": " << cn.groups << ", \"animated_nodes\": " << cn.animated << ", \"primitives\": " << primJson(cn)
          << ", \"position\": " << vec(p) << ", \"status\": \"" << esc(c->status())
          << "\", \"camera_chase\": " << camJson(c->camera(false)) << ", \"camera_sight\": " << camJson(c->camera(true))
          << ", \"dynamic_obstacles\": " << c->dynamicObstacles().size() << ",\n      \"specs\": " << specsJson(specsFor(c))
          << ",\n      \"moving_parts\": [";
        for (size_t k = 0; k < anim.size(); ++k)
            o << (k ? ", " : "") << "{\"id\": \"" << esc(exportId(*anim[k])) << "\", \"name\": \"" << esc(anim[k]->name)
              << "\", \"animation\": \"" << esc(anim[k]->animation) << "\"}";
        o << "]}" << (i + 1 < list.size() ? ",\n" : "\n");
    }
    o << "  ],\n";
}

std::string boxJson(const Box& b) { return "{\"min\": " + vec(b.min) + ", \"max\": " + vec(b.max) + "}"; }

void writeWalkSpace(std::ostream& o, const Scene& scene) {
    const WalkSpace& w = scene.walkSpace();
    o << "  \"walkspace\": {\n    \"floors\": [";
    for (size_t i = 0; i < w.floors().size(); ++i) {
        const WalkFloor& f = w.floors()[i];
        o << (i ? ",\n      " : "\n      ") << "{\"x0\": " << f.x0 << ", \"z0\": " << f.z0 << ", \"x1\": " << f.x1
          << ", \"z1\": " << f.z1 << ", \"y0\": " << f.y0 << ", \"y1\": " << f.y1 << ", \"slope_axis\": " << f.slopeAxis
          << ", \"ceiling\": " << (f.ceiling > 1e8f ? std::string("null") : num(f.ceiling)) << "}";
    }
    o << "],\n    \"walls\": [";
    for (size_t i = 0; i < w.walls().size(); ++i) o << (i ? ",\n      " : "\n      ") << boxJson(w.walls()[i]);
    o << "],\n    \"indoor\": [";
    for (size_t i = 0; i < w.indoorVolumes().size(); ++i)
        o << (i ? ",\n      " : "\n      ") << boxJson(w.indoorVolumes()[i]);
    o << "]\n  },\n";
}

void writeLayout(std::ostream& o) {
    using namespace layout;
    auto v = [](const char* k, const Vec3& p) { return std::string("\"") + k + "\": " + vec(p); };
    auto f = [](const char* k, float x) { return std::string("\"") + k + "\": " + num(x); };
    o << "  \"layout\": {" << f("base_radius", kBaseRadius) << ", " << f("perimeter_radius", kPerimeterRadius) << ", "
      << f("apron_level", kApronLevel) << ", " << f("bunker_floor_y", kBunkerFloorY) << ", "
      << f("bunker_ceiling_y", kBunkerCeilingY) << ", " << f("bunker_roof_top_y", kBunkerRoofTopY) << ", "
      << f("ramp_half_width", kRampHalfWidth) << ", " << f("ramp_top_z", kRampTopZ) << ", "
      << f("ramp_bottom_z", kRampBottomZ) << ", " << f("blast_door_z", kBlastDoorZ) << ", "
      << f("bunker_min_x", kBunkerMinX) << ", " << f("bunker_max_x", kBunkerMaxX) << ", " << f("bunker_min_z", kBunkerMinZ)
      << ", " << f("bunker_max_z", kBunkerMaxZ) << ", " << v("outer_gate", kOuterGate) << ", " << v("helipad", kHelipad)
      << ", " << f("helipad_radius", kHelipadRadius) << ", " << v("radar_site", kRadarSite) << ", "
      << v("aa_gun_site", kAaGunSite) << ", " << v("motor_pool", kMotorPool) << ", " << v("barracks", kBarracks) << ", "
      << v("flagpole", kFlagpole) << ", " << v("fuel_depot", kFuelDepot) << ", " << f("road_half_width", kRoadHalfWidth)
      << ", \"rover_count\": " << kRoverCount << "},\n";
}

void writeCombat(std::ostream& o) {
    auto f = [](const char* k, float x, const char* u, const char* src) {
        return std::string("{\"key\": \"") + k + "\", \"value\": " + numShort(x) + ", \"unit\": \"" + u + "\", \"source\": \"" +
               src + "\"}";
    };
    const Box ko{{-layout::kRampHalfWidth - 3.0f, -10.0f, layout::kBlastDoorZ - 3.0f},
                 {layout::kRampHalfWidth + 3.0f, 10.0f, layout::kRampTopZ + 3.0f}};
    o << "  \"combat\": {\n    \"constants\": [\n      "
      << f("kParticleCap", static_cast<float>(Combat::kParticleCap), "particles", "Combat.h") << ",\n      "
      << f("kMissileMaxSpeed", Combat::kMissileMaxSpeed, "m/s", "Combat.h") << ",\n      "
      << f("kMissileRampTime", Combat::kMissileRampTime, "s", "Combat.h") << ",\n      "
      << f("kMissileLife", Combat::kMissileLife, "s", "Combat.h") << ",\n      "
      << f("kMissileTurnRate", Combat::kMissileTurnRate, "rad/s", "Combat.h") << ",\n      "
      << f("kMissileFuse", Combat::kMissileFuse, "m", "Combat.h") << ",\n      "
      << f("kShellSpeed", Combat::kShellSpeed, "m/s", "Combat.h") << ",\n      "
      << f("kTracerSpeed", Combat::kTracerSpeed, "m/s", "Combat.h") << ",\n      "
      << f("kTracerLife", Combat::kTracerLife, "s", "Combat.h") << ",\n      "
      << f("kDamageRadiusPerPower", Combat::kDamageRadiusPerPower, "m per unit power", "Combat.h") << ",\n      "
      << f("kShakeRange", Combat::kShakeRange, "m", "Combat.h") << ",\n      "
      << f("kShakeDecay", Combat::kShakeDecay, "s", "Combat.h") << ",\n      "
      << f("kLockTime", kLockTime, "s", "DroneController.h") << ",\n      "
      << f("kLaunchInterval", kLaunchInterval, "s", "DroneController.h") << ",\n      "
      << f("kWreckIgnoreRadius", kWreckIgnoreRadius, "m", "DroneController.h") << ",\n      "
      << f("kWreckIgnoreTime", kWreckIgnoreTime, "s", "DroneController.h") << ",\n      "
      << f("kFriendlyRadius", kFriendlyRadius, "m", "DroneController.h") << ",\n      "
      << f("kUavMissiles", static_cast<float>(kUavMissiles), "rounds", "Uav.h") << ",\n      "
      << f("kUavReloadTime", kUavReloadTime, "s", "Scene.h") << ",\n      "
      << f("kWreckBurnTime", kWreckBurnTime, "s", "Scene.h") << ",\n      "
      << f("kWreckClearTime", kWreckClearTime, "s", "Scene.h") << ",\n      "
      << f("kRespawnDelay", kRespawnDelay, "s", "Scene.h") << ",\n      "
      << f("kDroneOwner", static_cast<float>(kDroneOwner), "id", "Scene.h") << "\n    ],\n";
    o << "    \"keep_out_box\": " << boxJson(ko) << ",\n";
    o << "    \"tank_shell_power_note\": \"BlastEvent::power: 1 = UAV missile, about 1.6 tank shell, 0.15 tracer impact\"\n  },\n";
}

// Particle census of one effect, produced by running the real Combat simulation.
struct Census {
    std::string name;
    float power = 0;
    int total = 0, peak = 0;
    float lastAlive = 0.0f;
    std::map<int, int> byType;
    std::map<int, std::pair<float, float>> lifeRange, sizeRange;
    int flashLights = 0;   // peak number of simultaneous dynamic lights during the effect
};

const char* effectName(int t) {
    static const char* n[] = {"Fire", "Smoke", "Spark", "Flash", "Shockwave", "Trail", "Dust", "Ember"};
    return (t >= 0 && t < 8) ? n[t] : "?";
}

template <class Fire>
Census runEffect(const char* name, float power, Fire fire, float simSeconds = 16.0f) {
    Combat c;
    Census r;
    r.name = name;
    r.power = power;
    fire(c);
    for (const EffectParticle& p : c.particles()) {
        ++r.total;
        ++r.byType[static_cast<int>(p.type)];
        const int t = static_cast<int>(p.type);
        auto lr = r.lifeRange.find(t);
        if (lr == r.lifeRange.end()) {
            r.lifeRange[t] = {p.life, p.life};
            r.sizeRange[t] = {p.size, p.size};
        } else {
            lr->second.first = std::min(lr->second.first, p.life);
            lr->second.second = std::max(lr->second.second, p.life);
            r.sizeRange[t].first = std::min(r.sizeRange[t].first, p.size);
            r.sizeRange[t].second = std::max(r.sizeRange[t].second, p.size);
        }
    }
    r.peak = r.total;
    const float dt = 1.0f / 60.0f;
    float t = 0;
    for (int i = 0; i < static_cast<int>(simSeconds / dt); ++i) {
        t += dt;
        c.update(dt, t);
        r.peak = std::max(r.peak, static_cast<int>(c.particles().size()));
        r.flashLights = std::max(r.flashLights, static_cast<int>(c.lights().size()));
        if (!c.particles().empty() || c.projectileCount() > 0) r.lastAlive = t;
    }
    return r;
}

void writeEffects(std::ostream& o) {
    std::vector<Census> all;
    all.push_back(runEffect("UAV missile detonation (power 1.0)", 1.0f, [](Combat& c) { c.explode({0, 0.5f, 0}, 1.0f, 0); }));
    all.push_back(runEffect("Tank shell detonation (power 1.6)", 1.6f, [](Combat& c) { c.explode({0, 0.5f, 0}, 1.6f, 1); }));
    all.push_back(runEffect("Tracer impact (power 0.15)", 0.15f, [](Combat& c) { c.explode({0, 0.5f, 0}, 0.15f, 2); }, 6.0f));
    all.push_back(runEffect("Muzzle flash (size 1.0)", 0.0f, [](Combat& c) { c.muzzleFlash({0, 1, 0}, {0, 0, 1}, 1.0f); }, 6.0f));
    all.push_back(runEffect("Dust burst (radius 2, 20 puffs)", 0.0f, [](Combat& c) { c.emitDust({0, 0, 0}, 2.0f, 20); }, 8.0f));
    // Burning wreck: one second of emitBurn at 60 Hz (rate based), measured over the following 10 s.
    all.push_back(runEffect("Burning wreck, 1 s of emitBurn(intensity 1)", 0.0f, [](Combat& c) {
        for (int i = 0; i < 60; ++i) c.emitBurn({0, 0, 0}, 1.0f, 1.0f / 60.0f);
    }, 10.0f));
    o << "  \"effects\": [\n";
    for (size_t i = 0; i < all.size(); ++i) {
        Census& r = all[i];
        o << "    {\"name\": \"" << esc(r.name) << "\", \"power\": " << r.power << ", \"particles_at_spawn\": " << r.total
          << ", \"peak_particles\": " << r.peak << ", \"seconds_until_empty\": " << numShort(r.lastAlive)
          << ", \"flash_lights\": " << r.flashLights << ", \"types\": [";
        bool first = true;
        for (const auto& kv : r.byType) {
            o << (first ? "" : ", ") << "{\"type\": \"" << effectName(kv.first) << "\", \"count\": " << kv.second
              << ", \"life_min\": " << numShort(r.lifeRange[kv.first].first) << ", \"life_max\": " << numShort(r.lifeRange[kv.first].second)
              << ", \"size_min\": " << numShort(r.sizeRange[kv.first].first) << ", \"size_max\": " << numShort(r.sizeRange[kv.first].second) << "}";
            first = false;
        }
        o << "]}" << (i + 1 < all.size() ? ",\n" : "\n");
    }
    o << "  ],\n";
}

const char* lifeName(Intruder::Life l) {
    switch (l) {
        case Intruder::Life::Waiting: return "Waiting";
        case Intruder::Life::Alive: return "Alive";
        case Intruder::Life::Wreck: return "Wreck";
        case Intruder::Life::Gone: return "Gone";
    }
    return "?";
}

// Headless run of a second Scene: when each intruder enters, the forced destruction of intruder 0 at t = 40 s
// (life-cycle transitions with timestamps), and the rail-round reload after one launch.
void writeSimulation(std::ostream& o) {
    Scene sim;
    Detection det;
    const float dt = 1.0f / 30.0f;
    const int n = static_cast<int>(sim.intruders().size());
    std::vector<Intruder::Life> prev(n, Intruder::Life::Waiting);
    struct Ev {
        float t;
        int who;
        std::string what;
    };
    std::vector<Ev> ev;
    bool killed = false, launched = false, reloaded = false;
    float launchT = 0, reloadT = 0;
    int launchOk = 0, readyBefore = sim.roundsReady(), readyAfter = -1;
    float t = 0;
    for (int step = 0; step < static_cast<int>(130.0f / dt); ++step) {
        sim.update(dt, det);
        t += dt;
        if (!killed && t >= 40.0f && sim.roverAlive(0)) {
            killed = true;
            ev.push_back({t, 0, "destroyIntruder(0, power 1.0)"});
            sim.destroyIntruder(0, sim.rovers()[0].position + Vec3{0, 0.3f, 0}, 1.0f);
        }
        if (!launched && t >= 20.0f) {
            launched = true;
            launchT = t;
            launchOk = sim.launchRound() ? 1 : 0;
            readyAfter = sim.roundsReady();
        }
        if (launched && launchOk && !reloaded && sim.roundsReady() >= readyBefore) {
            reloaded = true;
            reloadT = t - launchT;
        }
        for (int i = 0; i < n; ++i) {
            const Intruder::Life l = sim.intruders()[i]->life;
            if (l != prev[i]) {
                ev.push_back({t, i, std::string(lifeName(prev[i])) + " -> " + lifeName(l)});
                prev[i] = l;
            }
        }
    }
    o << "  \"simulation\": {\"dt\": " << num(dt) << ", \"duration\": " << num(t) << ", \"intruders\": " << n
      << ", \"kills\": " << sim.kills() << ", \"launch_ok\": " << launchOk << ", \"rounds_ready_before\": " << readyBefore
      << ", \"rounds_ready_after_launch\": " << readyAfter << ", \"reload_seconds_measured\": " << (reloaded ? num(reloadT) : "null")
      << ",\n    \"events\": [";
    for (size_t i = 0; i < ev.size(); ++i)
        o << (i ? ",\n      " : "\n      ") << "{\"t\": " << num(ev[i].t) << ", \"intruder\": " << ev[i].who << ", \"event\": \""
          << esc(ev[i].what) << "\"}";
    o << "]},\n";
}

void writeObjects(std::ostream& o, Scene& scene) {
    // per final object (the SceneNode::object tag), first-seen order
    std::vector<std::string> order;
    std::map<std::string, Counts> perObj;
    std::function<void(const SceneNode&)> walk = [&](const SceneNode& n) {
        if (!perObj.count(n.object)) order.push_back(n.object);
        Counts& c = perObj[n.object];
        if (n.isLeaf()) {
            ++c.leaves;
            ++c.prim[primitiveName(n.primitive)];
        } else {
            ++c.groups;
        }
        if (!n.animation.empty()) ++c.animated;
        for (const auto& ch : n.children) walk(*ch);
    };
    for (SceneNode* r : scene.roots()) walk(*r);
    o << "  \"objects\": [\n";
    for (size_t i = 0; i < order.size(); ++i) {
        const Counts& c = perObj[order[i]];
        o << "    {\"object\": \"" << esc(order[i]) << "\", \"parts\": " << c.leaves << ", \"groups\": " << c.groups
          << ", \"animated_nodes\": " << c.animated << ", \"primitives\": " << primJson(c) << "}" << (i + 1 < order.size() ? ",\n" : "\n");
    }
    o << "  ],\n  \"roots\": [\n";
    const auto roots = scene.roots();
    for (size_t i = 0; i < roots.size(); ++i) {
        Counts c;
        countNode(*roots[i], c);
        o << "    {\"id\": \"" << esc(exportId(*roots[i])) << "\", \"name\": \"" << esc(roots[i]->name) << "\", \"object\": \""
          << esc(roots[i]->object) << "\", \"parts\": " << c.leaves << ", \"groups\": " << c.groups << ", \"primitives\": "
          << primJson(c) << ", \"sections\": [";
        bool first = true;
        for (const auto& ch : roots[i]->children) {
            Counts s;
            countNode(*ch, s);
            o << (first ? "" : ", ") << "{\"id\": \"" << esc(exportId(*ch)) << "\", \"name\": \"" << esc(ch->name)
              << "\", \"object\": \"" << esc(ch->object) << "\", \"parts\": " << s.leaves << ", \"groups\": " << s.groups
              << ", \"primitives\": " << primJson(s) << "}";
            first = false;
        }
        o << "]}" << (i + 1 < roots.size() ? ",\n" : "\n");
    }
    o << "  ],\n";
}

void writePracticals(std::ostream& o, const Scene& scene) {
    const auto& pl = scene.practicalLights();
    o << "  \"practical_lights\": [\n";
    for (size_t i = 0; i < pl.size(); ++i) {
        const SceneNode& n = *pl[i];
        const Material& m = n.material;
        o << "    {\"id\": \"" << esc(exportId(n)) << "\", \"name\": \"" << esc(n.name) << "\", \"object\": \"" << esc(n.object)
          << "\", \"position\": " << vec({n.world.at(0, 3), n.world.at(1, 3), n.world.at(2, 3)})
          << ", \"color\": " << vec(m.baseColor) << ", \"emissive\": " << m.emissive << ", \"intensity\": " << m.lightIntensity
          << ", \"range\": " << m.lightRange << ", \"cone_deg\": " << m.lightConeDeg
          << ", \"animated\": " << (n.animation.empty() ? "false" : "true") << "}" << (i + 1 < pl.size() ? ",\n" : "\n");
    }
    o << "  ],\n";
}

}  // namespace

bool exportSceneDocs(const std::string& path) {
    g_exportIds.clear();
    g_idSeen.clear();
    Scene scene;  // freshly constructed = the initial (t = 0) state
    scene.updateWorld();
    const std::vector<PrimInfo> prims = knownPrimitives();

    std::ostringstream o;
    std::map<PrimitiveType, int> uses;
    o << "{\n  \"schema_version\": 3,\n"
      << "  \"matrix_convention\": \"local_matrix and world_matrix are 16 floats ROW-MAJOR (row 0 first); "
         "M = T * Ry * Rx * Rz * S (R_deg = pitch X, yaw Y, roll Z); world = parent_world * M; the code stores "
         "Mat4 column-major (m[col*4+row]); world matrices are at t = 0\",\n";
    o << "  \"terrain\": {\"radius\": " << layout::kBaseRadius << ", \"compound_radius\": " << kTerrainRadius
      << ", \"rings\": " << kTerrainRings
      << ", \"slices\": " << kTerrainSlices << "},\n";
    o << "  \"components\": [\n";
    bool first = true;
    for (SceneNode* r : scene.roots()) writeNode(o, *r, 0, first, uses);
    o << "\n  ],\n";
    writePrimitives(o, prims, uses);
    writeLights(o, scene.lights);
    writeObjects(o, scene);
    writeControllables(o, scene);
    writeWalkSpace(o, scene);
    writeLayout(o);
    writeCombat(o);
    writeEffects(o);
    writePracticals(o, scene);
    writeSimulation(o);
    o << "  \"mesh_samples\": {\n";
    writeMeshSample(o, "Cube", prims[0].mesh, prims[0].mesh.vertexCount(), prims[0].mesh.indexCount(), false);
    writeMeshSample(o, "Sphere", prims[1].mesh, kSampleVertices, kSampleIndices, false);
    writeMeshSample(o, "Cylinder", prims[2].mesh, kSampleVertices, kSampleIndices, true);
    o << "  },\n";
    o << "  \"leaf_count\": " << scene.leaves().size() << "\n}\n";

    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", path.c_str());
        return false;
    }
    f << o.str();
    std::printf("Scene documentation written to %s (%zu primitive instances)\n", path.c_str(), scene.leaves().size());
    return true;
}

}  // namespace ts
