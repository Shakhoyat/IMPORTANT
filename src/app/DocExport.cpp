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
#include "ai/Squad.h"
#include "objects/Launchers.h"
#include "scene/Mission.h"
#include "vision/CvPipeline.h"
#include "vision/MultiTracker.h"
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
#include "primitives/FlatFaces.h"
#include "primitives/GrassBlade.h"
#include "primitives/Library.h"
#include "primitives/Annulus.h"
#include "primitives/Capsule.h"
#include "primitives/Helix.h"
#include "primitives/Hemisphere.h"
#include "primitives/Pyramid.h"
#include "primitives/TaperedBox.h"
#include "primitives/Torus.h"
#include "primitives/Wedge.h"
#include "report/Designs.h"
#include "scene/PartOrder.h"
#include "scene/DayCycle.h"
#include "scene/Gait.h"
#include "scene/HudMath.h"
#include "scene/Garrison.h"
#include "scene/Scene.h"
#include "vision/SensorModel.h"
#include "math/ExplorerMath.h"     // v7 WP-W: Frame Explorer stage table + worked examples
#include "raytrace/RayTracer.h"    // v7 WP-W: RT modes, trace settings, emitter power
#include "raytrace/SceneBridge.h"  // v7 WP-W: BVH statistics of the camp
#include "scene/CameraRig.h"       // v7 WP-W: camera blend constants
#include "scene/Pov.h"             // v7 WP-W: possessable / POV sources

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

// v10 M3: the primitive table is the 16-entry library (primitives/Library.cpp) in report order. Instances partition
// the scene's leaves: a cube-mesh part counts as "Cube" when its S is uniform and as "Cuboid" otherwise; the grass
// blade has no scene leaves (its GPU instances are listed separately).
struct PrimUse {
    int instances = 0;
    const SceneNode* example = nullptr;   // first part (export order) that uses it
};

std::string primKey(const SceneNode& n) {
    if (n.primitive == PrimitiveType::Cube && isCuboidScale(restScaleOf(n))) return "Cuboid";
    return primitiveName(n.primitive);
}

std::string entryKey(const LibraryEntry& e) { return e.cuboid ? std::string("Cuboid") : std::string(primitiveName(e.type)); }

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

// v10 M3 per-component fields: hierarchy order (global depth-first index; leaf index inside its rig), the rest scale and
// whether the part is hidden in the exported t = 0 state.
int g_dfs = 0;
std::map<const SceneNode*, int> g_rigLeafOrder;
// Report designs (src/report/Designs.h): the design and instance root of every node.
designs::Catalogue g_designs;

void indexRigLeaves(const SceneNode& n) {
    if (rigRoot(n) == &n) {
        const std::vector<const SceneNode*> leaves = rigLeavesDfs(n);
        for (size_t i = 0; i < leaves.size(); ++i) g_rigLeafOrder[leaves[i]] = static_cast<int>(i);
    }
    for (const auto& c : n.children) indexRigLeaves(*c);
}

void writeNode(std::ostream& o, const SceneNode& n, int depth, bool& first, std::map<std::string, PrimUse>& uses) {
    if (!first) o << ",\n";
    first = false;
    if (n.isLeaf()) {
        PrimUse& u = uses[primKey(n)];
        ++u.instances;
        if (!u.example) u.example = &n;
    }
    const auto order = g_rigLeafOrder.find(&n);
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
      << "\", \"animation\": \"" << esc(n.animation) << "\", \"S_rest\": " << vecFull(restScaleOf(n))
      << ", \"hidden_at_export\": " << (hiddenAtRest(n) ? "true" : "false") << ", \"dfs_index\": " << g_dfs++
      << ", \"rig_root_id\": \"" << esc(exportId(*rigRoot(n))) << "\", \"rig_leaf_order\": "
      << (order != g_rigLeafOrder.end() ? order->second : -1) << ", \"part_kind\": \""
      << esc(n.isLeaf() ? partKindName(n.name) : std::string()) << "\"";
    const int inst = g_designs.instanceAt(n);
    if (inst >= 0) {
        const designs::Instance& in = g_designs.instances[static_cast<size_t>(inst)];
        o << ", \"design\": \"" << esc(g_designs.designs[static_cast<size_t>(in.design)].name) << "\", \"design_root\": \""
          << esc(exportId(*in.root)) << "\"";
    } else {
        o << ", \"design\": \"\", \"design_root\": \"\"";
    }
    o << "}";
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

std::string cornersJson(const MeshData& mesh) {
    const FlatCorners fc = flatCorners(mesh);
    std::string o = ", \"corners\": [";
    for (size_t i = 0; i < fc.corners.size(); ++i) o += std::string(i ? ", " : "") + vecFull(fc.corners[i]);
    o += "], \"faces\": [";
    for (size_t f = 0; f < fc.faces.size(); ++f) {
        o += f ? ", [" : "[";
        for (size_t k = 0; k < fc.faces[f].size(); ++k) o += (k ? ", " : "") + std::to_string(fc.faces[f][k]);
        o += "]";
    }
    return o + "]";
}

void writePrimitives(std::ostream& o, const std::vector<LibraryEntry>& prims, std::map<std::string, PrimUse>& uses) {
    o << "  \"primitives\": [\n";
    std::vector<std::string> entries;
    std::map<std::string, bool> known;
    for (const LibraryEntry& p : prims) {
        const std::string key = entryKey(p);
        known[key] = true;
        const PrimUse& u = uses[key];
        char id[8];
        std::snprintf(id, sizeof(id), "P%02d", p.number);
        std::ostringstream e;
        e << "    {\"id\": \"" << id << "\", \"number\": " << p.number << ", \"name\": \"" << esc(p.name)
          << "\", \"type\": \"" << esc(key) << "\", \"function\": \"" << esc(p.call) << "\", \"vertices\": "
          << p.mesh.vertexCount() << ", \"indices\": " << p.mesh.indexCount() << ", \"triangles\": "
          << p.mesh.triangleCount() << ", \"vertex_bytes\": " << sizeof(Vertex) << ", \"bytes_indexed\": "
          << p.mesh.indexedBytes() << ", \"bytes_non_indexed\": " << p.mesh.nonIndexedBytes() << ", \"instances\": "
          << u.instances;
        if (p.type == PrimitiveType::Cube) e << ", \"mesh_instances\": " << uses["Cube"].instances + uses["Cuboid"].instances;
        if (p.type == PrimitiveType::GrassBlade)
            e << ", \"gpu_instances\": " << kGrassBladesEpic << ", \"gpu_instances_by_quality\": {\"LOW\": " << kGrassBladesLow
              << ", \"HIGH\": " << kGrassBladesHigh << ", \"ULTRA\": " << kGrassBladesUltra << ", \"EPIC\": " << kGrassBladesEpic
              << "}";
        e << ", \"params\": " << p.params << ", \"formula_V\": \"" << esc(p.vFormula) << "\", \"formula_T\": \""
          << esc(p.tFormula) << "\", \"parametric\": \"" << esc(p.parametric) << "\", \"axis\": \"" << esc(p.axis)
          << "\", \"flat\": " << (p.flat ? "true" : "false");
        if (u.example)
            e << ", \"example_part\": {\"id\": \"" << esc(exportId(*u.example)) << "\", \"name\": \"" << esc(u.example->name)
              << "\", \"object\": \"" << esc(u.example->object) << "\", \"S\": " << vecFull(restScaleOf(*u.example)) << "}";
        else
            e << ", \"example_part\": null";
        if (p.flat) e << cornersJson(p.mesh);
        e << "}";
        entries.push_back(e.str());
    }
    // Primitive types used by the scene that this exporter has no generator entry for (e.g. a newly added
    // type): listed with their instance count so the workbook never silently drops them.
    int next = static_cast<int>(prims.size()) + 1;
    for (const auto& kv : uses) {
        if (known.count(kv.first)) continue;
        char id[16];
        std::snprintf(id, sizeof(id), "P%02d", next++);
        std::ostringstream e;
        e << "    {\"id\": \"" << id << "\", \"type\": \"" << esc(kv.first)
          << "\", \"function\": \"(not in DocExport table)\", \"vertices\": null, \"indices\": null, \"triangles\": null,"
          << " \"vertex_bytes\": " << sizeof(Vertex) << ", \"bytes_indexed\": null, \"bytes_non_indexed\": null,"
          << " \"instances\": " << kv.second.instances << "}";
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
    } else if (dynamic_cast<SamLauncher*>(c)) {
        s = {sp("canisters", SamLauncher::kCanisters, "canisters"), sp("elevation min", SamLauncher::kElevMinDeg, "deg"),
             sp("elevation max", SamLauncher::kElevMaxDeg, "deg"), sp("default elevation", SamLauncher::kDefaultElevDeg, "deg"),
             sp("azimuth limit +-", SamLauncher::kAzLimitDeg, "deg"), sp("elevation rate", SamLauncher::kElevRate, "deg/s"),
             sp("elevation accel", SamLauncher::kElevAccel, "deg/s2"), sp("azimuth rate", SamLauncher::kAzRate, "deg/s"),
             sp("azimuth accel", SamLauncher::kAzAccel, "deg/s2"), sp("reload per canister", SamLauncher::kReloadSeconds, "s"),
             sp("min fire elevation", SamLauncher::kMinFireElevDeg, "deg"), sp("round life", SamLauncher::kRoundLife, "s")};
    } else if (dynamic_cast<MlrsTruck*>(c)) {
        s = {sp("tube columns", MlrsTruck::kColumns, "tubes"), sp("tube rows", MlrsTruck::kRows, "tubes"),
             sp("tubes", MlrsTruck::kTubes, "tubes"), sp("elevation min", MlrsTruck::kElevMinDeg, "deg"),
             sp("elevation max", MlrsTruck::kElevMaxDeg, "deg"), sp("azimuth limit +-", MlrsTruck::kAzLimitDeg, "deg"),
             sp("elevation rate", MlrsTruck::kElevRate, "deg/s"), sp("azimuth rate", MlrsTruck::kAzRate, "deg/s"),
             sp("ripple interval", MlrsTruck::kRippleInterval, "s"), sp("reload after last tube", MlrsTruck::kReloadSeconds, "s"),
             sp("rocket pool", MlrsTruck::kRocketPool, "rockets"), sp("rocket life", MlrsTruck::kRocketLife, "s")};
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
    std::vector<Controllable*> rawList;
    for (const auto& u : list) rawList.push_back(u.get());
    for (size_t i = 0; i < list.size(); ++i) {
        Controllable* c = list[i].get();
        SceneNode* r = c->root();
        Counts cn;
        if (r) countNode(*r, cn);
        std::vector<const SceneNode*> anim;
        if (r) collectAnimated(*r, anim);
        char key = '-';
        const int slot = slotOfName(c->name());
        if (slot >= 0 && resolveSlot(rawList, static_cast<Slot>(slot)) == static_cast<int>(i))
            key = slotInfo(static_cast<Slot>(slot)).key;
        const Vec3 p = c->position();
        o << "    {\"index\": " << i << ", \"key\": \"" << key << "\", \"name\": \"" << esc(c->name())
          << "\", \"controls\": \"" << esc(c->controls()) << "\", \"possessable\": " << (c->possessable() ? "true" : "false")
          << ", \"root_id\": \"" << (r ? esc(exportId(*r)) : "") << "\", \"root_name\": \"" << (r ? esc(r->name) : "")
          << "\", \"root_object\": \"" << (r ? esc(r->object) : "") << "\", \"parts\": " << cn.leaves
          << ", \"groups\": " << cn.groups << ", \"animated_nodes\": " << cn.animated << ", \"primitives\": " << primJson(cn)
          << ", \"position\": " << vec(p) << ", \"status\": \"" << esc(c->status())
          << "\", \"camera_chase\": " << camJson(c->camera(false)) << ", \"camera_sight\": " << camJson(c->camera(true))
          << ", \"moves\": " << (c->moves() ? "true" : "false") << ", \"dynamic_obstacles\": " << c->dynamicObstacles().size() << ",\n      \"specs\": " << specsJson(specsFor(c))
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
    struct P3 {
        float t, x, z;
    };
    std::vector<std::vector<P3>> roverTrack(static_cast<size_t>(n)), uavTrack(static_cast<size_t>(kUavCount));
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
        if (step % 8 == 0) {   // tracks every 0.27 s (report route maps)
            const std::vector<RoverState> rs = sim.rovers();
            for (int i = 0; i < n && i < static_cast<int>(rs.size()); ++i)
                if (sim.roverAlive(i)) roverTrack[static_cast<size_t>(i)].push_back({t, rs[static_cast<size_t>(i)].position.x, rs[static_cast<size_t>(i)].position.z});
            for (int u = 0; u < kUavCount; ++u) {
                const Vec3 q = transformPoint(sim.uav(u).root->world, {0, 0, 0});
                uavTrack[static_cast<size_t>(u)].push_back({t, q.x, q.z});
            }
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
    auto tracks = [&](const char* key, const std::vector<std::vector<P3>>& tr) {
        o << "],\n    \"" << key << "\": [";
        for (size_t i = 0; i < tr.size(); ++i) {
            o << (i ? ",\n      [" : "\n      [");
            for (size_t k = 0; k < tr[i].size(); ++k)
                o << (k ? ", " : "") << "[" << numShort(tr[i][k].t) << ", " << numShort(tr[i][k].x) << ", " << numShort(tr[i][k].z) << "]";
            o << "]";
        }
    };
    tracks("rover_tracks", roverTrack);
    tracks("uav_tracks", uavTrack);
    o << "]},\n";
}

// ---- v5: squad, mission, tracker, CV, armour --------------------------------------------------------------
std::string kv(const char* k, float v, const char* unit, const char* src) {
    return std::string("{\"key\": \"") + k + "\", \"value\": " + numShort(v) + ", \"unit\": \"" + unit + "\", \"source\": \"" +
           src + "\"}";
}

void writeV5(std::ostream& o, Scene& scene) {
    MultiTracker mt;   // default member values = the live tracker's parameters
    CvConfig cv;
    o << "  \"v5\": {\n    \"squad\": [\n      "
      << kv("kSquadLeadLock", kSquadLeadLock, "s", "Squad.h") << ",\n      " << kv("kSquadWingLock", kSquadWingLock, "s", "Squad.h") << ",\n      "
      << kv("kSquadSoloLock", kSquadSoloLock, "s", "Squad.h") << ",\n      " << kv("kSquadWingWait", kSquadWingWait, "s", "Squad.h") << ",\n      "
      << kv("kSquadStandOff", kSquadStandOff, "m", "Squad.h") << ",\n      " << kv("kSquadEngageAlt", kSquadEngageAlt, "m", "Squad.h") << ",\n      "
      << kv("kSquadPatrolAlt", kSquadPatrolAlt, "m", "Squad.h") << ",\n      " << kv("kSquadReacquireTime", kSquadReacquireTime, "s", "Squad.h") << ",\n      "
      << kv("Squad::kCount", static_cast<float>(Squad::kCount), "UAVs", "Squad.h") << "\n    ],\n";
    o << "    \"flight_time\": [";
    const float ds[] = {2, 4, 6, 8, 10, 12, 14, 16, 20, 25, 30, 40};
    for (size_t i = 0; i < sizeof(ds) / sizeof(ds[0]); ++i)
        o << (i ? ", " : "") << "{\"d\": " << ds[i] << ", \"t\": " << numShort(roundFlightTime(ds[i])) << "}";
    o << "],\n    \"flight_model\": {\"launch_speed\": 8, \"accel\": 130, \"ramp_time\": 0.4, \"cruise_speed\": 60, \"fuse_m\": 0.5, \"source\": \"Squad.cpp roundFlightTime comment\"},\n";
    o << "    \"mission\": [\n      "
      << kv("kKillScore", kKillScore, "points", "Mission.h") << ",\n      " << kv("kCombinedBonus", kCombinedBonus, "points", "Mission.h") << ",\n      "
      << kv("kBreachDamage", kBreachDamage, "% integrity", "Mission.h") << ",\n      " << kv("kSpawnInterval", kSpawnInterval, "s", "Mission.h") << ",\n      "
      << kv("kWaveBreak", kWaveBreak, "s", "Mission.h") << ",\n      " << kv("kFirstWaveDelay", kFirstWaveDelay, "s", "Mission.h") << ",\n      "
      << kv("kMaxRovers", static_cast<float>(layout::kMaxRovers), "slots", "BaseLayout.h") << ",\n      "
      << kv("kRoverCount", static_cast<float>(layout::kRoverCount), "rovers", "BaseLayout.h") << ",\n      "
      << kv("kSpawnRadius", layout::kSpawnRadius, "m", "BaseLayout.h") << ",\n      " << kv("kBreachRadius", layout::kBreachRadius, "m", "BaseLayout.h") << "\n    ],\n";
    o << "    \"objectives\": [";
    const auto objs = Mission::objectives();
    for (size_t i = 0; i < objs.size(); ++i) o << (i ? ", " : "") << vec(objs[i]);
    o << "],\n    \"tracker\": [\n      "
      << kv("gateChi2", mt.gateChi2, "chi2 (2 dof, 99 %)", "MultiTracker.h") << ",\n      " << kv("accelNoise", mt.accelNoise, "m/s2", "MultiTracker.h") << ",\n      "
      << kv("confirmHits", static_cast<float>(mt.confirmHits), "hits", "MultiTracker.h") << ",\n      " << kv("coastAfter", mt.coastAfter, "s", "MultiTracker.h") << ",\n      "
      << kv("deleteAfter", mt.deleteAfter, "s", "MultiTracker.h") << ",\n      " << kv("mergeDistance", mt.mergeDistance, "m", "MultiTracker.h") << "\n    ],\n";
    o << "    \"cv\": [\n      "
      << kv("floor", cv.floor, "8-bit level (0.80)", "CvPipeline.h") << ",\n      " << kv("core", cv.core, "8-bit level (0.90)", "CvPipeline.h") << ",\n      "
      << kv("minArea", static_cast<float>(cv.minArea), "px", "CvPipeline.h") << ",\n      " << kv("maxArea", static_cast<float>(cv.maxArea), "px", "CvPipeline.h") << ",\n      "
      << kv("maxElongation", cv.maxElongation, "major/minor", "CvPipeline.h") << ",\n      " << kv("despeckle", cv.despeckle ? 1.0f : 0.0f, "bool", "CvPipeline.h") << ",\n      "
      << kv("kCvWidth", static_cast<float>(kCvWidth), "px", "DroneController.h") << ",\n      " << kv("kCvHeight", static_cast<float>(kCvHeight), "px", "DroneController.h") << ",\n      "
      << kv("kCvFovYDeg", kCvFovYDeg, "deg", "DroneController.h") << "\n    ],\n";
    o << "    \"armour\": [\n      " << kv("kRoverHealth", kRoverHealth, "HP", "Scene.h") << ",\n      " << kv("kRoundDamage", kRoundDamage, "HP at centre of a power-1 blast (half at the edge)", "Scene.h")
      << ",\n      " << kv("kUavReloadTime", kUavReloadTime, "s", "Scene.h") << ",\n      " << kv("kUavCount", static_cast<float>(kUavCount), "UAVs", "Scene.h") << "\n    ],\n";
    o << "    \"uavs\": [";
    for (int i = 0; i < kUavCount; ++i) {
        const DroneState& d = scene.droneState(i);
        Counts cn;
        countNode(*scene.uav(i).root, cn);
        o << (i ? ", " : "") << "{\"index\": " << i << ", \"position\": " << vec(d.position) << ", \"yaw_deg\": " << d.yawDeg
          << ", \"role\": \"" << squadRoleName(scene.squad().roles[i]) << "\", \"parts\": " << cn.leaves << ", \"owner_id\": "
          << (i == 0 ? kDroneOwner : 10) << ", \"rounds_ready\": " << scene.roundsReady(i) << "}";
    }
    o << "],\n    \"intruder_slots\": " << scene.intruders().size() << ",\n    \"controllables_total\": " << scene.controllables().size()
      << ",\n    \"mission_initial\": {\"wave\": " << scene.mission().state().wave << ", \"integrity\": " << scene.mission().state().integrity << "}\n  },\n";
}

// ---- v6: day cycle grid, garrison, gait, sensor model, random routes, seeds (keys: docs/v6_notes/wp-e.md) ----------
const char* eventTypeName(GameEventType t) {
    switch (t) {
        case GameEventType::Dusk: return "Dusk";
        case GameEventType::NightFalls: return "NightFalls";
        case GameEventType::Dawn: return "Dawn";
        case GameEventType::DayBreak: return "DayBreak";
        case GameEventType::ShiftChange: return "ShiftChange";
        case GameEventType::Withdraw: return "Withdraw";
        default: return "other";
    }
}

float luma(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

void writeDayCycle(std::ostream& o) {
    const DayCycleConfig cfg;
    o << "    \"day_cycle\": {\n      \"config\": {\"latitude_deg\": " << numShort(cfg.latitudeDeg) << ", \"declination_deg\": "
      << numShort(cfg.declinationDeg) << ", \"day_sec_per_hour\": " << numShort(cfg.daySecPerHour) << ", \"night_sec_per_hour\": "
      << numShort(cfg.nightSecPerHour) << ", \"twilight_slow\": " << numShort(cfg.twilightSlow) << ", \"scrub_hours_per_sec\": "
      << numShort(cfg.scrubHoursPerSec) << ", \"shadow_quant_deg\": " << numShort(cfg.shadowQuantDeg) << ", \"cycle_seconds\": "
      << numShort(DayCycle::cycleSeconds(cfg)) << ", \"night_below_deg\": " << numShort(kNightBelowDeg) << ", \"day_above_deg\": "
      << numShort(kDayAboveDeg) << ", \"beams_below_deg\": " << numShort(kBeamsBelowDeg) << ", \"sun_intensity\": "
      << numShort(kSunIntensity) << ", \"day_exposure_key_log2\": " << numShort(kDayExposureKeyLog2) << ", \"solar_gain\": "
      << numShort(kSolarGain) << ", \"cloud_altitude_m\": " << numShort(kCloudAltitude) << ", \"cloud_coverage\": "
      << numShort(kCloudCoverage) << ", \"sunrise\": " << numShort(DayCycle::hourAtElevation(0.0f, true, cfg)) << ", \"sunset\": "
      << numShort(DayCycle::hourAtElevation(0.0f, false, cfg)) << "},\n";
    // Phase crossings of one manual sweep through the day.
    o << "      \"events\": [";
    {
        DayCycle c(0.0f, false, cfg);
        std::vector<GameEvent> ev;
        bool first = true;
        for (int k = 1; k <= 24 * 60; ++k) {
            ev.clear();
            c.setHour(static_cast<float>(k) / 60.0f);
            c.update(0.0f, ev, 0.0f);
            for (const GameEvent& e : ev) {
                if (std::string(eventTypeName(e.type)) == "other") continue;
                o << (first ? "" : ", ") << "{\"hour\": " << numShort(c.hour()) << ", \"event\": \"" << eventTypeName(e.type) << "\"}";
                first = false;
            }
        }
    }
    o << "],\n      \"grid\": [\n";
    for (int i = 0; i <= 96; ++i) {
        const float hour = 0.25f * static_cast<float>(i);
        DayCycle c(std::fmod(hour, 24.0f), false, cfg);
        LightRig L;
        c.applyTo(L);
        const Atmosphere& a = L.atmo;
        // Mean cloud shadow over the camp (9 x 9 points, +-30 m) at t = 0; 1 when the sun is down.
        float shadow = 0.0f;
        for (int gz = 0; gz < 9; ++gz)
            for (int gx = 0; gx < 9; ++gx)
                shadow += c.keyIsSun() ? cloudShadow({-30.0f + 7.5f * gx, 0.0f, -30.0f + 7.5f * gz}, c.towardSun(), 0.0f) : 1.0f;
        shadow /= 81.0f;
        o << "        {\"hour\": " << numShort(hour) << ", \"clock\": \"" << formatClock(hour) << "\", \"phase\": \"" << c.phaseName()
          << "\", \"elevation_deg\": " << numShort(c.sunElevationDeg()) << ", \"azimuth_deg\": " << numShort(c.sunAzimuthDeg())
          << ", \"daylight\": " << numShort(c.daylight()) << ", \"key_is_sun\": " << (c.keyIsSun() ? "true" : "false")
          << ", \"key_intensity\": " << numShort(L.moon.intensity) << ", \"key_color\": " << vec(L.moon.color)
          << ", \"key_dir\": " << vec(L.moon.direction) << ", \"ambient\": " << vec(L.ambient) << ", \"ambient_luma\": "
          << numShort(luma(L.ambient)) << ", \"sky_zenith\": " << vec(a.skyZenith) << ", \"fog_color\": " << vec(a.fogColor)
          << ", \"fog_density\": " << numShort(a.fogDensity) << ", \"vol_fog_density\": " << numShort(a.volFogDensity)
          << ", \"exposure_key_log2\": " << numShort(a.exposureKeyLog2) << ", \"purkinje\": " << numShort(a.purkinje)
          << ", \"solar\": " << numShort(a.solar) << ", \"practical_scale\": " << numShort(a.practicalScale)
          << ", \"beams_on\": " << (c.beamsOn() ? "true" : "false") << ", \"waves_allowed\": " << (c.wavesAllowed() ? "true" : "false")
          << ", \"cloud_shadow_mean\": " << numShort(shadow) << "}" << (i < 96 ? ",\n" : "\n");
    }
    o << "      ]\n    },\n";
}

std::string gaitJson(const GaitParams& g) {
    return "{\"speed\": " + numShort(g.speed) + ", \"stride\": " + numShort(g.stride) + ", \"duty\": " + numShort(g.duty) +
           ", \"lift\": " + numShort(g.lift) + ", \"bob\": " + numShort(g.bob) + ", \"arm_swing_deg\": " + numShort(g.armSwingDeg) +
           ", \"thigh\": " + numShort(g.thigh) + ", \"shin\": " + numShort(g.shin) + ", \"hip_height\": " + numShort(g.hipHeight) +
           ", \"cadence_steps_per_min\": " + numShort(g.stride > 0.0f ? 120.0f * g.speed / g.stride : 0.0f) + "}";
}

void writeGarrison(std::ostream& o, Scene& scene) {
    const Garrison* g = scene.garrison();
    const auto& list = scene.controllables();
    o << "    \"garrison\": {\n      \"count\": " << (g ? g->count() : 0) << ", \"route_lengths_m\": [";
    if (g) {
        const std::vector<float> rl = g->routeLengths();
        for (size_t i = 0; i < rl.size(); ++i) o << (i ? ", " : "") << numShort(rl[i]);
    }
    o << "],\n      \"routes\": [";
    if (g) {
        const std::vector<CatmullRomPath>& routes = g->routes();
        for (size_t i = 0; i < routes.size(); ++i) {
            const CatmullRomPath& r = routes[i];
            float maxK = 0.0f;
            for (float d = 0.0f; d < r.length(); d += 0.25f) maxK = std::max(maxK, r.curvatureAtDistance(d));
            o << (i ? ", " : "") << "{\"index\": " << i << ", \"length_m\": " << numShort(r.length()) << ", \"closed\": "
              << (r.closed() ? "true" : "false") << ", \"control_points\": " << r.points().size() << ", \"segments\": " << r.segments()
              << ", \"max_curvature\": " << numShort(maxK) << ", \"min_clearance_m\": " << numShort(g->planner().minClearance(r)) << "}";
        }
    }
    o << "],\n      \"plan\": " << (g ? g->planJson() : std::string("null"));
    o << ",\n      \"soldiers\": [";
    for (int i = 0; g && i < g->count(); ++i) {
        const Controllable* c = g->soldier(i);
        if (!c) continue;
        int index = -1;
        for (size_t k = 0; k < list.size(); ++k)
            if (list[k].get() == c) index = static_cast<int>(k);
        Counts cn;
        if (SceneNode* r = const_cast<Controllable*>(c)->root()) countNode(*r, cn);
        o << (i ? ",\n        " : "\n        ") << "{\"index\": " << i << ", \"name\": \"" << esc(c->name()) << "\", \"controllable_index\": "
          << index << ", \"roster_day\": \"" << esc(g->roster(i, true)) << "\", \"roster_night\": \"" << esc(g->roster(i, false))
          << "\", \"position\": " << vec(c->position()) << ", \"parts\": " << cn.leaves << ", \"groups\": " << cn.groups
          << ", \"primitives\": " << primJson(cn) << ", \"friendly_radius\": " << numShort(c->friendlyRadius()) << ", \"status\": \""
          << esc(c->status()) << "\"}";
    }
    o << "],\n      \"joints\": [";
    if (g && g->soldier(0))
        if (SceneNode* r = const_cast<Controllable*>(g->soldier(0))->root()) {
            bool first = true;
            std::function<void(const SceneNode&, int)> walk = [&](const SceneNode& n, int depth) {
                o << (first ? "\n        " : ",\n        ") << "{\"id\": \"" << esc(n.id) << "\", \"name\": \"" << esc(n.name) << "\", \"parent\": \""
                  << (n.parent && depth > 0 ? esc(n.parent->name) : std::string("World")) << "\", \"depth\": " << depth << ", \"primitive\": \""
                  << (n.isLeaf() ? primitiveName(n.primitive) : "group") << "\", \"animation\": \"" << esc(n.animation) << "\"}";
                first = false;
                for (const auto& ch : n.children) walk(*ch, depth + 1);
            };
            walk(*r, 0);
        }
    const GaitParams walkP = GaitParams::walk(), runP = GaitParams::run();
    o << "],\n      \"gait\": {\"walk\": " << gaitJson(walkP) << ", \"run\": " << gaitJson(runP) << ",\n        \"cycle\": [";
    for (int k = 0; k <= 32; ++k) {
        const float ph = static_cast<float>(k) / 32.0f;
        const Pose p = gaitPose(walkP, ph);
        float fx = 0.0f, fy = 0.0f;
        footInPelvis(walkP, ph, 0, fx, fy);
        o << (k ? ",\n          " : "\n          ") << "{\"phase\": " << numShort(ph) << ", \"hip_l\": " << numShort(p.hip[0]) << ", \"knee_l\": "
          << numShort(p.knee[0]) << ", \"ankle_l\": " << numShort(p.ankle[0]) << ", \"hip_r\": " << numShort(p.hip[1]) << ", \"knee_r\": "
          << numShort(p.knee[1]) << ", \"shoulder_l\": " << numShort(p.shoulder[0]) << ", \"shoulder_r\": " << numShort(p.shoulder[1])
          << ", \"elbow_l\": " << numShort(p.elbow[0]) << ", \"spine_yaw\": " << numShort(p.spineYaw) << ", \"pelvis_y\": "
          << numShort(p.pelvisY) << ", \"foot_x_l\": " << numShort(fx) << ", \"foot_y_l\": " << numShort(fy) << "}";
    }
    o << "]}\n    },\n";
}

void writeSensor(std::ostream& o) {
    const SensorNoiseConfig n;
    const CvConfig cv;
    o << "    \"sensor\": [\n      " << kv("netdDn", n.netdDn, "DN (8-bit)", "SensorModel.h") << ",\n      "
      << kv("fpnGainSigma", n.fpnGainSigma, "gain", "SensorModel.h") << ",\n      " << kv("fpnOffsetSigma", n.fpnOffsetSigma, "DN", "SensorModel.h")
      << ",\n      " << kv("columnSigma", n.columnSigma, "DN", "SensorModel.h") << ",\n      " << kv("psf", n.psf ? 1.0f : 0.0f, "bool (3x3 binomial)", "SensorModel.h")
      << ",\n      " << kv("jitterDeg", n.jitterDeg, "deg (1 sigma)", "SensorModel.h") << ",\n      " << kv("dropProb", n.dropProb, "per frame", "SensorModel.h")
      << ",\n      " << kv("stuckFrac", n.stuckFrac, "of pixels", "SensorModel.h") << ",\n      " << kv("nucResidual", n.nucResidual, "of the fixed pattern", "SensorModel.h")
      << ",\n      " << kv("cvFloor", cv.floor, "8-bit level", "CvPipeline.h") << ",\n      " << kv("cvCore", cv.core, "8-bit level", "CvPipeline.h")
      << "\n    ],\n    \"sensor_sweep\": \"build/figdata_out/cv_noise.json\",\n";
}

void writeRoutesAndSeeds(std::ostream& o) {
    o << "    \"random_routes\": [\n      " << kv("kSpawnJitter", kSpawnJitter, "m", "Mission.h") << ",\n      "
      << kv("kNorthGapDeg", kNorthGapDeg, "deg", "Mission.h") << ",\n      " << kv("kViaSectorDeg", kViaSectorDeg, "deg", "Mission.h")
      << ",\n      " << kv("kViaMinR", kViaMinR, "m", "Mission.h") << ",\n      " << kv("kViaMaxR", kViaMaxR, "m", "Mission.h")
      << ",\n      " << kv("kMaxVia", static_cast<float>(kMaxVia), "via points", "Mission.h") << ",\n      "
      << kv("kSwitchProbability", kSwitchProbability, "probability", "Mission.h") << ",\n      "
      << kv("kSwitchMinS", kSwitchMinS, "s", "Mission.h") << ",\n      " << kv("kSwitchMaxS", kSwitchMaxS, "s", "Mission.h") << "\n    ],\n";
    // Example run seed 42: the derived streams (1 mission, 2 combat, 3 squad, 4 garrison, 101 / 102 UAV sensors) and the
    // squad's randomised patrols.
    const uint32_t base = 42u;
    const std::pair<const char*, uint32_t> streams[] = {{"mission", 1u}, {"combat", 2u}, {"squad", 3u}, {"garrison", 4u},
                                                        {"sensor UAV-1", 100u}, {"sensor UAV-2", 101u}};
    o << "    \"seeds\": {\"example_base\": " << base << ", \"streams\": [";
    for (size_t i = 0; i < sizeof(streams) / sizeof(streams[0]); ++i)
        o << (i ? ", " : "") << "{\"name\": \"" << streams[i].first << "\", \"stream\": " << streams[i].second << ", \"seed\": "
          << deriveSeed(base, streams[i].second) << "}";
    o << "], \"patrols\": [";
    Scene seeded(SceneOptions::v6(base, 23.0f, false));
    for (int i = 0; i < kUavCount; ++i) {
        const PatrolParams& p = seeded.squad().patrolParams(i);
        o << (i ? ", " : "") << "{\"uav\": " << i << ", \"base_radius\": " << numShort(p.baseRadius) << ", \"amplitude\": "
          << numShort(p.amplitude) << ", \"breathing\": " << numShort(p.breathing) << ", \"angular_speed\": " << numShort(p.angularSpeed) << "}";
    }
    o << "]}\n";
}

// Key table of the v6 controls (report appendix B) and the F1 help lines, from the same data the program draws.
void writeKeys(std::ostream& o) {
    o << "    \"keys\": [";
    const std::vector<KeyBinding>& k = keyBindingsV6();
    for (size_t i = 0; i < k.size(); ++i)
        o << (i ? ", " : "") << "{\"keys\": \"" << esc(k[i].keys) << "\", \"action\": \"" << esc(k[i].action) << "\", \"version\": \""
          << k[i].version << "\"}";
    o << "],\n    \"help_lines\": [";
    const std::vector<HelpLine>& h = helpLines();
    for (size_t i = 0; i < h.size(); ++i) o << (i ? ", " : "") << "{\"column\": " << h[i].column << ", \"text\": \"" << esc(h[i].text) << "\"}";
    o << "],\n";
}

// ---- v7 (WP-W workbook sheets 24-27): ray tracing, Frame Explorer, game camera / POV sources -------------------------------
// Everything below is read from the same code the program runs (no GL: CPU ray-tracing core, ExplorerMath, Pov, CameraRig).
const char* lightTypeName(rt::LightType t) {
    switch (t) {
        case rt::LightType::Directional: return "Directional";
        case rt::LightType::Point: return "Point";
        case rt::LightType::Spot: return "Spot";
        case rt::LightType::Practical: return "Practical";
    }
    return "?";
}

std::string boxJsonRt(const rt::Aabb& b) { return "{\"min\": " + vecFull(b.lo) + ", \"max\": " + vecFull(b.hi) + "}"; }

// Primitives under a BVH node (sum of its leaves' counts).
int bvhPrims(const rt::Bvh& b, int i) {
    const rt::BvhNode& n = b.nodes()[i];
    return n.leaf() ? n.count : bvhPrims(b, n.leftFirst) + bvhPrims(b, n.leftFirst + 1);
}

void writeRayTrace(std::ostream& o, const Scene& scene) {
    o << "    \"raytrace\": {\n      \"modes\": [";
    for (int i = 0; i < rt::kRtModeCount; ++i) {
        const rt::RtMode m = static_cast<rt::RtMode>(i);
        o << (i ? ", " : "") << "{\"index\": " << i << ", \"name\": \"" << rt::rtModeName(m) << "\", \"caption\": \"" << esc(rt::rtModeCaption(m))
          << "\"}";
    }
    const rt::TraceSettings ts;
    o << "],\n      \"trace_settings\": {\"whitted_depth\": " << ts.whittedDepth << ", \"max_bounces\": " << ts.maxBounces
      << ", \"rr_start\": " << ts.rrStart << ", \"soft_shadows\": " << (ts.softShadows ? "true" : "false") << ", \"emit_radius_m\": "
      << numShort(ts.emitRadius) << "},\n";
    o << "      \"bvh_constants\": {\"traversal_cost\": " << numShort(rt::Bvh::kTraversalCost) << ", \"intersect_cost\": "
      << numShort(rt::Bvh::kIntersectCost) << ", \"bins\": " << rt::Bvh::kBins << ", \"max_leaf\": " << rt::Bvh::kMaxLeaf
      << ", \"rebuild_ratio\": " << numShort(rt::SceneBridge::kRebuildRatio) << ", \"heightfield_samples\": "
      << rt::SceneBridge::kHeightfieldSamples << "},\n";
    // The camp at t = 0 as the tracers see it: SAH build vs a median-split build over the same bounds.
    rt::SceneBridge bridge;
    rt::RtScene rs;
    bridge.sync(scene, rt::LightOptions{}, rs);
    const rt::Bvh& b = rs.bvh();
    std::vector<rt::Aabb> bounds;
    for (const rt::RtPrim& p : rs.prims) bounds.push_back(rt::shapeBounds(p.shape, p.world));
    rt::Bvh median;
    median.buildMedian(bounds);
    std::map<std::string, int> shapes;
    int dyn = 0, foliage = 0;
    for (const rt::RtPrim& p : rs.prims) {
        ++shapes[rt::shapeName(p.shape)];
        dyn += (p.flags & rt::kDynamic) ? 1 : 0;
        foliage += (p.flags & rt::kFoliage) ? 1 : 0;
    }
    o << "      \"bvh\": {\"prims\": " << rs.prims.size() << ", \"dynamic_prims\": " << dyn << ", \"foliage_prims\": " << foliage
      << ", \"shapes\": {";
    bool first = true;
    for (const auto& s : shapes) {
        o << (first ? "" : ", ") << "\"" << s.first << "\": " << s.second;
        first = false;
    }
    o << "}, \"nodes\": " << b.nodes().size() << ", \"leaves\": " << b.leafCount() << ", \"depth\": " << b.depth() << ", \"sah_cost\": "
      << numShort(b.sahCost()) << ",\n        \"median\": {\"nodes\": " << median.nodes().size() << ", \"leaves\": " << median.leafCount()
      << ", \"depth\": " << median.depth() << ", \"sah_cost\": " << numShort(median.sahCost()) << "}";
    if (!b.empty() && !b.nodes()[0].leaf()) {
        const int l = b.nodes()[0].leftFirst;
        o << ",\n        \"root_split\": {\"root\": " << boxJsonRt(b.nodes()[0].box) << ", \"n\": " << bvhPrims(b, 0) << ",\n          \"left\": "
          << boxJsonRt(b.nodes()[l].box) << ", \"n_left\": " << bvhPrims(b, l) << ",\n          \"right\": " << boxJsonRt(b.nodes()[l + 1].box)
          << ", \"n_right\": " << bvhPrims(b, l + 1) << "}";
    }
    // Probe: a 64 x 36 grid of primary rays from the Frame Explorer's default view, BVH vs brute force.
    const RigPose cam = RigPose::look({-15.0f, 5.2f, 21.0f}, {1.0f, 1.2f, 2.0f}, 52.0f);
    rt::RtCamera rc;
    rc.position = cam.eye;
    rc.forward = cam.forward();
    rc.right = cam.right();
    rc.up = cam.up();
    rc.tanHalfY = std::tan(radians(0.5f * cam.fovDeg));
    rc.aspect = 16.0f / 9.0f;
    rt::TraceStats st;
    int hits = 0, agree = 0, rays = 0;
    for (int y = 0; y < 36; ++y)
        for (int x = 0; x < 64; ++x) {
            const rt::Ray r = rc.primary((x + 0.5f) / 64.0f, (y + 0.5f) / 36.0f);
            rt::Hit h, hb;
            const bool a = rs.intersect(r, h, &st), c = rs.intersectBrute(r, hb);
            ++rays;
            hits += a ? 1 : 0;
            agree += (a == c && (!a || std::fabs(h.t - hb.t) < 1e-3f)) ? 1 : 0;
        }
    o << ",\n        \"probe\": {\"camera_eye\": " << vec(cam.eye) << ", \"camera_target\": [1, 1.2, 2], \"fov_deg\": " << numShort(cam.fovDeg)
      << ", \"grid\": [64, 36], \"rays\": " << rays << ", \"hits\": " << hits << ", \"agree_with_brute_force\": " << agree
      << ", \"node_visits\": " << st.nodeVisits << ", \"prim_tests\": " << st.primTests << "}},\n";
    // Lights of the night scene as the tracers see them + the light tracer's emitter power (last entry = sky).
    o << "      \"lights\": [";
    for (size_t i = 0; i < rs.lights.size(); ++i) {
        const rt::RtLight& l = rs.lights[i];
        o << (i ? ",\n        " : "\n        ") << "{\"type\": \"" << lightTypeName(l.type) << "\", \"color\": " << vecFull(l.color)
          << ", \"position\": " << vec(l.position) << ", \"direction\": " << vec(l.direction) << ", \"linear\": " << numShort(l.linear)
          << ", \"quadratic\": " << numShort(l.quadratic) << ", \"cos_inner\": " << numShort(l.cosInner) << ", \"cos_outer\": "
          << numShort(l.cosOuter) << ", \"range\": " << numShort(l.range) << ", \"radius\": " << numShort(l.radius) << "}";
    }
    const rt::Tracer tracer(rs);
    const std::vector<float> power = tracer.emitterPower();
    o << "],\n      \"env\": {\"zenith\": " << vecFull(rs.env.zenith) << ", \"bounce\": " << vecFull(rs.env.bounce) << "},\n      \"emitter_power\": [";
    for (size_t i = 0; i < power.size(); ++i) o << (i ? ", " : "") << num(power[i]);
    o << "]\n    },\n";
}

void writeExplorer(std::ostream& o) {
    o << "    \"explorer\": {\n      \"stages\": [";
    for (int i = 0; i < fx::kStageCount; ++i) {
        const fx::StageInfo& s = fx::stageInfo(i);
        o << (i ? ",\n        " : "\n        ") << "{\"index\": " << i + 1 << ", \"short\": \"" << esc(s.shortName) << "\", \"title\": \""
          << esc(s.title) << "\", \"caption\": \"" << esc(s.caption) << "\", \"formula\": \"" << esc(s.formula) << "\", \"concepts\": \""
          << esc(s.concepts) << "\"}";
    }
    // Worked rasterisation example: one screen triangle, one pixel, affine vs perspective-correct interpolation.
    const fx::Vec2f v0{12.0f, 4.0f}, v1{56.0f, 18.0f}, v2{20.0f, 50.0f}, p{30.5f, 22.5f};
    const float w[3] = {1.5f, 6.0f, 2.5f}, a[3] = {0.0f, 1.0f, 0.5f};
    const fx::Barycentric bc = fx::rasterize(v0, v1, v2, p);
    o << "\n      ],\n      \"raster_example\": {\"v0\": [" << num(v0.x) << ", " << num(v0.y) << "], \"v1\": [" << num(v1.x) << ", " << num(v1.y)
      << "], \"v2\": [" << num(v2.x) << ", " << num(v2.y) << "], \"p\": [" << num(p.x) << ", " << num(p.y) << "], \"w\": [" << num(w[0]) << ", "
      << num(w[1]) << ", " << num(w[2]) << "], \"attr\": [" << num(a[0]) << ", " << num(a[1]) << ", " << num(a[2]) << "],\n        \"e\": ["
      << num(bc.e0) << ", " << num(bc.e1) << ", " << num(bc.e2) << "], \"bary\": [" << num(bc.w0) << ", " << num(bc.w1) << ", " << num(bc.w2)
      << "], \"inside\": " << (bc.inside ? "true" : "false") << ", \"area2\": " << num(fx::signedArea2(v0, v1, v2)) << ", \"affine\": "
      << num(fx::interpolateAffine(bc, a[0], a[1], a[2])) << ", \"perspective\": "
      << num(fx::interpolatePerspective(bc, w[0], w[1], w[2], a[0], a[1], a[2])) << "},\n      \"kernels\": [";
    for (int i = 0; i < fx::kKernelCount; ++i) {
        const fx::Kernel k = fx::kernel(static_cast<fx::KernelType>(i));
        o << (i ? ",\n        " : "\n        ") << "{\"name\": \"" << esc(k.name) << "\", \"note\": \"" << esc(k.note) << "\", \"size\": " << k.size
          << ", \"bias\": " << num(k.bias) << ", \"magnitude\": " << (k.magnitude ? "true" : "false") << ", \"sum\": " << num(fx::kernelSum(k))
          << ", \"w\": [";
        for (int j = 0; j < k.size * k.size; ++j) o << (j ? ", " : "") << num(k.w[j]);
        o << "]}";
    }
    o << "\n      ],\n      \"halton\": [";
    for (unsigned k = 1; k <= 8; ++k) o << (k > 1 ? ", " : "") << "[" << k << ", " << num(fx::halton(k, 2)) << ", " << num(fx::halton(k, 3)) << "]";
    o << "],\n      \"tone_curve\": [";
    for (int i = 0; i <= 24; ++i) {
        const float x = std::pow(2.0f, -8.0f + 0.5f * i);
        o << (i ? ", " : "") << "[" << num(x) << ", " << num(fx::acesFitted(x)) << ", " << num(fx::toneCurve(x, 1.0f)) << "]";
    }
    o << "]\n    },\n";
}

void writePov(std::ostream& o, const Scene& scene) {
    const Vec3 viewer{-15.0f, 5.2f, 21.0f};
    const std::vector<PovSource> list = listPovSources(scene, viewer);
    o << "    \"pov_viewer\": " << vec(viewer) << ",\n    \"pov_sources\": [";
    for (size_t i = 0; i < list.size(); ++i) {
        const PovSource& s = list[i];
        RigPose first, chase;
        const bool okF = povPose(scene, s, PovView::First, first), okC = povPose(scene, s, PovView::Chase, chase);
        o << (i ? ",\n      " : "\n      ") << "{\"order\": " << i + 1 << ", \"kind\": \"" << povKindName(s.kind) << "\", \"tag\": \""
          << povKindTag(s.kind) << "\", \"name\": \"" << esc(s.name) << "\", \"controllable\": " << s.controllable << ", \"can_control\": "
          << (s.canControl ? "true" : "false") << ", \"has_alt\": " << (s.hasAlt ? "true" : "false") << ", \"hostile\": "
          << (povHostile(s.kind) ? "true" : "false") << ", \"position\": " << vec(s.position) << ", \"distance\": " << numShort(s.distance)
          << ", \"pick_radius\": " << numShort(s.radius) << ", \"first_eye\": " << (okF ? vec(first.eye) : std::string("null"))
          << ", \"first_fov\": " << (okF ? numShort(first.fovDeg) : std::string("null")) << ", \"chase_eye\": "
          << (okC ? vec(chase.eye) : std::string("null")) << "}";
    }
    o << "],\n    \"camera_rig\": {\"blend_time_s\": " << numShort(CameraRig::kBlendTime) << ", \"omega\": " << numShort(CameraRig::kOmega)
      << ", \"arc_per_metre\": " << numShort(CameraRig::kArcPerMetre) << ", \"arc_max_m\": " << numShort(CameraRig::kArcMax)
      << ", \"fly_speed\": " << numShort(FlyCam{}.speed) << ", \"fly_min_speed\": " << numShort(FlyCam::kMinSpeed) << ", \"fly_max_speed\": "
      << numShort(FlyCam::kMaxSpeed) << ", \"fly_boost\": " << numShort(FlyCam::kBoost) << ", \"fly_tau_s\": " << numShort(FlyCam::kTau)
      << ",\n      \"curves\": [";
    for (int i = 0; i <= 20; ++i) {
        const float t = 0.03f * i;   // 0 .. 0.6 s (the blend)
        o << (i ? ", " : "") << "{\"t\": " << num(t) << ", \"spring\": " << num(springDecay(CameraRig::kOmega, t)) << ", \"letterbox\": "
          << num(jumpLetterbox(t / CameraRig::kBlendTime)) << ", \"hit_marker\": " << num(hitMarkerAlpha(t)) << "}";
    }
    o << "]}\n";
}

// v7: the key table (WP-G, HudMath keyBindingsV7) and the WP-W documentation data above.
void writeV7(std::ostream& o, const Scene& scene) {
    o << "  \"v7\": {\n    \"keys\": [";
    const std::vector<KeyBinding>& k = keyBindingsV7();
    for (size_t i = 0; i < k.size(); ++i)
        o << (i ? ", " : "") << "{\"keys\": \"" << esc(k[i].keys) << "\", \"action\": \"" << esc(k[i].action) << "\", \"version\": \""
          << k[i].version << "\"}";
    o << "],\n";
    writeRayTrace(o, scene);
    writeExplorer(o);
    writePov(o, scene);
    o << "  },\n";
}

void writeV6(std::ostream& o, Scene& scene) {
    o << "  \"v6\": {\n";
    writeKeys(o);
    writeDayCycle(o);
    writeGarrison(o, scene);
    writeSensor(o);
    writeRoutesAndSeeds(o);
    o << "  },\n";
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
    // v10 M3: rigs per object tag (separate roots), families of numbered variants ("Soldier 1".."Soldier 8") and the
    // build stages of one rig (first parts in hierarchy order).
    std::map<std::string, std::vector<const SceneNode*>> rigs;
    std::function<void(const SceneNode&)> findRigs = [&](const SceneNode& n) {
        if (rigRoot(n) == &n) rigs[n.object].push_back(&n);
        for (const auto& ch : n.children) findRigs(*ch);
    };
    for (SceneNode* r : scene.roots()) findRigs(*r);
    std::map<std::string, int> familyRigs;
    for (const std::string& ob : order) familyRigs[objectFamily(ob)] += static_cast<int>(rigs[ob].size());
    o << "  \"objects\": [\n";
    for (size_t i = 0; i < order.size(); ++i) {
        const Counts& c = perObj[order[i]];
        const std::vector<const SceneNode*>& rr = rigs[order[i]];
        const int rigParts = rr.empty() ? 0 : static_cast<int>(rigLeavesDfs(*rr.front()).size());
        o << "    {\"object\": \"" << esc(order[i]) << "\", \"parts\": " << c.leaves << ", \"groups\": " << c.groups
          << ", \"animated_nodes\": " << c.animated << ", \"primitives\": " << primJson(c) << ", \"rigs\": " << rr.size()
          << ", \"rig_parts\": " << rigParts << ", \"variant_of\": \"" << esc(objectFamily(order[i])) << "\", \"instances\": "
          << familyRigs[objectFamily(order[i])] << ", \"build_stages\": [";
        for (int k = 0; k < 4; ++k)
            o << (k ? ", " : "") << "{\"pct\": " << kBuildStagePct[k] << ", \"parts\": " << buildStageCount(rigParts, kBuildStagePct[k]) << "}";
        o << "]}" << (i + 1 < order.size() ? ",\n" : "\n");
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

// v10 final pass: every object design (one builder / prefab) with its instances, parts per instance, primitives and
// nested designs (src/report/Designs.h). Instances: root id, placement of the root (world translation, heading = yaw of
// the root's +Z axis, uniform scale of the root frame) and the parts the instance owns.
void writeDesigns(std::ostream& o) {
    const designs::Catalogue& c = g_designs;
    o << "  \"designs\": [\n";
    for (size_t d = 0; d < c.designs.size(); ++d) {
        const designs::Design& ds = c.designs[d];
        std::map<std::string, int> total, first;
        std::map<std::string, int> nested;
        int parts = 0, hidden = 0;
        std::string per;
        for (size_t k = 0; k < ds.instances.size(); ++k) {
            const designs::Instance& in = c.instances[static_cast<size_t>(ds.instances[k])];
            for (const SceneNode* l : in.own) {
                ++total[primKey(*l)];
                if (k == 0) {
                    ++first[primKey(*l)];
                    hidden += hiddenAtRest(*l);
                }
            }
            parts += static_cast<int>(in.own.size());
            per += (k ? ", " : "") + std::to_string(in.own.size());
            if (k == 0)
                for (int nid : in.nested) ++nested[c.designs[static_cast<size_t>(c.instances[static_cast<size_t>(nid)].design)].name];
        }
        auto prims = [](const std::map<std::string, int>& m) {
            std::string s = "{";
            bool f = true;
            for (const auto& kv : m) {
                s += (f ? "\"" : ", \"") + esc(kv.first) + "\": " + std::to_string(kv.second);
                f = false;
            }
            return s + "}";
        };
        const designs::Instance& in0 = c.instances[static_cast<size_t>(ds.instances.front())];
        std::string parent;
        if (in0.parent >= 0) parent = c.designs[static_cast<size_t>(c.instances[static_cast<size_t>(in0.parent)].design)].name;
        o << "    {\"name\": \"" << esc(ds.name) << "\", \"family\": \"" << esc(ds.family) << "\", \"source\": \""
          << esc(ds.source) << "\", \"instances\": " << ds.instances.size() << ", \"parts_per_instance\": [" << per
          << "], \"parts_total\": " << parts << ", \"parts_first\": " << in0.own.size() << ", \"hidden_first\": " << hidden
          << ", \"nested_in\": \"" << esc(parent) << "\", \"primitives_first\": " << prims(first)
          << ", \"primitives_total\": " << prims(total) << ", \"nested_first\": " << prims(nested) << ", \"instance_list\": [";
        for (size_t k = 0; k < ds.instances.size(); ++k) {
            const designs::Instance& in = c.instances[static_cast<size_t>(ds.instances[k])];
            const Mat4& w = in.root->world;
            const Vec3 zAxis{w.at(0, 2), w.at(1, 2), w.at(2, 2)};
            const float s = length(Vec3{w.at(0, 0), w.at(1, 0), w.at(2, 0)});
            const float heading = degrees(std::atan2(zAxis.x, zAxis.z));
            o << (k ? ", " : "") << "{\"root_id\": \"" << esc(exportId(*in.root)) << "\", \"root_name\": \"" << esc(in.root->name)
              << "\", \"object\": \"" << esc(in.root->object) << "\", \"parts\": " << in.own.size() << ", \"position\": "
              << vecFull({w.at(0, 3), w.at(1, 3), w.at(2, 3)}) << ", \"heading_deg\": " << num(heading)
              << ", \"scale\": " << num(in.root->isLeaf() ? 1.0f : s) << ", \"hidden\": " << (hiddenAtRest(*in.root) ? "true" : "false")
              << "}";
        }
        o << "]}" << (d + 1 < c.designs.size() ? ",\n" : "\n");
    }
    o << "  ],\n  \"designs_unassigned\": " << c.unassigned.size() << ",\n";
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
    g_dfs = 0;
    g_rigLeafOrder.clear();
    Scene scene;  // freshly constructed = the initial (t = 0) state
    scene.updateWorld();
    const std::vector<LibraryEntry> prims = primitiveLibrary();
    for (SceneNode* r : scene.roots()) indexRigLeaves(*r);
    g_designs = designs::collect(scene.roots());

    std::ostringstream o;
    std::map<std::string, PrimUse> uses;
    o << "{\n  \"schema_version\": 9,\n"  // 6: + "v7" (keys); 7: + v7 raytrace / explorer / pov (WP-W); 8: v10 M3 library; 9: designs
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
    writeDesigns(o);
    writeControllables(o, scene);
    writeWalkSpace(o, scene);
    writeLayout(o);
    writeCombat(o);
    writeV5(o, scene);
    writeV6(o, scene);
    writeV7(o, scene);
    writeEffects(o);
    writePracticals(o, scene);
    writeSimulation(o);
    o << "  \"mesh_samples\": {\n";
    const MeshData cube = sceneMesh(PrimitiveType::Cube), sphere = sceneMesh(PrimitiveType::Sphere),
                   cylinder = sceneMesh(PrimitiveType::Cylinder);
    writeMeshSample(o, "Cube", cube, cube.vertexCount(), cube.indexCount(), false);
    writeMeshSample(o, "Sphere", sphere, kSampleVertices, kSampleIndices, false);
    writeMeshSample(o, "Cylinder", cylinder, kSampleVertices, kSampleIndices, true);
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

// Low-resolution copies of every library primitive for the report's primitive cards: the REAL generators called with
// small parameters (so every vertex can be numbered in a drawing), with their positions, normals and indices. The
// generators write no texture coordinates (MeshData has position + normal only), so none are exported.
bool exportPrimsLowres(const std::string& path) {
    struct Low {
        const char* name;
        const char* call;
        std::string params;
        MeshData mesh;
    };
    const std::vector<Low> low = {
        {"cube", "createCube()", "{}", createCube()},
        {"wedge", "createWedge()", "{}", createWedge()},
        {"tapered box", "createTaperedBox(0.8, 0.8)", "{\"topX\": 0.8, \"topZ\": 0.8}", createTaperedBox(0.8f, 0.8f)},
        {"pyramid", "createPyramid()", "{}", createPyramid()},
        {"sphere", "createSphere(8, 4)", "{\"s\": 8, \"k\": 4}", createSphere(8, 4)},
        {"hemisphere", "createHemisphere(8, 2)", "{\"s\": 8, \"k\": 2}", createHemisphere(8, 2)},
        {"capsule", "createCapsule(8, 2, 0.25)", "{\"s\": 8, \"c\": 2, \"h\": 0.25}", createCapsule(8, 2, 0.25f)},
        {"cylinder", "createCylinder(8, 0.5, 0.5, true)", "{\"s\": 8, \"rb\": 0.5, \"rt\": 0.5, \"capped\": true}", createCylinder(8, 0.5f, 0.5f, true)},
        {"frustum", "createCylinder(8, 0.03, 0.5, false)", "{\"s\": 8, \"rb\": 0.03, \"rt\": 0.5, \"capped\": false}",
         createCylinder(8, lib::kBeamTop, lib::kBeamBottom, false)},
        {"cone", "createCylinder(8, 0.5, 0, true)", "{\"s\": 8, \"rb\": 0.5, \"rt\": 0, \"capped\": true}", createCylinder(8, 0.5f, 0.0f, true)},
        {"torus", "createTorus(0.35, 0.15, 8, 6)", "{\"R\": 0.35, \"r\": 0.15, \"u\": 8, \"v\": 6}", createTorus(0.35f, 0.15f, 8, 6)},
        {"annulus", "createAnnulus(0.43, 8)", "{\"inner\": 0.43, \"n\": 8}", createAnnulus(0.43f, 8)},
        // drawn with a thicker tube than the scene's (r = 0.025) so the vertices can be numbered
        {"helix", "createHelix(0.42, 0.07, 2, 24, 6)", "{\"R\": 0.42, \"r\": 0.07, \"turns\": 2, \"segments\": 24, \"m\": 6}",
         createHelix(0.42f, 0.07f, 2.0f, 24, 6)},
        {"terrain disc", "createTerrainDisc(3, 8)", "{\"k\": 3, \"s\": 8, \"radius\": " + num(layout::kBaseRadius) + "}", createTerrainDisc(3, 8)},
        {"grass blade", "createGrassBlade()", "{}", createGrassBlade()},
    };
    std::ostringstream o;
    o << "{\n  \"note\": \"real generators at low resolution; positions / normals in the unit cube (terrain disc in metres)\",\n"
      << "  \"scene\": {\"sphere\": [" << lib::kSphereSlices << ", " << lib::kSphereStacks << "], \"hemisphere\": [" << lib::kHemiSlices
      << ", " << lib::kHemiStacks << "], \"cylinder\": " << lib::kCylinderSlices << ", \"torus\": [" << lib::kTorusU << ", "
      << lib::kTorusV << "], \"capsule\": [" << lib::kCapsuleSlices << ", " << lib::kCapsuleCapStacks << "], \"annulus\": "
      << lib::kAnnulusSegments << ", \"helix\": [" << lib::kHelixSegments << ", " << lib::kHelixSides << ", " << num(lib::kHelixTurns)
      << "], \"terrain\": [" << kTerrainRings << ", " << kTerrainSlices << "]},\n  \"primitives\": [\n";
    for (size_t i = 0; i < low.size(); ++i) {
        const MeshData& m = low[i].mesh;
        o << "    {\"name\": \"" << low[i].name << "\", \"call\": \"" << low[i].call << "\", \"params\": " << low[i].params
          << ", \"V\": " << m.vertexCount() << ", \"T\": " << m.triangleCount() << ",\n     \"positions\": [";
        for (size_t v = 0; v < m.vertices.size(); ++v) o << (v ? ", " : "") << vecFull(m.vertices[v].position);
        o << "],\n     \"normals\": [";
        for (size_t v = 0; v < m.vertices.size(); ++v) o << (v ? ", " : "") << vecFull(m.vertices[v].normal);
        o << "],\n     \"indices\": [";
        for (size_t k = 0; k < m.indices.size(); ++k) o << (k ? ", " : "") << m.indices[k];
        o << "]}" << (i + 1 < low.size() ? ",\n" : "\n");
    }
    o << "  ]\n}\n";
    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "Cannot write %s\n", path.c_str());
        return false;
    }
    f << o.str();
    std::printf("Low-resolution primitives written to %s (%zu)\n", path.c_str(), low.size());
    return true;
}

}  // namespace ts
