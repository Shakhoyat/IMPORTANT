// figdata: dumps the program's own data (meshes, node transforms, planner fields, CV / tracker runs) as JSON for
// tools/make_figures.py. Links the CORE sources only (no GL). Usage: build/figdata <outdir>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <functional>
#include <set>
#include <sstream>
#include <string>

#include "ai/DroneController.h"
#include "ai/HideController.h"
#include "ai/Squad.h"
#include "objects/BaseLayout.h"
#include "objects/Rover.h"
#include "objects/Terrain.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/Sphere.h"
#include "scene/Combat.h"
#include "scene/Mission.h"
#include "scene/Scene.h"
#include "vision/CvPipeline.h"
#include "vision/MultiTracker.h"
#include "vision/SensorModel.h"

using namespace ts;

namespace {

// ---------------------------------------------------------------- tiny JSON writer
struct J {
    std::ostringstream o;
    bool first = true;
    std::vector<bool> stack;
    void sep() { if (!first) o << ","; first = false; }
    void key(const char* k) { sep(); o << "\"" << k << "\":"; first = true; }
    void beginObj() { sep(); o << "{"; first = true; }
    void endObj() { o << "}"; first = false; }
    void beginArr() { sep(); o << "["; first = true; }
    void endArr() { o << "]"; first = false; }
    void num(double v) { sep(); if (std::isfinite(v)) o << v; else o << "null"; }
    void str(const std::string& s) { sep(); o << "\"" << s << "\""; }
    void boolean(bool b) { sep(); o << (b ? "true" : "false"); }
    void kv(const char* k, double v) { key(k); num(v); }
    void kvs(const char* k, const std::string& v) { key(k); str(v); }
    void vec(const char* k, const std::vector<double>& v) { key(k); beginArr(); for (double d : v) num(d); endArr(); }
    void vecf(const char* k, const std::vector<float>& v) { key(k); beginArr(); for (float d : v) num(d); endArr(); }
    void veci(const char* k, const std::vector<int>& v) { key(k); beginArr(); for (int d : v) num(d); endArr(); }
};

std::string gOut;
void save(const char* name, J& j) {
    std::ofstream f(gOut + "/" + name);
    f << j.o.str();
    std::printf("wrote %s (%zu bytes)\n", name, j.o.str().size());
}

uint32_t gRng = 12345;
float rnd() { gRng = gRng * 1664525u + 1013904223u; return static_cast<float>(gRng >> 8) / 16777216.0f; }
float gauss() { float u = std::max(rnd(), 1e-6f), v = rnd(); return std::sqrt(-2.0f * std::log(u)) * std::cos(2.0f * PI * v); }

void meshJson(J& j, const char* k, const MeshData& m) {
    j.key(k);
    j.beginObj();
    j.kvs("name", m.name);
    std::vector<float> p, n;
    for (const Vertex& v : m.vertices) { p.push_back(v.position.x); p.push_back(v.position.y); p.push_back(v.position.z); n.push_back(v.normal.x); n.push_back(v.normal.y); n.push_back(v.normal.z); }
    j.vecf("pos", p);
    j.vecf("nrm", n);
    j.key("idx"); j.beginArr(); for (uint32_t i : m.indices) j.num(i); j.endArr();
    j.kv("nonIndexedBytes", static_cast<double>(m.nonIndexedBytes()));
    j.kv("indexedBytes", static_cast<double>(m.indexedBytes()));
    j.endObj();
}

void mat(J& j, const char* k, const Mat4& m) {  // row-major 4x4
    j.key(k);
    j.beginArr();
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) j.num(m.at(r, c));
    j.endArr();
}

// ---------------------------------------------------------------- A: primitives, nodes
void dumpPrimitives() {
    J j;
    j.beginObj();
    meshJson(j, "cube", createCube());
    meshJson(j, "sphere", createSphere(36, 18));
    meshJson(j, "cylinder", createCylinder(36));
    meshJson(j, "cone", createCylinder(36, 0.5f, 0.0f, true));
    meshJson(j, "beamcone", createCylinder(36, 0.03f, 0.5f, false));
    meshJson(j, "sphere_small", createSphere(12, 6));
    meshJson(j, "cylinder12", createCylinder(12));
    meshJson(j, "cone12", createCylinder(12, 0.5f, 0.0f, true));
    meshJson(j, "beamcone12", createCylinder(12, 0.03f, 0.5f, false));
    // terrain: coarse copy for the picture, real counts, and the hole window of the real mesh
    meshJson(j, "terrain_coarse", createTerrainDisc(12, 48, layout::kBaseRadius));
    const MeshData t = createTerrainDisc();
    j.key("terrain_real");
    j.beginObj();
    j.kv("rings", kTerrainRings);
    j.kv("slices", kTerrainSlices);
    j.kv("vertices", static_cast<double>(t.vertexCount()));
    j.kv("triangles", static_cast<double>(t.triangleCount()));
    j.kv("expectedNoHole", static_cast<double>(kTerrainSlices * (2 * kTerrainRings - 1)));
    j.kv("radius", layout::kBaseRadius);
    j.kv("rampHalfWidth", layout::kRampHalfWidth);
    j.kv("rampTopZ", layout::kRampTopZ);
    j.kv("blastDoorZ", layout::kBlastDoorZ);
    std::vector<float> w;  // triangles whose centroid lies in the window x in [-8,8], z in [-2,22]
    for (size_t i = 0; i + 2 < t.indices.size(); i += 3) {
        Vec3 c = (t.vertices[t.indices[i]].position + t.vertices[t.indices[i + 1]].position + t.vertices[t.indices[i + 2]].position) * (1.0f / 3.0f);
        if (c.x < -8 || c.x > 8 || c.z < -2 || c.z > 22) continue;
        for (int k = 0; k < 3; ++k) { const Vec3& p = t.vertices[t.indices[i + k]].position; w.push_back(p.x); w.push_back(p.y); w.push_back(p.z); }
    }
    j.vecf("windowTris", w);
    j.endObj();
    j.endObj();
    save("primitives.json", j);
}

void walk(J& j, const SceneNode* n, const SceneNode* parent, int& counter, std::map<const SceneNode*, int>& ids) {
    const int id = counter++;
    ids[n] = id;
    j.beginObj();
    j.kv("i", id);
    j.kv("parent", parent ? ids[parent] : -1);
    j.kvs("id", n->id);
    j.kvs("name", n->name);
    j.kvs("object", n->object);
    j.kvs("primitive", primitiveName(n->primitive));
    j.vec("T", {n->local.translation.x, n->local.translation.y, n->local.translation.z});
    j.vec("R", {n->local.rotationDeg.x, n->local.rotationDeg.y, n->local.rotationDeg.z});
    j.vec("S", {n->local.scale.x, n->local.scale.y, n->local.scale.z});
    mat(j, "local", n->local.matrix());
    mat(j, "world", n->world);
    j.vec("color", {n->material.baseColor.x, n->material.baseColor.y, n->material.baseColor.z});
    j.kv("rough", n->material.roughness);
    j.kv("metal", n->material.metalness);
    j.kv("spec", n->material.specular);
    j.kv("shin", n->material.shininess);
    j.kvs("shading", shadingName(n->shading));
    j.endObj();
    for (const auto& c : n->children) walk(j, c.get(), n, counter, ids);
}

void dumpNodes(Scene& s) {
    s.updateWorld();
    J j;
    j.beginObj();
    // tank (first controllable whose name contains TANK)
    for (const auto& c : s.controllables()) {
        const std::string nm = c->name();
        if (nm.find("TANK") == std::string::npos) continue;
        j.key("tank");
        j.beginArr();
        int cnt = 0;
        std::map<const SceneNode*, int> ids;
        // the controllable's own world matrices are valid after Scene::updateWorld
        walk(j, c->root(), nullptr, cnt, ids);
        j.endArr();
        break;
    }
    j.key("uav");
    j.beginArr();
    {
        int cnt = 0;
        std::map<const SceneNode*, int> ids;
        walk(j, s.uav(0).root.get(), nullptr, cnt, ids);
    }
    j.endArr();
    j.key("rover");
    j.beginArr();
    {
        int cnt = 0;
        std::map<const SceneNode*, int> ids;
        walk(j, s.roverRig().root.get(), nullptr, cnt, ids);
    }
    j.endArr();
    j.key("practicals");
    j.beginArr();
    for (const SceneNode* n : s.practicalLights()) {
        j.beginObj(); j.kvs("name", n->name); j.kvs("object", n->object); j.kv("intensity", n->material.lightIntensity);
        j.kv("range", n->material.lightRange); j.kv("cone", n->material.lightConeDeg); j.kv("emissive", n->material.emissive); j.endObj();
    }
    j.endArr();
    int total = 0, leaves = 0;
    std::map<std::string, int> prim;
    for (const SceneNode* r : s.roots()) {
        std::function<void(const SceneNode*)> f = [&](const SceneNode* n) {
            ++total;
            if (n->isLeaf()) { ++leaves; ++prim[primitiveName(n->primitive)]; }
            for (auto& c : n->children) f(c.get());
        };
        f(r);
    }
    j.kv("totalNodes", total);
    j.kv("leafParts", leaves);
    j.key("primCounts");
    j.beginObj();
    for (auto& [k, v] : prim) j.kv(k.c_str(), v);
    j.endObj();
    j.endObj();
    save("nodes.json", j);
}

// ---------------------------------------------------------------- E: micromouse / LFR
std::vector<Vec3> chaikinCopy(const std::vector<Vec3>& pts, int iterations) {  // identical to HideController.cpp
    std::vector<Vec3> cur = pts;
    for (int it = 0; it < iterations && cur.size() > 2; ++it) {
        std::vector<Vec3> next;
        next.push_back(cur.front());
        for (size_t i = 0; i + 1 < cur.size(); ++i) {
            next.push_back(lerp(cur[i], cur[i + 1], 0.25f));
            next.push_back(lerp(cur[i], cur[i + 1], 0.75f));
        }
        next.push_back(cur.back());
        cur = next;
    }
    return cur;
}

void gridU8(J& j, const char* k, const std::function<int(int, int)>& f) {
    j.key(k);
    j.beginArr();
    for (int cz = 0; cz < kGridCells; ++cz) for (int cx = 0; cx < kGridCells; ++cx) j.num(f(cx, cz));
    j.endArr();
}

void pts(J& j, const char* k, const std::vector<Vec3>& p) {
    j.key(k);
    j.beginArr();
    for (const Vec3& v : p) { j.num(v.x); j.num(v.z); }
    j.endArr();
}

void dumpMicromouse(Scene& s) {
    // let the squad patrol a little so the two UAVs are at "typical" positions
    s.setMissionEnabled(false);
    for (int i = 0; i < 30 * 10; ++i) s.update(1.0f / 30.0f, Detection{});
    const Vec3 uav[2] = {s.droneState(0).position, s.droneState(1).position};
    HideController base = s.intruders()[0]->hide;  // copy: has the real free-cell grid
    const Vec3 start{-12.0f, 0.0f, -10.0f};
    const Vec3 goal = layout::kObjectives[2];
    J j;
    j.beginObj();
    j.kv("grid", kGridCells);
    j.kv("cell", kCellSize);
    j.kv("start_x", start.x); j.kv("start_z", start.z);
    j.kv("goal_x", goal.x); j.kv("goal_z", goal.z);
    j.vec("uav0", {uav[0].x, uav[0].y, uav[0].z});
    j.vec("uav1", {uav[1].x, uav[1].y, uav[1].z});
    gridU8(j, "free", [&](int x, int z) { return base.cellFree(x, z) ? 1 : 0; });
    HideController h = base;
    unsigned char exposedAny[kGridCells * kGridCells] = {};
    for (int u = 0; u < 2; ++u) {
        h.computeVisibility(uav[u]);
        char nm[16];
        std::snprintf(nm, sizeof nm, "hidden%d", u);
        gridU8(j, nm, [&](int x, int z) { return h.cellHidden(x, z) ? 1 : 0; });
        for (int z = 0; z < kGridCells; ++z) for (int x = 0; x < kGridCells; ++x) if (h.cellFree(x, z) && !h.cellHidden(x, z)) exposedAny[z * kGridCells + x] = 1;
    }
    gridU8(j, "exposed", [&](int x, int z) { return exposedAny[z * kGridCells + x] ? 1 : 0; });
    // BFS hiding field (v4 mode) for UAV 0
    h.computeVisibility(uav[0]);
    h.floodFill();
    gridU8(j, "bfs_flood", [&](int x, int z) { return h.floodValue(x, z); });
    {
        int cx, cz;
        HideController::worldToCell(start, cx, cz);
        pts(j, "bfs_route", h.descend(cx, cz));
    }
    j.key("seeds");
    j.beginArr();
    for (uint32_t seed : {11u, 977u}) {
        HideController r = base;
        r.setObjective(goal, seed);
        r.floodFillToObjective({uav[0], uav[1]});
        j.beginObj();
        j.kv("seed", seed);
        gridU8(j, "flood", [&](int x, int z) { return r.floodValue(x, z); });
        j.key("noise"); j.beginArr();
        for (int z = 0; z < kGridCells; ++z) for (int x = 0; x < kGridCells; ++x) j.num(r.noiseAt(x, z));
        j.endArr();
        int cx, cz;
        HideController::worldToCell(start, cx, cz);
        const std::vector<Vec3> cells = r.descend(cx, cz);
        pts(j, "route", cells);
        // the guide line exactly as HideController::replan builds it (heading yaw 0.8 as in T144)
        const float yaw = 0.8f;
        std::vector<Vec3> raw{start, start + Vec3{std::sin(yaw), 0, std::cos(yaw)} * (0.31f + 0.1f)};
        raw.insert(raw.end(), cells.begin(), cells.end());
        pts(j, "raw", raw);
        pts(j, "chaikin", chaikinCopy(raw, 3));
        j.endObj();
    }
    j.endArr();
    j.endObj();
    save("micromouse.json", j);
}

void dumpDrive() {
    Scene s;
    s.setMissionEnabled(false);
    s.destroyIntruder(0, s.intruders()[0]->state.position + Vec3{0, 50, 0}, 0.0f);
    const Vec3 start{-12.0f, 0.0f, -10.0f};
    const Vec3 goal = layout::kObjectives[2];
    const int slot = s.spawnIntruder(start, 0.8f, goal, 11u);
    const float dt = 1.0f / 30.0f;
    J j;
    j.beginObj();
    j.kv("dt", dt);
    j.kv("kp", 3.2); j.kv("kd", 0.25); j.kv("maxSpeed", 0.75); j.kv("trackWidth", kRoverTrackWidth);
    j.kv("sensorForward", 0.31); j.kv("sensorSpacing", 0.06); j.kv("lineHalfWidth", 0.05);
    j.kv("goal_x", goal.x); j.kv("goal_z", goal.z);
    std::vector<float> t, x, z, yaw, ls, rs, bits;
    std::vector<int> evade;
    std::vector<std::vector<Vec3>> lines;
    std::vector<float> lineT;
    const Intruder& in = *s.intruders()[slot];
    Vec3 lastFirst{1e9f, 0, 0};
    size_t lastSize = 0;
    for (int f = 0; f < 30 * 70; ++f) {
        s.update(dt, Detection{});
        const RoverState& r = in.state;
        t.push_back(s.time()); x.push_back(r.position.x); z.push_back(r.position.z); yaw.push_back(r.yaw);
        ls.push_back(r.leftSpeed); rs.push_back(r.rightSpeed);
        float b = 0; for (int i = 0; i < 5; ++i) b += (r.sensors[i] ? 1 : 0) * static_cast<float>(1 << i);
        bits.push_back(b);
        evade.push_back(in.hide.evading() ? 1 : 0);
        const auto& g = in.hide.guideLine();
        if (!g.empty() && (g.size() != lastSize || length(g.back() - lastFirst) > 1e-3f)) {
            lines.push_back(g); lineT.push_back(s.time()); lastSize = g.size(); lastFirst = g.back();
        }
        if (in.life != Intruder::Life::Alive || in.hide.reachedObjective(r)) break;
    }
    j.vecf("t", t); j.vecf("x", x); j.vecf("z", z); j.vecf("yaw", yaw); j.vecf("left", ls); j.vecf("right", rs); j.vecf("bits", bits);
    j.veci("evading", evade);
    j.vecf("lineT", lineT);
    j.key("lines"); j.beginArr();
    for (auto& l : lines) { j.beginArr(); for (const Vec3& p : l) { j.num(p.x); j.num(p.z); } j.endArr(); }
    j.endArr();
    j.endObj();
    save("drive.json", j);
}

// ---------------------------------------------------------------- F: vision
Mat4 camMatrix(const Vec3& pos, const Vec3& target) {  // camera looks along +Z, +Y up, image-right = local -X
    const Vec3 f = normalize(target - pos);
    const Vec3 X = normalize(cross(Vec3{0, 1, 0}, f));
    const Vec3 Y = cross(f, X);
    Mat4 m;
    const Vec3 cols[4] = {X, Y, f, pos};
    for (int c = 0; c < 4; ++c) { m.at(0, c) = cols[c].x; m.at(1, c) = cols[c].y; m.at(2, c) = cols[c].z; }
    m.at(3, 0) = m.at(3, 1) = m.at(3, 2) = 0; m.at(3, 3) = 1;
    return m;
}

void blobJson(J& j, const Measurement2D& m) {
    j.beginObj();
    j.kv("cx", m.cx); j.kv("cy", m.cy); j.kv("area", m.area); j.kv("mean", m.meanHeat); j.kv("peak", m.peak);
    j.kv("orient", m.orientationDeg); j.kv("elong", m.elongation);
    j.kv("minX", m.minX); j.kv("minY", m.minY); j.kv("maxX", m.maxX); j.kv("maxY", m.maxY);
    j.endObj();
}

void dumpCv() {
    const int W = 320, H = 240;
    std::vector<uint8_t> img(W * H);
    auto put = [&](int x, int y, float v) { if (x >= 0 && y >= 0 && x < W && y < H) img[y * W + x] = static_cast<uint8_t>(std::min(255.0f, std::max(float(img[y * W + x]), v * 255.0f))); };
    gRng = 777;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float g = 0.10f + 0.10f * (1.0f - y / float(H)) + 0.025f * gauss();  // ambient night ground, warmer to the horizon
            img[y * W + x] = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, g * 255.0f)));
        }
    struct Blob { float cx, cy, rx, ry, ang, body, core; };
    // warm bunker wall / hut (large, lukewarm), two rovers (small, hot engine), fire (huge, hot)
    const Blob blobs[] = {{70, 170, 26, 10, 0.0f, 0.55f, 0.0f},      // lukewarm hut
                          {200, 120, 6.5f, 3.5f, 0.5f, 0.86f, 0.97f},  // rover A
                          {120, 70, 4.0f, 2.5f, -0.3f, 0.84f, 0.95f},  // rover B (far)
                          {262, 180, 38, 30, 0.0f, 0.93f, 1.0f}};      // fire
    for (const Blob& b : blobs)
        for (int y = int(b.cy - 40); y < int(b.cy + 40); ++y)
            for (int x = int(b.cx - 40); x < int(b.cx + 40); ++x) {
                const float dx = x - b.cx, dy = y - b.cy;
                const float u = (dx * std::cos(b.ang) + dy * std::sin(b.ang)) / b.rx, v = (-dx * std::sin(b.ang) + dy * std::cos(b.ang)) / b.ry;
                const float r2 = u * u + v * v;
                if (r2 < 1.0f) put(x, y, b.body + 0.04f * gauss() + (b.core > 0 ? (b.core - b.body) * std::max(0.0f, 1.0f - 2.2f * r2) : 0.0f));
                else if (r2 < 2.2f) put(x, y, 0.12f + 0.4f * (b.body - 0.12f) * (2.2f - r2) / 1.2f);  // soft halo
            }
    // thin hot streak (tracer): 70 px long, 1 px wide
    for (int i = 0; i < 70; ++i) put(30 + i, 40 + i / 5, 0.98f);
    // sparks / glints: isolated hot pixels
    for (int k = 0; k < 40; ++k) put(int(rnd() * W), int(rnd() * H), 0.85f + 0.15f * rnd());
    std::vector<uint8_t> work = img;
    CvFrameStats st;
    CvConfig cfg;
    const std::vector<Measurement2D> meas = cv::detectTargets(img, W, H, cfg, &st);
    // intermediate masks, produced with the same public stage functions in the same order as detectTargets
    std::vector<uint8_t> mask(img.size()), mask2;
    for (size_t i = 0; i < img.size(); ++i) mask[i] = img[i] >= st.threshold ? 1 : 0;
    mask2 = mask;
    const int removed = cv::despeckle(mask2, W, H);
    std::vector<int> labels;
    const int n = cv::labelComponents(mask2, W, H, labels);
    // all components (before the target filter) with their features
    CvConfig loose = cfg; loose.minArea = 0; loose.maxArea = 1 << 30; loose.core = 0; loose.maxElongation = 1e9f;
    const std::vector<Measurement2D> all = cv::detectTargets(img, W, H, loose, nullptr);
    J j;
    j.beginObj();
    j.kv("w", W); j.kv("h", H);
    j.key("image"); j.beginArr(); for (uint8_t v : img) j.num(v); j.endArr();
    j.key("mask"); j.beginArr(); for (uint8_t v : mask) j.num(v); j.endArr();
    j.key("mask2"); j.beginArr(); for (uint8_t v : mask2) j.num(v); j.endArr();
    j.key("labels"); j.beginArr(); for (int v : labels) j.num(v); j.endArr();
    j.kv("otsu", st.otsu); j.kv("threshold", st.threshold); j.kv("hotPixels", st.hotPixels);
    j.kv("removed", removed); j.kv("components", n); j.kv("targets", st.targets);
    j.kv("floor", cfg.floor); j.kv("core", cfg.core); j.kv("minArea", cfg.minArea); j.kv("maxArea", cfg.maxArea); j.kv("maxElong", cfg.maxElongation);
    j.key("targetsList"); j.beginArr(); for (auto& m : meas) blobJson(j, m); j.endArr();
    j.key("allList"); j.beginArr(); for (auto& m : all) blobJson(j, m); j.endArr();
    // small union-find example (12 x 7): a U shape (two provisional labels that merge) plus an isolated blob
    const char* ex[7] = {"............",
                         ".#...#..##..",
                         ".#...#..##..",
                         ".##.##......",
                         "..###....#..",
                         "............",
                         "............"};
    std::vector<uint8_t> em(12 * 7);
    for (int y = 0; y < 7; ++y) for (int x = 0; x < 12; ++x) em[y * 12 + x] = ex[y][x] == '#';
    std::vector<int> el;
    const int en = cv::labelComponents(em, 12, 7, el);
    j.key("uf"); j.beginObj();
    j.kv("w", 12); j.kv("h", 7); j.kv("count", en);
    j.key("mask"); j.beginArr(); for (uint8_t v : em) j.num(v); j.endArr();
    j.key("labels"); j.beginArr(); for (int v : el) j.num(v); j.endArr();
    j.endObj();
    // despeckle example: same mask + 3 isolated pixels
    j.endObj();
    save("cv.json", j);
}

bool dumpKalman(uint32_t seed, bool write) {
    // One UAV watching a rover that drives straight, turns, then straight again. 30 Hz, sigma 0.3 m.
    MultiTracker mt;
    gRng = seed;
    bool clean = true;
    float pvPrev = 0.0f;
    const float dt = 1.0f / 30.0f, sigma = 0.3f;
    J j;
    j.beginObj();
    j.kv("sigma", sigma); j.kv("dt", dt); j.kv("accelNoise", mt.accelNoise);
    std::vector<float> t, tx, tz, mx, mz, ex, ez, evx, evz, tvx, tvz, p00, p01, p11, pv;
    std::vector<int> state;
    float px = 0, pz = 0, yaw = 0.2f;
    const float speed = 0.7f;
    for (int f = 0; f < 30 * 14; ++f) {
        const float time = f * dt;
        if (time > 5.0f && time < 8.0f) yaw += 0.5f * dt;
        const float vx = speed * std::sin(yaw), vz = speed * std::cos(yaw);
        px += vx * dt; pz += vz * dt;
        mt.predict(dt);
        std::vector<GroundMeasurement> ms;
        GroundMeasurement m;
        m.point = {px + sigma * gauss(), 0, pz + sigma * gauss()};
        m.sigma = sigma;
        m.sensor = 0;
        m.rayOrigin = {0, 6, 0}; m.rayDir = normalize(m.point - m.rayOrigin);
        ms.push_back(m);
        mt.update(ms, time);
        if (mt.tracks().size() != 1 || mt.tracks()[0].id != 1) clean = false;
        if (!mt.tracks().empty()) {
            const float pvNow = std::sqrt(0.5f * (mt.tracks()[0].P[10] + mt.tracks()[0].P[15]));
            if (f > 30 && pvNow > pvPrev + 0.3f) clean = false;  // a merge with a newborn track resets the velocity (see notes)
            pvPrev = pvNow;
        }
        t.push_back(time); tx.push_back(px); tz.push_back(pz); tvx.push_back(vx); tvz.push_back(vz);
        mx.push_back(m.point.x); mz.push_back(m.point.z);
        if (!mt.tracks().empty()) {
            const Track& k = mt.tracks()[0];
            ex.push_back(k.x[0]); ez.push_back(k.x[1]); evx.push_back(k.x[2]); evz.push_back(k.x[3]);
            p00.push_back(k.P[0]); p01.push_back(k.P[1]); p11.push_back(k.P[5]); pv.push_back(std::sqrt(0.5f * (k.P[10] + k.P[15])));
            state.push_back(static_cast<int>(k.state));
        } else {
            ex.push_back(NAN); ez.push_back(NAN); evx.push_back(NAN); evz.push_back(NAN); p00.push_back(NAN); p01.push_back(NAN); p11.push_back(NAN); pv.push_back(NAN); state.push_back(-1);
        }
    }
    j.vecf("t", t); j.vecf("tx", tx); j.vecf("tz", tz); j.vecf("mx", mx); j.vecf("mz", mz);
    j.vecf("ex", ex); j.vecf("ez", ez); j.vecf("evx", evx); j.vecf("evz", evz); j.vecf("tvx", tvx); j.vecf("tvz", tvz);
    j.vecf("p00", p00); j.vecf("p01", p01); j.vecf("p11", p11); j.vecf("velSigma", pv); j.veci("state", state);
    j.endObj();
    if (write) save("kalman.json", j);
    j.kv("seed", seed);
    return clean;
}

void dumpAssociation() {
    // Three tracks with moving targets; frame k: measurements arrive in shuffled order, one is a clutter outlier.
    MultiTracker mt;
    gRng = 99;
    const float dt = 0.1f;
    const float sx[3] = {-4.0f, 0.0f, 3.5f}, sz[3] = {2.0f, 5.0f, 1.0f}, vx[3] = {0.5f, -0.3f, -0.6f}, vz[3] = {0.2f, -0.5f, 0.4f};
    auto feed = [&](float time, bool shuffle, int extra) {
        std::vector<GroundMeasurement> ms;
        for (int k = 0; k < 3; ++k) {
            GroundMeasurement m;
            m.point = {sx[k] + vx[k] * time + 0.25f * gauss(), 0, sz[k] + vz[k] * time + 0.25f * gauss()};
            m.sigma = 0.25f; m.sensor = 0;
            ms.push_back(m);
        }
        if (shuffle) std::swap(ms[0], ms[2]);
        (void)extra;
        return ms;
    };
    float time = 0;
    for (int f = 0; f < 15; ++f) { time += dt; mt.predict(dt); mt.update(feed(time, false, 0), time); }
    time += dt;
    mt.predict(dt);
    std::vector<GroundMeasurement> ms = feed(time, true, 0);
    // measurement 1 is moved far away (a false alarm in the gate's eyes): it fails the gate for every track
    ms[1].point.x += 3.0f; ms[1].point.z -= 2.5f;
    const int nT = static_cast<int>(mt.tracks().size());
    std::vector<std::vector<float>> d2(nT, std::vector<float>(ms.size())), cost = d2;
    for (int t = 0; t < nT; ++t)
        for (size_t k = 0; k < ms.size(); ++k) {
            d2[t][k] = MultiTracker::mahalanobis2(mt.tracks()[t], ms[k].point.x, ms[k].point.z, ms[k].sigma * ms[k].sigma);
            cost[t][k] = d2[t][k] < mt.gateChi2 ? d2[t][k] : 1e6f;
        }
    const std::vector<int> a = MultiTracker::hungarian(cost);
    J j;
    j.beginObj();
    j.kv("gate", mt.gateChi2);
    j.key("tracks"); j.beginArr();
    for (const Track& t : mt.tracks()) {
        j.beginObj(); j.kv("id", t.id); j.kv("x", t.x[0]); j.kv("z", t.x[1]); j.kv("p00", t.P[0]); j.kv("p01", t.P[1]); j.kv("p11", t.P[5]); j.endObj();
    }
    j.endArr();
    j.key("meas"); j.beginArr();
    for (auto& m : ms) { j.beginObj(); j.kv("x", m.point.x); j.kv("z", m.point.z); j.kv("sigma", m.sigma); j.endObj(); }
    j.endArr();
    j.key("d2"); j.beginArr(); for (auto& r : d2) for (float v : r) j.num(v); j.endArr();
    j.key("cost"); j.beginArr(); for (auto& r : cost) for (float v : r) j.num(v); j.endArr();
    j.veci("assign", a);
    j.endObj();
    save("assoc.json", j);
}

void dumpTriangulation() {
    J j;
    j.beginObj();
    const Vec3 target{2.0f, 0.0f, 3.0f};  // flat-ish ground point (height taken from the terrain below)
    const float ty = terrainHeight(target.x, target.z);
    const Vec3 tgt{target.x, ty + 0.2f, target.z};
    const float ptyS = std::tan(radians(kCvFovYDeg) * 0.5f), ptxS = ptyS * float(kCvWidth) / kCvHeight;
    j.kv("fovY", kCvFovYDeg); j.kv("w", kCvWidth); j.kv("h", kCvHeight);
    // Geometry example: UAV A, UAV B at 90 degrees, the 6.5 m stand-off / 5 m altitude of the squad
    auto uavAt = [&](float angDeg) {
        const float a = radians(angDeg);
        return Vec3{tgt.x + kSquadStandOff * std::cos(a), kSquadEngageAlt, tgt.z + kSquadStandOff * std::sin(a)};
    };
    const Vec3 pa = uavAt(0), pb = uavAt(90);
    const Mat4 ca = camMatrix(pa, tgt), cb = camMatrix(pb, tgt);
    gRng = 31337;
    const float pxSigma = 1.5f;  // pointing noise in pixels
    auto noisyRay = [&](const Mat4& cam, const Vec3& pos, float& u, float& v) {
        // project the true point then perturb pixel
        const Vec3 l = transformPoint(inverse(cam), tgt);
        u = -(l.x / l.z) / ptxS + gauss() * pxSigma * 2.0f / kCvWidth;
        v = (l.y / l.z) / ptyS + gauss() * pxSigma * 2.0f / kCvHeight;
        (void)pos;
        return DroneController::pixelRay(u, v, cam);
    };
    {
        float u, v;
        const Vec3 d1 = noisyRay(ca, pa, u, v), d2 = noisyRay(cb, pb, u, v);
        Vec3 mid; float res = 0;
        const bool ok = MultiTracker::triangulate(pa, d1, pb, d2, mid, res);
        // closest points individually
        const Vec3 w = pa - pb;
        const float a = dot(d1, d1), b = dot(d1, d2), c = dot(d2, d2), d = dot(d1, w), e = dot(d2, w);
        const float den = a * c - b * b, sA = (b * e - c * d) / den, sB = (a * e - b * d) / den;
        const Vec3 p1 = pa + d1 * sA, p2 = pb + d2 * sB;
        j.key("example"); j.beginObj();
        j.vec("uavA", {pa.x, pa.y, pa.z}); j.vec("uavB", {pb.x, pb.y, pb.z}); j.vec("target", {tgt.x, tgt.y, tgt.z});
        j.vec("dirA", {d1.x, d1.y, d1.z}); j.vec("dirB", {d2.x, d2.y, d2.z});
        j.vec("p1", {p1.x, p1.y, p1.z}); j.vec("p2", {p2.x, p2.y, p2.z}); j.vec("mid", {mid.x, mid.y, mid.z});
        j.kv("residual", res); j.key("ok"); j.boolean(ok);
        j.endObj();
    }
    // Monte Carlo: error vs angle between the two UAVs (0 = same side ... 180 = opposite)
    j.key("mc"); j.beginObj();
    std::vector<double> ang, single, tri, triOk;
    const int N = 1500;
    for (int deg = 0; deg <= 180; deg += 15) {
        const Vec3 qa = uavAt(0), qb = uavAt(float(deg));
        const Mat4 ma = camMatrix(qa, tgt), mb = camMatrix(qb, tgt);
        double sS = 0, sT = 0; int ok = 0;
        for (int n = 0; n < N; ++n) {
            float u, v;
            const Vec3 d1 = noisyRay(ma, qa, u, v);
            Vec3 hit;
            const bool h1 = DroneController::imagePointToGround(u, v, ma, hit);
            const Vec3 d2 = noisyRay(mb, qb, u, v);
            double e1 = h1 ? std::hypot(hit.x - tgt.x, hit.z - tgt.z) : 0.0;
            Vec3 mid; float res;
            if (MultiTracker::triangulate(qa, d1, qb, d2, mid, res)) {
                sT += std::pow(std::hypot(mid.x - tgt.x, mid.z - tgt.z), 2.0);
                ++ok;
            }
            // single ray: hit measured against the ground point below the target at terrain height
            sS += e1 * e1;
        }
        ang.push_back(deg); single.push_back(std::sqrt(sS / N)); tri.push_back(ok ? std::sqrt(sT / ok) : NAN); triOk.push_back(double(ok) / N);
    }
    j.vec("angle", ang); j.vec("single", single); j.vec("tri", tri); j.vec("ok", triOk);
    j.kv("pxSigma", pxSigma); j.kv("N", N);
    j.endObj();
    // Error scatter at 90 degrees
    j.key("scatter"); j.beginObj();
    std::vector<double> sx, sz2, tx2, tz2;
    for (int n = 0; n < 400; ++n) {
        float u, v;
        const Vec3 d1 = noisyRay(ca, pa, u, v);
        Vec3 hit;
        if (DroneController::imagePointToGround(u, v, ca, hit)) { sx.push_back(hit.x - tgt.x); sz2.push_back(hit.z - tgt.z); }
        const Vec3 d2 = noisyRay(cb, pb, u, v);
        Vec3 mid; float res;
        if (MultiTracker::triangulate(pa, d1, pb, d2, mid, res)) { tx2.push_back(mid.x - tgt.x); tz2.push_back(mid.z - tgt.z); }
    }
    j.vec("sx", sx); j.vec("sz", sz2); j.vec("tx", tx2); j.vec("tz", tz2);
    j.endObj();
    j.endObj();
    save("triangulation.json", j);
}

// ---------------------------------------------------------------- squad run with synthetic vision (as tests/test_squad.cpp)
std::vector<Measurement2D> syntheticBlobs(const Scene& s, int uav) {
    std::vector<Measurement2D> out;
    const Mat4 inv = inverse(s.cameraWorld(uav));
    const float ty = std::tan(radians(kCvFovYDeg) * 0.5f), tx = ty * static_cast<float>(kCvWidth) / kCvHeight;
    for (const auto& in : s.intruders()) {
        if (in->life != Intruder::Life::Alive) continue;
        const Vec3 l = transformPoint(inv, in->state.position + Vec3{0, 0.2f, 0});
        if (l.z < 0.5f) continue;
        if (!s.squad().clearShot(transformPoint(s.cameraWorld(uav), {0, 0, 0}), in->state.position + Vec3{0, 0.25f, 0})) continue;
        Measurement2D m;
        m.u = -(l.x / l.z) / tx;
        m.v = (l.y / l.z) / ty;
        if (std::fabs(m.u) > 0.98f || std::fabs(m.v) > 0.98f) continue;
        m.area = 12;
        m.peak = 255;
        out.push_back(m);
    }
    return out;
}

void dumpSquad() {
    Scene s;
    const float dt = 1.0f / 30.0f;
    J j;
    j.beginObj();
    std::vector<float> t;
    std::vector<int> role0, role1, phase, target;
    std::vector<float> la0, la1, ft0, ft1, lat0, lat1;
    std::vector<int> l0, l1, comb;
    j.key("frames"); j.beginArr();
    std::vector<GameEvent> evs;
    for (int f = 0; f < 30 * 90; ++f) {
        for (int u = 0; u < kUavCount; ++u) s.submitVision(u, syntheticBlobs(s, u), s.cameraWorld(u));
        s.update(dt, Detection{});
        for (const GameEvent& e : s.events()) evs.push_back(e);
        const StrikeState& st = s.squad().strike();
        if (f % 3 != 0) continue;  // 10 Hz
        j.beginObj();
        j.kv("t", s.time());
        j.kv("r0", static_cast<int>(s.squad().roles[0])); j.kv("r1", static_cast<int>(s.squad().roles[1]));
        j.kv("phase", static_cast<int>(st.phase)); j.kv("track", st.track); j.key("combined"); j.boolean(st.combined);
        j.vec("launchAt", {st.launchAt[0], st.launchAt[1]});
        j.vec("flight", {st.flightTime[0], st.flightTime[1]});
        j.vec("launchedAt", {st.launchedAt[0], st.launchedAt[1]});
        j.vec("launched", {double(st.launched[0]), double(st.launched[1])});
        j.vec("uav0", {s.droneState(0).position.x, s.droneState(0).position.z});
        j.vec("uav1", {s.droneState(1).position.x, s.droneState(1).position.z});
        j.key("tracks"); j.beginArr();
        for (const Track& k : s.squad().tracker().tracks()) {
            j.beginArr(); j.num(k.id); j.num(k.x[0]); j.num(k.x[1]); j.num(k.positionSigma()); j.num(static_cast<int>(k.state)); j.num(k.seenNow); j.num(k.triangulated); j.endArr();
        }
        j.endArr();
        j.key("rovers"); j.beginArr();
        for (const auto& in : s.intruders()) if (in->life == Intruder::Life::Alive) { j.beginArr(); j.num(in->state.position.x); j.num(in->state.position.z); j.endArr(); }
        j.endArr();
        j.endObj();
    }
    j.endArr();
    j.key("events"); j.beginArr();
    for (const GameEvent& e : evs) {
        j.beginObj(); j.kv("type", static_cast<int>(e.type)); j.kv("t", e.time); j.kv("uav", e.uav); j.kv("track", e.track); j.kv("value", e.value);
        j.vec("pos", {e.position.x, e.position.y, e.position.z}); j.key("combined"); j.boolean(e.combined); j.endObj();
    }
    j.endArr();
    const MissionState& m = s.mission().state();
    j.kv("kills", m.kills); j.kv("combined", m.combinedKills); j.kv("score", m.score); j.kv("wave", m.wave); j.kv("fused", s.squad().tracker().fusedUpdates());
    j.endObj();
    save("squad_run.json", j);
}

// v6 WP-D: CV under sensor noise. Synthetic frames (makeSyntheticFrame: rover 0.86 / core 0.97, soldier 0.80-0.82,
// sunlit patch 0.62) through the full sensor model (after NUC) at NETD 0, 1, 2, 4, 8 DN -> P_d, false targets per
// frame, soldier-as-target rate, person recognition, centroid RMS. Same seeds every run (reproducible figure).
struct NoisePoint {
    double pd = 0, pdFar = 0, falsePerFrame = 0, personAsTarget = 0, personRecognised = 0, centroidRms = 0;
};
NoisePoint cvNoisePoint(float netd, bool calibrated, int frames) {
    const int W = 320, H = 240;
    SensorNoiseConfig cfg;
    cfg.netdDn = netd;
    SensorModel near(9100, cfg), far(9200, cfg), empty(9300, cfg);
    if (calibrated) { near.nuc(); far.nuc(); empty.nuc(); }
    Pcg32 r(9400, 3);
    auto spot = [&](float& x, float& y) { x = 16 + r.uniform() * (W - 32); y = 16 + r.uniform() * (H - 32); };
    NoisePoint p;
    int det = 0, detFar = 0, falseT = 0, pAsT = 0, pRec = 0;
    double err2 = 0;
    for (int f = 0; f < frames; ++f) {
        float rx, ry, sx, sy;
        spot(rx, ry);
        do spot(sx, sy); while (std::hypot(sx - rx, sy - ry) < 25.0f);
        // rover + soldier
        std::vector<uint8_t> img = makeSyntheticFrame(W, H, 20000 + f, true, rx, ry, true, sx, sy);
        near.apply(img, W, H);
        float best = 1e9f;
        bool st = false, sp = false;
        for (const Measurement2D& m : cv::detectAll(img, W, H)) {
            const float dr = std::hypot(m.cx - rx, m.cy - ry), ds = std::hypot(m.cx - sx, m.cy - sy);
            if (m.cls == BlobClass::Target && dr < best) best = dr;
            if (ds < 5.0f && m.cls == BlobClass::Target) st = true;
            if (ds < 5.0f && m.cls == BlobClass::Person) sp = true;
        }
        if (best < 3.0f) { ++det; err2 += best * best; }
        pAsT += st; pRec += sp;
        // far rover (half size) alone
        std::vector<uint8_t> img2 = makeSyntheticFrame(W, H, 30000 + f, true, rx, ry, false, 0, 0, 0.5f);
        far.apply(img2, W, H);
        for (const Measurement2D& m : cv::detectTargets(img2, W, H))
            if (std::hypot(m.cx - rx, m.cy - ry) < 3.0f) { ++detFar; break; }
        // no rover: soldier in half of the frames
        std::vector<uint8_t> img3 = makeSyntheticFrame(W, H, 40000 + f, false, 0, 0, f % 2 == 0, sx, sy);
        empty.apply(img3, W, H);
        falseT += static_cast<int>(cv::detectTargets(img3, W, H).size());
    }
    p.pd = double(det) / frames; p.pdFar = double(detFar) / frames; p.falsePerFrame = double(falseT) / frames;
    p.personAsTarget = double(pAsT) / frames; p.personRecognised = double(pRec) / frames;
    p.centroidRms = det ? std::sqrt(err2 / det) : NAN;
    return p;
}

void dumpCvNoise() {
    const int frames = 500;
    const float netds[] = {0.0f, 1.0f, 2.0f, 4.0f, 8.0f};
    J j;
    j.beginObj();
    j.kv("frames", frames);
    std::vector<double> nd, pd, pdFar, fa, pat, prec, rms;
    for (float n : netds) {
        const NoisePoint p = cvNoisePoint(n, true, frames);
        nd.push_back(n); pd.push_back(p.pd); pdFar.push_back(p.pdFar); fa.push_back(p.falsePerFrame);
        pat.push_back(p.personAsTarget); prec.push_back(p.personRecognised); rms.push_back(p.centroidRms);
        std::printf("NETD %.0f DN: P_d %.3f, P_d far %.3f, false/frame %.4f, person->target %.4f, person recognised %.3f, "
                    "centroid RMS %.3f px\n", n, p.pd, p.pdFar, p.falsePerFrame, p.personAsTarget, p.personRecognised, p.centroidRms);
    }
    j.vec("netd", nd); j.vec("pd", pd); j.vec("pdFar", pdFar); j.vec("falsePerFrame", fa);
    j.vec("personAsTarget", pat); j.vec("personRecognised", prec); j.vec("centroidRms", rms);
    // the same at the default NETD on an uncalibrated sensor (no NUC: full fixed pattern, stuck pixels not mapped)
    const NoisePoint raw = cvNoisePoint(2.0f, false, frames);
    std::printf("no NUC, NETD 2 DN: P_d %.3f, false/frame %.4f, person->target %.4f\n", raw.pd, raw.falsePerFrame, raw.personAsTarget);
    j.key("noNuc"); j.beginObj();
    j.kv("netd", 2.0); j.kv("pd", raw.pd); j.kv("pdFar", raw.pdFar); j.kv("falsePerFrame", raw.falsePerFrame);
    j.kv("personAsTarget", raw.personAsTarget); j.kv("personRecognised", raw.personRecognised); j.kv("centroidRms", raw.centroidRms);
    j.endObj();
    const SensorNoiseConfig c;
    j.key("config"); j.beginObj();
    j.kv("netdDn", c.netdDn); j.kv("fpnGainSigma", c.fpnGainSigma); j.kv("fpnOffsetSigma", c.fpnOffsetSigma);
    j.kv("columnSigma", c.columnSigma); j.kv("jitterDeg", c.jitterDeg); j.kv("jitterPx", c.jitterDeg / (kCvFovYDeg / kCvHeight));
    j.kv("dropProb", c.dropProb); j.kv("stuckFrac", c.stuckFrac); j.kv("nucResidual", c.nucResidual);
    j.endObj();
    // one example frame per NETD level (rover + soldier, same scene) for the figure
    j.key("examples"); j.beginArr();
    const std::vector<uint8_t> scene = makeSyntheticFrame(320, 240, 77, true, 120, 110, true, 200, 130);
    for (float n : netds) {
        SensorNoiseConfig cfg;
        cfg.netdDn = n;
        SensorModel s(9500, cfg);
        s.nuc();
        std::vector<uint8_t> img = scene;
        s.apply(img, 320, 240);
        j.beginObj();
        j.kv("netd", n);
        j.key("crop"); j.beginArr();  // 140 x 80 crop around both blobs
        for (int y = 80; y < 160; ++y) for (int x = 90; x < 230; ++x) j.num(img[y * 320 + x]);
        j.endArr();
        j.key("blobs"); j.beginArr();
        for (const Measurement2D& m : cv::detectAll(img, 320, 240)) {
            j.beginObj(); j.kv("cx", m.cx); j.kv("cy", m.cy); j.kv("area", m.area); j.kvs("cls", blobClassName(m.cls)); j.endObj();
        }
        j.endArr();
        j.endObj();
    }
    j.endArr();
    j.kv("cropX", 90); j.kv("cropY", 80); j.kv("cropW", 140); j.kv("cropH", 80);
    j.endObj();
    save("cv_noise.json", j);
}

void dumpFlight() {
    J j;
    j.beginObj();
    std::vector<double> d, model, sim;
    for (float x = 2.0f; x <= 30.01f; x += 1.0f) {
        Combat c;
        const Vec3 a{0, 10, 0}, b{x, 10, 0};
        c.setDesignator(1, b, true);
        c.fireMissile(a, {1, 0, 0}, 1, 0);
        float t = 0.0f;
        bool hit = false;
        for (; t < 4.0f && !hit; t += 1.0f / 240.0f) {
            c.update(1.0f / 240.0f, t);
            hit = !c.takeBlasts().empty();
        }
        d.push_back(x); model.push_back(roundFlightTime(x)); sim.push_back(hit ? t : NAN);
    }
    j.vec("d", d); j.vec("model", model); j.vec("sim", sim);
    j.kv("rampTime", Combat::kMissileRampTime); j.kv("vmax", Combat::kMissileMaxSpeed); j.kv("fuse", Combat::kMissileFuse);
    j.endObj();
    save("flight.json", j);
}

void dumpMission() {
    Mission m;
    J j;
    j.beginObj();
    std::vector<double> waves, sizes;
    std::vector<SpawnRequest> sp;
    std::vector<GameEvent> ev;
    float time = 0;
    int curWave = 0, spawnsThisWave = 0;
    for (int step = 0; step < 100000 && curWave < 9; ++step) {
        sp.clear(); ev.clear();
        time += 0.25f;
        m.update(0.25f, 0, 6, sp, ev, time);
        for (auto& e : ev) if (e.type == GameEventType::WaveStart) { if (curWave > 0) { waves.push_back(curWave); sizes.push_back(spawnsThisWave); } curWave = int(e.value); spawnsThisWave = 0; }
        for (auto& r : sp) { ++spawnsThisWave; std::vector<GameEvent> e2; m.onKill(false, r.position, e2, time); }
    }
    j.vec("wave", waves); j.vec("size", sizes);
    j.kv("maxRovers", layout::kMaxRovers); j.kv("spawnInterval", kSpawnInterval); j.kv("waveBreak", kWaveBreak);
    j.endObj();
    save("mission.json", j);
}

}  // namespace

int main(int argc, char** argv) {
    gOut = argc > 1 ? argv[1] : "build/figdata_out";
    std::string cmd = "mkdir -p " + gOut;
    if (std::system(cmd.c_str()) != 0) return 1;
    const std::string only = argc > 2 ? argv[2] : "";
    auto want = [&](const char* n) { return only.empty() || only == n; };
    if (want("prim")) dumpPrimitives();
    if (want("nodes") || want("micro") || want("drive")) {}
    if (want("cv")) dumpCv();
    if (want("cvnoise")) dumpCvNoise();
    if (want("kalman")) { uint32_t seed = 4242; while (!dumpKalman(seed, false) && seed < 6000) ++seed; std::printf("kalman seed %u\n", seed); dumpKalman(seed, true); dumpAssociation(); dumpTriangulation(); }
    if (want("flight")) { dumpFlight(); dumpMission(); }
    if (want("nodes") || want("micro")) {
        Scene s;
        if (want("nodes")) dumpNodes(s);
        if (want("micro")) dumpMicromouse(s);
    }
    if (want("drive")) dumpDrive();
    if (want("squad")) dumpSquad();
    return 0;
}
