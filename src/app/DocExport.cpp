#include "app/DocExport.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "objects/Terrain.h"
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
                                  "Bark",  "Rock",     "Wood",   "Sand",  "Rubber", "Glass"};
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
                  kTerrainRadius);
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

void writeNode(std::ostream& o, const SceneNode& n, int depth, bool& first, std::map<PrimitiveType, int>& uses) {
    if (!first) o << ",\n";
    first = false;
    if (n.isLeaf()) ++uses[n.primitive];
    const Material& m = n.material;
    o << "    {\"id\": \"" << esc(n.id) << "\", \"object\": \"" << esc(n.object) << "\", \"name\": \"" << esc(n.name)
      << "\", \"primitive\": \"" << primitiveName(n.primitive) << "\", \"parent\": \""
      << (n.parent ? esc(n.parent->id + " " + n.parent->name) : std::string("World")) << "\", \"parent_id\": \""
      << (n.parent ? esc(n.parent->id) : std::string()) << "\", \"depth\": " << depth << ", \"shading\": \""
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

}  // namespace

bool exportSceneDocs(const std::string& path) {
    Scene scene;  // freshly constructed = the initial (t = 0) state
    scene.updateWorld();
    const std::vector<PrimInfo> prims = knownPrimitives();

    std::ostringstream o;
    std::map<PrimitiveType, int> uses;
    o << "{\n  \"schema_version\": 2,\n"
      << "  \"matrix_convention\": \"local_matrix and world_matrix are 16 floats ROW-MAJOR (row 0 first); "
         "M = T * Ry * Rx * Rz * S (R_deg = pitch X, yaw Y, roll Z); world = parent_world * M; the code stores "
         "Mat4 column-major (m[col*4+row]); world matrices are at t = 0\",\n";
    o << "  \"terrain\": {\"radius\": " << kTerrainRadius << ", \"rings\": " << kTerrainRings
      << ", \"slices\": " << kTerrainSlices << "},\n";
    o << "  \"components\": [\n";
    bool first = true;
    for (SceneNode* r : scene.roots()) writeNode(o, *r, 0, first, uses);
    o << "\n  ],\n";
    writePrimitives(o, prims, uses);
    writeLights(o, scene.lights);
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
