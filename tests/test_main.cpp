// ThermalSense automated tests (no OpenGL context needed).
// Every check records: id, description, expected, observed, PASS/FAIL. Results are printed and written to
// build/test_results.json, which tools/build_workbook.py copies into the "Testing" sheet.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "ai/DroneController.h"
#include "ai/HideController.h"
#include "ai/Occlusion.h"
#include "objects/Terrain.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "math/ShadowMath.h"
#include "objects/Backdrop.h"
#include "primitives/Sphere.h"
#include "scene/Camera.h"
#include "scene/Scene.h"
#include "vision/ThermalTracker.h"
#include "TestKit.h"

using namespace ts;

namespace {

using testkit::fmt;
using testkit::record;
using testkit::Result;
std::vector<Result>& g_results = testkit::results();

struct Bounds {
    Vec3 min{1e9f, 1e9f, 1e9f}, max{-1e9f, -1e9f, -1e9f};
};
Bounds boundsOf(const MeshData& m) {
    Bounds b;
    for (const Vertex& v : m.vertices) {
        b.min = {std::min(b.min.x, v.position.x), std::min(b.min.y, v.position.y), std::min(b.min.z, v.position.z)};
        b.max = {std::max(b.max.x, v.position.x), std::max(b.max.y, v.position.y), std::max(b.max.z, v.position.z)};
    }
    return b;
}

// Counts triangles whose geometric normal (CCW) disagrees with the stored vertex normals,
// i.e. triangles that would be back-face culled when seen from outside.
int wrongWinding(const MeshData& m) {
    int bad = 0;
    for (size_t t = 0; t < m.indices.size(); t += 3) {
        const Vertex& a = m.vertices[m.indices[t]];
        const Vertex& b = m.vertices[m.indices[t + 1]];
        const Vertex& c = m.vertices[m.indices[t + 2]];
        const Vec3 geo = cross(b.position - a.position, c.position - a.position);
        const Vec3 avgN = a.normal + b.normal + c.normal;
        if (dot(geo, avgN) <= 0.0f) ++bad;
    }
    return bad;
}

int degenerate(const MeshData& m) {
    int bad = 0;
    for (size_t t = 0; t < m.indices.size(); t += 3) {
        const Vec3 a = m.vertices[m.indices[t]].position, b = m.vertices[m.indices[t + 1]].position,
                   c = m.vertices[m.indices[t + 2]].position;
        if (length(cross(b - a, c - a)) < 1e-9f) ++bad;
    }
    return bad;
}

bool indicesInRange(const MeshData& m) {
    for (uint32_t i : m.indices)
        if (i >= m.vertices.size()) return false;
    return m.indices.size() % 3 == 0;
}

float maxNormalError(const MeshData& m) {
    float e = 0.0f;
    for (const Vertex& v : m.vertices) e = std::max(e, std::fabs(length(v.normal) - 1.0f));
    return e;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// ---------------------------------------------------------------- primitives
void testPrimitives() {
    const MeshData cube = createCube();
    record("T01", "Cube vertex/index count", "24 vertices, 36 indices, 12 triangles",
           fmt("%zu vertices, %zu indices, %zu triangles", cube.vertexCount(), cube.indexCount(), cube.triangleCount()),
           cube.vertexCount() == 24 && cube.indexCount() == 36);
    Bounds b = boundsOf(cube);
    record("T02", "Cube centred at origin with unit size", "min (-0.5,-0.5,-0.5), max (0.5,0.5,0.5)",
           fmt("min (%.3f,%.3f,%.3f), max (%.3f,%.3f,%.3f)", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z),
           near(b.min.x, -0.5f) && near(b.min.y, -0.5f) && near(b.min.z, -0.5f) && near(b.max.x, 0.5f) &&
               near(b.max.y, 0.5f) && near(b.max.z, 0.5f));
    int outward = 0;
    for (const Vertex& v : cube.vertices) outward += dot(v.position, v.normal) > 0.0f ? 1 : 0;
    record("T03", "Cube normals unit length, outward, CCW winding", "0 wrong-winding triangles, 24/24 outward normals",
           fmt("%d wrong-winding, %d/24 outward, max |n|-1 = %.2e", wrongWinding(cube), outward, maxNormalError(cube)),
           wrongWinding(cube) == 0 && outward == 24 && maxNormalError(cube) < 1e-5f);

    const MeshData sphere = createSphere(36, 18);
    record("T04", "Sphere counts follow 2+(S-1)N vertices and 2N(S-1) triangles (N=36, S=18)",
           "614 vertices, 1224 triangles",
           fmt("%zu vertices, %zu triangles", sphere.vertexCount(), sphere.triangleCount()),
           sphere.vertexCount() == 614 && sphere.triangleCount() == 1224);
    float radiusErr = 0.0f, normalErr = 0.0f;
    for (const Vertex& v : sphere.vertices) {
        radiusErr = std::max(radiusErr, std::fabs(length(v.position) - 0.5f));
        normalErr = std::max(normalErr, length(v.normal - v.position * 2.0f));
    }
    record("T05", "Sphere vertices lie on r = 0.5 and normal = p / r", "errors < 1e-5",
           fmt("max radius error %.2e, max normal error %.2e", radiusErr, normalErr),
           radiusErr < 1e-5f && normalErr < 1e-5f);
    int mirrored = 0;
    for (const Vertex& v : sphere.vertices) {
        for (const Vertex& w : sphere.vertices) {
            if (near(v.position.x, w.position.x, 1e-5f) && near(v.position.y, -w.position.y, 1e-5f) &&
                near(v.position.z, w.position.z, 1e-5f)) {
                ++mirrored;
                break;
            }
        }
    }
    record("T06", "Sphere lower hemisphere is the reflection (y -> -y) of the upper one, CCW kept",
           "every vertex has a mirror partner; 0 wrong-winding triangles",
           fmt("%d/%zu vertices mirrored, %d wrong-winding", mirrored, sphere.vertexCount(), wrongWinding(sphere)),
           mirrored == static_cast<int>(sphere.vertexCount()) && wrongWinding(sphere) == 0);

    const MeshData cyl = createCylinder(36);
    b = boundsOf(cyl);
    record("T07", "Cylinder counts 4N+2 vertices, 12N indices (N=36)", "146 vertices, 432 indices",
           fmt("%zu vertices, %zu indices", cyl.vertexCount(), cyl.indexCount()),
           cyl.vertexCount() == 146 && cyl.indexCount() == 432);
    record("T08", "Cylinder centred at origin, axis along Z, radius 0.5, height 1",
           "x,y in [-0.5,0.5], z in [-0.5,0.5]",
           fmt("x [%.3f,%.3f] y [%.3f,%.3f] z [%.3f,%.3f]", b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z),
           near(b.min.x, -0.5f) && near(b.max.x, 0.5f) && near(b.min.y, -0.5f, 2e-3f) && near(b.max.y, 0.5f, 2e-3f) &&
               near(b.min.z, -0.5f) && near(b.max.z, 0.5f));
    record("T09", "Cylinder normals unit length and CCW outward winding", "0 wrong-winding triangles",
           fmt("%d wrong-winding, max |n|-1 = %.2e", wrongWinding(cyl), maxNormalError(cyl)),
           wrongWinding(cyl) == 0 && maxNormalError(cyl) < 1e-5f);
    const MeshData cone = createCylinder(36, 0.03f, 0.5f, false);
    record("T10", "Frustum variant (light cone): open, side normals tilted, CCW", "0 wrong-winding, normal.z < 0",
           fmt("%d wrong-winding, first normal z = %.3f", wrongWinding(cone), cone.vertices[0].normal.z),
           wrongWinding(cone) == 0 && cone.vertices[0].normal.z < 0.0f);

    bool allOk = true;
    int degen = 0;
    for (const MeshData* m : {&cube, &sphere, &cyl, &cone}) {
        allOk = allOk && indicesInRange(*m);
        degen += degenerate(*m);
    }
    record("T11", "All indices valid and no degenerate triangles", "indices < vertex count, 0 degenerate",
           fmt("indices valid: %s, degenerate: %d", allOk ? "yes" : "no", degen), allOk && degen == 0);
    record("T12", "Indexed storage smaller than non-indexed for every primitive", "indexed < non-indexed",
           fmt("cube %zu<%zu, sphere %zu<%zu, cylinder %zu<%zu B", cube.indexedBytes(), cube.nonIndexedBytes(),
               sphere.indexedBytes(), sphere.nonIndexedBytes(), cyl.indexedBytes(), cyl.nonIndexedBytes()),
           cube.indexedBytes() < cube.nonIndexedBytes() && sphere.indexedBytes() < sphere.nonIndexedBytes() &&
               cyl.indexedBytes() < cyl.nonIndexedBytes());

    const MeshData disc = createTerrainDisc();
    b = boundsOf(disc);
    int upFacing = 0;
    for (size_t t = 0; t < disc.indices.size(); t += 3) {
        const Vec3 a = disc.vertices[disc.indices[t]].position, bb = disc.vertices[disc.indices[t + 1]].position,
                   c = disc.vertices[disc.indices[t + 2]].position;
        upFacing += cross(bb - a, c - a).y > 0.0f ? 1 : 0;
    }
    const size_t expV = 1 + kTerrainRings * kTerrainSlices, expT = kTerrainSlices * (2 * kTerrainRings - 1);
    record("T13", "Terrain disc: radius 20 m, 1+R*N vertices, N(2R-1) triangles, all facing +Y",
           fmt("%zu vertices, %zu triangles, radius 20.00, all up", expV, expT),
           fmt("%zu vertices, %zu triangles, radius %.2f, %d/%zu up", disc.vertexCount(), disc.triangleCount(),
               b.max.x, upFacing, disc.triangleCount()),
           disc.vertexCount() == expV && disc.triangleCount() == expT && near(b.max.x, kTerrainRadius, 1e-3f) &&
               upFacing == static_cast<int>(disc.triangleCount()));
}

// ---------------------------------------------------------------- transformations
void testMath() {
    const Mat4 m = translate({1, 2, 3}) * rotateZ(radians(90)) * scale({2, 2, 2});
    const Vec3 p = transformPoint(m, {1, 0, 0});
    record("T14", "Model matrix order M = T*R*S (scale, then rotate, then translate)",
           "(1,0,0) -> S(2) -> (2,0,0) -> Rz(90) -> (0,2,0) -> T -> (1,4,3)", fmt("(%.3f, %.3f, %.3f)", p.x, p.y, p.z),
           near(p.x, 1) && near(p.y, 4) && near(p.z, 3));
    const Mat4 a = translate({3, -1, 2}) * rotateEulerYXZ({20, 35, -10}) * scale({1, 2, 0.5f});
    const Mat4 id = inverse(a) * a;
    float err = 0.0f;
    for (int i = 0; i < 16; ++i) err = std::max(err, std::fabs(id.m[i] - Mat4::identity().m[i]));
    record("T15", "Inverse matrix: inverse(M) * M = I", "max error < 1e-5", fmt("max error %.2e", err), err < 1e-5f);
    const Mat4 s = scale({4, 1, 1});
    const Mat3 nm = normalMatrix(s);
    const Vec3 tangent = transformDirection(s, normalize(Vec3{1, -1, 0}));
    const Vec3 n{nm.m[0] * 0.7071f + nm.m[3] * 0.7071f, nm.m[1] * 0.7071f + nm.m[4] * 0.7071f, 0.0f};
    record("T16", "Normal matrix keeps normals perpendicular under non-uniform scale",
           "dot(transformed normal, transformed tangent) = 0", fmt("dot = %.2e", dot(normalize(n), normalize(tangent))),
           std::fabs(dot(normalize(n), normalize(tangent))) < 1e-5f);
    const Mat4 vp = perspective(radians(50), 16.0f / 9.0f, 0.05f, 500.0f) * lookAt({0, 3, 10}, {0, 1, 0}, {0, 1, 0});
    const Vec4 c = vp * Vec4{0, 1, 0, 1};
    record("T17", "View-projection maps the look-at target to the NDC centre", "ndc (0, 0, z) with z in [-1,1]",
           fmt("ndc (%.3f, %.3f, %.3f)", c.x / c.w, c.y / c.w, c.z / c.w),
           near(c.x / c.w, 0) && near(c.y / c.w, 0) && std::fabs(c.z / c.w) < 1.0f);
}

// ---------------------------------------------------------------- scene graph
void testScene() {
    Scene scene;
    std::map<std::string, std::set<std::string>> prims;
    std::map<std::string, int> count;
    bool finite = true;
    for (const SceneNode* n : scene.leaves()) {
        prims[n->object].insert(primitiveName(n->primitive));
        ++count[n->object];
        for (float v : n->world.m) finite = finite && std::isfinite(v);
    }
    auto has = [&](const std::string& o, const char* p) { return prims[o].count(p) > 0; };
    record("T18", "Bunker, UAV and Rover are each built from cube + sphere + cylinder",
           "all three composites use all three primitives",
           fmt("bunker %d parts, UAV %d parts, rover %d parts", count["Command Bunker"], count["Autonomous UAV"],
               count["Target Rover"]),
           has("Command Bunker", "Cube") && has("Command Bunker", "Sphere") && has("Command Bunker", "Cylinder") &&
               has("Autonomous UAV", "Cube") && has("Autonomous UAV", "Sphere") && has("Autonomous UAV", "Cylinder") &&
               has("Target Rover", "Cube") && has("Target Rover", "Sphere") && has("Target Rover", "Cylinder"));
    bool shadingOk = true;
    for (const SceneNode* n : scene.leaves()) {
        if (n->object == "Command Bunker") shadingOk = shadingOk && n->shading == ShadingModel::Flat;
        if (n->object == "Terrain") shadingOk = shadingOk && n->shading == ShadingModel::Gouraud;
        if (n->object == "Autonomous UAV" && n->primitive != PrimitiveType::BeamCone)
            shadingOk = shadingOk && n->shading == ShadingModel::Phong;
    }
    record("T19", "Shading assignment follows the proposal", "Bunker=Flat, Terrain=Gouraud, UAV=Phong",
           shadingOk ? "all leaves match" : "mismatch found", shadingOk);
    record("T20", "All world matrices finite after scene construction", "no NaN/Inf", finite ? "all finite" : "NaN found",
           finite);

    // Hierarchy: the lamp must move with the UAV body.
    const Vec3 before = transformPoint(scene.cameraWorld(), {0, 0, 0});
    const Vec3 bodyBefore = scene.drone.position;
    Detection none;
    scene.update(0.5f, none);
    const Vec3 after = transformPoint(scene.cameraWorld(), {0, 0, 0});
    const Vec3 bodyAfter = scene.drone.position;
    const float lampMove = length(after - before), bodyMove = length(bodyAfter - bodyBefore);
    record("T21", "Searchlight is hierarchical: lamp moves with the UAV body", "lamp displacement follows body",
           fmt("body moved %.3f m, lamp moved %.3f m", bodyMove, lampMove), lampMove > 0.5f * bodyMove && bodyMove > 0.0f);
}

// ---------------------------------------------------------------- computer vision
void testTracker() {
    const int w = 40, h = 30;
    std::vector<uint8_t> img(w * h, 40);
    for (int y = 10; y < 14; ++y)
        for (int x = 20; x < 26; ++x) img[y * w + x] = 240;
    ThermalTracker tracker;
    Detection d = tracker.process(img, w, h);
    const float expU = (22.5f + 0.5f) / w * 2 - 1, expV = 1 - (11.5f + 0.5f) / h * 2;
    record("T22", "Tracker finds a hot blob and its centroid", fmt("found, area 24, u=%.3f v=%.3f", expU, expV),
           fmt("found=%d, area %d, u=%.3f v=%.3f", d.found, d.area, d.u, d.v),
           d.found && d.area == 24 && near(d.u, expU) && near(d.v, expV));

    std::vector<uint8_t> noise(w * h, 40);
    noise[5 * w + 5] = 250;  // single hot pixel = sensor noise
    ThermalTracker t2;
    d = t2.process(noise, w, h);
    record("T23", "Tracker ignores single-pixel noise (min area 2)", "not found", d.found ? "found" : "not found",
           !d.found);

    std::vector<uint8_t> two(w * h, 40);
    for (int y = 2; y < 5; ++y)
        for (int x = 2; x < 5; ++x) two[y * w + x] = 230;     // small blob top-left
    for (int y = 20; y < 26; ++y)
        for (int x = 30; x < 38; ++x) two[y * w + x] = 250;   // big blob bottom-right
    ThermalTracker t3;
    std::vector<uint8_t> first(w * h, 40);
    for (int y = 2; y < 5; ++y)
        for (int x = 3; x < 6; ++x) first[y * w + x] = 230;
    t3.process(first, w, h);            // track established top-left
    d = t3.process(two, w, h);          // big distractor appears far away
    record("T24", "Data association keeps the existing track instead of a bigger distractor",
           "selected blob is the top-left one (u < 0, v > 0), 2 blobs seen",
           fmt("u=%.2f v=%.2f, blobs=%d", d.u, d.v, d.blobCount), d.found && d.u < 0 && d.v > 0 && d.blobCount == 2);
}

// ---------------------------------------------------------------- hide logic (MicroMouse + LFR)
void testTrackerCore() {
    const int w = 40, h = 30;
    std::vector<uint8_t> img(w * h, 40);
    for (int y = 5; y < 12; ++y)
        for (int x = 5; x < 14; ++x) img[y * w + x] = 215;   // big warm object (0.84), no hot core
    for (int y = 20; y < 23; ++y)
        for (int x = 30; x < 33; ++x) img[y * w + x] = 245;  // small hot engine (0.96)
    ThermalTracker t;
    const Detection d = t.process(img, w, h);
    record("T32", "Hysteresis threshold: a big warm blob without a hot core is not a target",
           "2 blobs seen, the small hot one (u > 0, v < 0) selected",
           fmt("blobs=%d, found=%d, u=%.2f v=%.2f, area %d", d.blobCount, d.found, d.u, d.v, d.area),
           d.blobCount == 2 && d.found && d.u > 0 && d.v < 0 && d.area == 9);
}

void testHide() {
    const Box wall{{-1, 0, -1}, {1, 2, 1}};
    record("T25", "Segment/box occlusion test", "through: blocked, beside: clear",
           fmt("through=%d beside=%d", segmentHitsBox({0, 1, -5}, {0, 1, 5}, wall), segmentHitsBox({3, 1, -5}, {3, 1, 5}, wall)),
           segmentHitsBox({0, 1, -5}, {0, 1, 5}, wall) && !segmentHitsBox({3, 1, -5}, {3, 1, 5}, wall));

    // Exact primitive test: a ray through the corner of a sphere's bounding box misses the sphere itself.
    Occluder ball{PrimitiveType::Sphere, inverse(scale({2, 2, 2})), worldBounds(scale({2, 2, 2}))};
    Occluder can{PrimitiveType::Cylinder, inverse(rotateX(radians(90)) * scale({2, 2, 2})),
                 worldBounds(rotateX(radians(90)) * scale({2, 2, 2}))};
    const bool cornerSphere = segmentHitsPrimitive({-5, 0.9f, 0.9f}, {5, 0.9f, 0.9f}, ball);
    const bool centreSphere = segmentHitsPrimitive({-5, 0.2f, 0.1f}, {5, 0.2f, 0.1f}, ball);
    const bool cornerCyl = segmentHitsPrimitive({-5, 0.0f, 6.6f}, {5, 0.0f, -3.4f}, can);  // line x + z = 1.6
    const bool throughCyl = segmentHitsPrimitive({-5, 0.5f, 0.3f}, {5, 0.5f, 0.3f}, can);
    record("T31", "Exact occlusion in primitive space (inverse model matrix), not just bounding boxes",
           "sphere: corner miss, centre hit; cylinder: corner miss, body hit",
           fmt("sphere corner=%d centre=%d, cylinder corner=%d body=%d", cornerSphere, centreSphere, cornerCyl, throughCyl),
           !cornerSphere && centreSphere && !cornerCyl && throughCyl);

    Scene scene;  // gives the real occluders through the rover controller
    HideController hc;
    std::vector<Occluder> occ;
    for (SceneNode* r : scene.roots())
        if (r->object == "Command Bunker" || r->object == "Environment") collectOccluders(*r, occ);
    hc.setStaticOccluders(occ);
    const Vec3 drone{0.0f, 4.0f, 8.0f};
    hc.computeVisibility(drone);
    int bx, bz, fx, fz;
    HideController::worldToCell({0, 0, -2.2f}, bx, bz);
    HideController::worldToCell({0, 0, 5.5f}, fx, fz);
    record("T26", "Visibility grid: behind the bunker is hidden, in front is visible (UAV at +Z)",
           "cell (0,-2.2) hidden, cell (0,5.5) visible",
           fmt("behind hidden=%d, front hidden=%d", hc.cellHidden(bx, bz), hc.cellHidden(fx, fz)),
           hc.cellHidden(bx, bz) && !hc.cellHidden(fx, fz));

    hc.floodFill();
    bool bfsOk = true;
    int goals = 0;
    for (int z = 0; z < kGridCells; ++z) {
        for (int x = 0; x < kGridCells; ++x) {
            if (!hc.cellFree(x, z) || hc.floodValue(x, z) < 0) continue;
            if (hc.floodValue(x, z) == 0) ++goals;
            const int nx[4] = {x + 1, x - 1, x, x}, nz[4] = {z, z, z + 1, z - 1};
            for (int k = 0; k < 4; ++k)
                if (hc.cellFree(nx[k], nz[k]) && hc.floodValue(nx[k], nz[k]) >= 0)
                    bfsOk = bfsOk && std::abs(hc.floodValue(nx[k], nz[k]) - hc.floodValue(x, z)) <= 1;
        }
    }
    int sx, sz;
    HideController::worldToCell({6.0f, 0, 6.0f}, sx, sz);
    const std::vector<Vec3> path = hc.descend(sx, sz);
    int ex = sx, ez = sz;
    if (!path.empty()) HideController::worldToCell(path.back(), ex, ez);
    record("T27", "Flood fill is a valid BFS field and descent reaches a goal (MicroMouse)",
           "neighbours differ by <= 1; path ends on a value-0 cell",
           fmt("goals %d, field consistent %d, start value %d, path %zu cells, end value %d", goals, bfsOk,
               hc.floodValue(sx, sz), path.size(), hc.floodValue(ex, ez)),
           bfsOk && goals > 0 && !path.empty() && hc.floodValue(ex, ez) == 0 &&
               static_cast<int>(path.size()) == hc.floodValue(sx, sz));

    RoverState rover;
    rover.position = {-1.0f, terrainHeight(-1.0f, 6.0f), 6.0f};  // open ground in front of the bunker
    rover.yaw = 3.4f;
    float travelled = 0.0f;
    int sensorFrames = 0, frames = 0;
    for (float t = 0.0f; t < 60.0f; t += 1.0f / 60.0f) {
        const Vec3 prev = rover.position;
        hc.update(1.0f / 60.0f, drone, rover);
        travelled += length(rover.position - prev);
        bool any = false;
        for (bool s : rover.sensors) any = any || s;
        if (rover.moving) {
            ++frames;
            sensorFrames += any ? 1 : 0;
        }
    }
    record("T28", "Rover drives (LFR controller) from open ground into the UAV's blind spot within 60 s",
           "rover hidden at the end",
           fmt("hidden=%d at (%.2f, %.2f), travelled %.1f m, line under sensors %d/%d moving frames", rover.hidden,
               rover.position.x, rover.position.z, travelled, sensorFrames, frames),
           rover.hidden && travelled > 1.0f);
}

void testDrone() {
    // Camera 4 m above flat ground (origin pad), pitched straight down: the centre pixel must hit below it.
    const Mat4 cam = translate({0.5f, 4.0f, 0.5f}) * rotateX(radians(90));
    Vec3 hit;
    const bool ok = DroneController::imagePointToGround(0, 0, cam, hit);
    record("T29", "Pixel-ray ground estimate: image centre of a downward camera hits the point below it",
           "hit near (0.5, 0.0, 0.5)", fmt("hit=%d at (%.2f, %.2f, %.2f)", ok, hit.x, hit.y, hit.z),
           ok && near(hit.x, 0.5f, 0.06f) && near(hit.z, 0.5f, 0.06f) && std::fabs(hit.y) < 0.06f);

    Scene scene;
    Detection none;
    bool inside = true, finite = true;
    for (int i = 0; i < 60 * 40; ++i) {
        scene.update(1.0f / 60.0f, none);
        const Vec3 p = scene.drone.position;
        inside = inside && std::sqrt(p.x * p.x + p.z * p.z) <= 19.51f && p.y >= terrainHeight(p.x, p.z) + 1.99f;
        finite = finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    record("T30", "40 s simulation without detections: UAV searches inside the 20 m area, above ground",
           "mode SEARCH, always inside r <= 19.5 m and >= 2 m above ground",
           fmt("mode %s, inside=%d, finite=%d", droneModeName(scene.drone.mode), inside, finite),
           inside && finite && scene.drone.mode == DroneMode::Search);
}

// ---------------------------------------------------------------- shadow mapping
Vec3 ndcOf(const Mat4& vp, const Vec3& p) {
    const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1.0f};
    return {c.x / c.w, c.y / c.w, c.z / c.w};
}

void testShadows() {
    const Vec3 moon = normalize(Vec3{-0.55f, -0.38f, 0.72f});
    const Sphere world{{0, 1, 0}, 22.0f};
    const LightMatrix m = fitDirectionalShadow(moon, world, 2048, 10.0f);
    // Sample the bounding sphere surface: everything must land inside the shadow map's clip volume.
    float worst = 0.0f;
    for (int i = 0; i < 64; ++i) {
        for (int j = 0; j <= 16; ++j) {
            const float th = 2.0f * PI * i / 64.0f, ph = PI * j / 16.0f;
            const Vec3 p = world.centre + Vec3{std::sin(ph) * std::cos(th), std::cos(ph), std::sin(ph) * std::sin(th)} * world.radius;
            const Vec3 n = ndcOf(m.viewProj, p);
            worst = std::max(worst, std::max(std::fabs(n.x), std::max(std::fabs(n.y), std::fabs(n.z))));
        }
    }
    record("T33", "Moon shadow map: the whole 20 m scene sphere projects inside the light's clip volume",
           "max |NDC| <= 1.0 (texel " + fmt("%.1f", 2 * 22.0f / 2048 * 100) + " cm)",
           fmt("max |NDC| = %.3f, texel = %.2f cm", worst, m.texelWorld * 100.0f), worst <= 1.0001f);

    // Texel snapping: a small camera move shifts a fixed world point by a whole number of texels (no shimmer).
    const LightMatrix a = fitDirectionalShadow(moon, {{0.00f, 1.0f, 0.00f}, 6.0f}, 2048, 10.0f);
    const LightMatrix b = fitDirectionalShadow(moon, {{0.013f, 1.0f, 0.021f}, 6.0f}, 2048, 10.0f);
    const Vec3 probe{1.3f, 0.2f, -2.1f};
    const float dx = (ndcOf(b.viewProj, probe).x - ndcOf(a.viewProj, probe).x) * 1024.0f;
    const float dy = (ndcOf(b.viewProj, probe).y - ndcOf(a.viewProj, probe).y) * 1024.0f;
    const float frac = std::max(std::fabs(dx - std::round(dx)), std::fabs(dy - std::round(dy)));
    record("T34", "Shadow cascade snapping: moving the camera shifts the shadow map by whole texels only",
           "fractional texel shift < 0.01", fmt("shift (%.3f, %.3f) texels, fraction %.4f", dx, dy, frac), frac < 0.01f);

    // Spot shadow: a point on the lamp axis maps to the centre, a point on the outer cone stays inside.
    const Vec3 pos{2, 4, 1}, dir = normalize(Vec3{0.3f, -1.0f, 0.2f});
    const LightMatrix s = spotShadowMatrix(pos, dir, 14.0f, 0.05f, 30.0f);
    const Vec3 onAxis = ndcOf(s.viewProj, pos + dir * 5.0f);
    const Vec3 side = normalize(cross(dir, Vec3{0, 1, 0}));
    const Vec3 edgeDir = normalize(dir * std::cos(radians(14.0f)) + side * std::sin(radians(14.0f)));
    const Vec3 edge = ndcOf(s.viewProj, pos + edgeDir * 5.0f);
    record("T35", "Searchlight shadow matrix: lamp axis -> map centre, 14 deg cone edge inside the map",
           "axis at (0,0), |edge| < 1", fmt("axis (%.3f, %.3f), edge (%.3f, %.3f)", onAxis.x, onAxis.y, edge.x, edge.y),
           near(onAxis.x, 0, 1e-3f) && near(onAxis.y, 0, 1e-3f) && std::max(std::fabs(edge.x), std::fabs(edge.y)) < 1.0f);
}

// ---------------------------------------------------------------- 20 m world (v2): cone, props, terrain, agents
bool isLabPrimitive(PrimitiveType p) {
    return p == PrimitiveType::Cube || p == PrimitiveType::Sphere || p == PrimitiveType::Cylinder ||
           p == PrimitiveType::Cone;
}

// Every edge (by vertex position) is shared by exactly two triangles.
bool closedSurface(const MeshData& m) {
    using Key = std::tuple<long, long, long>;
    auto key = [](const Vec3& p) -> Key {
        return Key{std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f)};
    };
    std::map<std::pair<Key, Key>, int> edges;
    for (size_t t = 0; t < m.indices.size(); t += 3) {
        for (size_t e = 0; e < 3; ++e) {
            Key a = key(m.vertices[m.indices[t + e]].position), b = key(m.vertices[m.indices[t + (e + 1) % 3]].position);
            if (b < a) std::swap(a, b);
            ++edges[{a, b}];
        }
    }
    for (const auto& kv : edges)
        if (kv.second != 2) return false;
    return !edges.empty();
}

void testCone() {
    const int n = 36;
    const MeshData cone = createCylinder(n, 0.5f, 0.0f, true);
    const Bounds b = boundsOf(cone);
    record("T36", "Cone (cylinder generator, top radius 0): counts, centred, CCW, closed, no degenerate triangles",
           "109 vertices, 72 triangles, x,y,z in [-0.5,0.5], 0 wrong-winding, 0 degenerate, closed",
           fmt("%zu vertices, %zu triangles, x [%.3f,%.3f] y [%.3f,%.3f] z [%.3f,%.3f], %d wrong, %d degenerate, closed=%d",
               cone.vertexCount(), cone.triangleCount(), b.min.x, b.max.x, b.min.y, b.max.y, b.min.z, b.max.z,
               wrongWinding(cone), degenerate(cone), closedSurface(cone)),
           cone.vertexCount() == static_cast<size_t>(3 * n + 1) && cone.triangleCount() == static_cast<size_t>(2 * n) &&
               near(b.min.x, -0.5f) && near(b.max.x, 0.5f) && near(b.min.y, -0.5f, 2e-3f) && near(b.max.y, 0.5f, 2e-3f) &&
               near(b.min.z, -0.5f) && near(b.max.z, 0.5f) && wrongWinding(cone) == 0 && degenerate(cone) == 0 &&
               closedSurface(cone) && indicesInRange(cone) && maxNormalError(cone) < 1e-5f);

    // Exact cone occlusion: off-axis near the apex misses (inside the bounding box), through the axis/base hits.
    const Mat4 upright = translate({0, 1.0f, 0}) * rotateX(radians(-90)) * scale({2, 2, 2});
    const Occluder o{PrimitiveType::Cone, inverse(upright), worldBounds(upright)};
    const bool nearApex = segmentHitsPrimitive({-5, 1.9f, 0.3f}, {5, 1.9f, 0.3f}, o);  // radius only 0.1 m there
    const bool axis = segmentHitsPrimitive({-5, 0.5f, 0.0f}, {5, 0.5f, 0.0f}, o);
    const bool base = segmentHitsPrimitive({-5, 0.05f, 0.9f}, {5, 0.05f, 0.9f}, o);  // radius 0.95 at the base
    record("T37", "Exact cone occlusion in primitive space (pine crowns)", "near apex off-axis miss, axis hit, base hit",
           fmt("near apex=%d, axis=%d, base=%d", nearApex, axis, base), !nearApex && axis && base);
}

void testWorld() {
    Scene scene;
    std::map<std::string, int> count;
    std::map<std::string, std::set<std::string>> prims;
    bool labOnly = true, finite = true, pbr = true;
    std::string offender;
    for (const SceneNode* n : scene.leaves()) {
        ++count[n->object];
        prims[n->object].insert(primitiveName(n->primitive));
        for (float v : n->world.m) finite = finite && std::isfinite(v);
        if ((n->object == "Environment" || n->object == "Backdrop") && !isLabPrimitive(n->primitive)) labOnly = false;
        const Material& m = n->material;
        const bool ok = m.roughness >= 0.0f && m.roughness <= 1.0f && m.metalness >= 0.0f && m.metalness <= 1.0f;
        if (!ok && offender.empty()) offender = n->object + "/" + n->name;
        pbr = pbr && ok;
    }
    const int total = static_cast<int>(scene.leaves().size());
    record("T38", "New props and backdrop are built only from cube / sphere / cylinder / cone leaves",
           "Environment + Backdrop: only lab primitives, pines use Cone; total leaves < 1000 (draw-call budget)",
           fmt("environment %d parts {%s%s%s%s}, backdrop %d parts, scene total %d", count["Environment"],
               prims["Environment"].count("Cube") ? "Cube " : "", prims["Environment"].count("Sphere") ? "Sphere " : "",
               prims["Environment"].count("Cylinder") ? "Cylinder " : "", prims["Environment"].count("Cone") ? "Cone" : "",
               count["Backdrop"], total),
           labOnly && prims["Environment"].count("Cone") && prims["Backdrop"].count("Cone") && total < 1000);
    record("T39", "Every material has PBR parameters (roughness, metalness in [0,1]) and all matrices are finite",
           "all leaves: roughness/metalness set, no NaN/Inf", pbr && finite ? "all leaves OK" : ("first offender " + offender),
           pbr && finite);

    // Placement: props inside the disc, backdrop outside it.
    float worstInside = 0.0f, nearestBackdrop = 1e9f;
    for (const SceneNode* n : scene.leaves()) {
        if (n->name == "Moon" || n->name == "Far ground") continue;
        const Box b = worldBounds(n->world);
        if (n->object == "Environment") {
            // Extreme points of the unit primitive along its local axes (on the surface of a cube face centre,
            // sphere, cylinder or cone; a world AABB corner would overstate diagonal wires).
            const Vec3 axes[7] = {{0, 0, 0}, {0.5f, 0, 0}, {-0.5f, 0, 0}, {0, 0.5f, 0}, {0, -0.5f, 0}, {0, 0, 0.5f}, {0, 0, -0.5f}};
            for (const Vec3& a : axes) {
                const Vec3 p = transformPoint(n->world, a);
                worstInside = std::max(worstInside, std::sqrt(p.x * p.x + p.z * p.z));
            }
        } else if (n->object == "Backdrop") {
            const float cx = 0.5f * (b.min.x + b.max.x), cz = 0.5f * (b.min.z + b.max.z);
            nearestBackdrop = std::min(nearestBackdrop, std::sqrt(cx * cx + cz * cz));
        }
    }
    record("T40", "Props sit inside the 20 m disc, backdrop parts outside it",
           fmt("prop extents r <= %.1f m, backdrop centres r >= %.1f m", kTerrainRadius, kBackdropInnerRadius),
           fmt("farthest prop extent %.2f m, nearest backdrop part %.2f m", worstInside, nearestBackdrop),
           worstInside <= kTerrainRadius && nearestBackdrop >= kBackdropInnerRadius);

    // Thermal: nothing near the ground may outshine the rover's motor box or reach the tracker's blob threshold.
    float motorHeat = 0.0f;
    for (const SceneNode* n : scene.leaves())
        if (n->name == "Motor + battery box") motorHeat = n->material.heat;
    const float blobThreshold = TrackerConfig{}.threshold / 255.0f;
    float hottest = 0.0f;
    std::string hottestName;
    for (const SceneNode* n : scene.leaves()) {
        if (n->object == "Target Rover" || n->object == "Autonomous UAV") continue;
        const Box b = worldBounds(n->world);
        const float cx = 0.5f * (b.min.x + b.max.x), cz = 0.5f * (b.min.z + b.max.z);
        if (b.min.y > terrainHeight(cx, cz) + 3.0f) continue;  // only parts within 3 m of the ground
        if (n->material.heat > hottest) {
            hottest = n->material.heat;
            hottestName = n->object + "/" + n->name;
        }
    }
    record("T41", "No ground-level prop is hotter than the rover's motor box or reaches the tracker blob threshold",
           fmt("hottest < %.2f (blob threshold) < %.2f (motor box)", blobThreshold, motorHeat),
           fmt("hottest ground-level part %s = %.2f", hottestName.c_str(), hottest),
           hottest < blobThreshold && hottest < motorHeat);
}

void testTerrainV2() {
    // Continuity/smoothness, pad, creek, track, berm and rim.
    float maxStep = 0.0f, minH = 1e9f, maxPad = 0.0f;
    for (float x = -20.0f; x <= 20.0f; x += 0.1f) {
        for (float z = -20.0f; z <= 20.0f; z += 0.1f) {
            if (x * x + z * z > kTerrainRadius * kTerrainRadius) continue;
            const float h = terrainHeight(x, z);
            minH = std::min(minH, h);
            maxStep = std::max(maxStep, std::max(std::fabs(terrainHeight(x + 0.01f, z) - h),
                                                 std::fabs(terrainHeight(x, z + 0.01f) - h)));
            if (x * x + z * z < 3.5f * 3.5f) maxPad = std::max(maxPad, std::fabs(h));
        }
    }
    float rimSpread = 0.0f;
    for (int i = 0; i < 72; ++i) {
        const float a = radians(5.0f * i);
        rimSpread = std::max(rimSpread, std::fabs(terrainHeight(kTerrainRadius * std::cos(a), kTerrainRadius * std::sin(a)) -
                                                  terrainRimHeight()));
    }
    const float creek = terrainHeight(-6.0f, creekCentreZ(-6.0f)) - terrainHeight(-6.0f, creekCentreZ(-6.0f) + 3.0f);
    const float track = terrainHeight(trackCentreX(10.0f), 10.0f) - terrainHeight(trackCentreX(10.0f) + 1.8f, 10.0f);
    const float berm = terrainHeight(kBermRadius, 0.0f) - terrainHeight(15.0f, 0.0f);  // east side, open ground
    record("T42", "Terrain v2: continuous, level bunker pad, creek bed, track depression, berm, constant rim",
           "1 cm step < 6 mm (slope < 0.6), |h| < 1 mm on the pad, creek < 0, track < 0, berm > 0, rim spread < 1 mm",
           fmt("max 1 cm step %.4f m, pad %.5f m, creek %.2f m, track %.2f m, berm %+.2f m, rim spread %.5f m", maxStep,
               maxPad, creek, track, berm, rimSpread),
           maxStep < 0.006f && maxPad < 1e-3f && creek < -0.1f && track < -0.03f && berm > 0.1f && rimSpread < 1e-3f);

    const MeshData disc = createTerrainDisc();
    const float radial = kTerrainRadius / kTerrainRings, rim = 2.0f * PI * kTerrainRadius / kTerrainSlices;
    const float farTop = terrainRimHeight() - kFarGroundGap;
    record("T43", "Terrain mesh density and far-ground clearance",
           "radial step <= 0.3 m, rim step <= 0.5 m, < 60k triangles, terrain >= far ground top + 5 cm everywhere",
           fmt("radial %.3f m, rim %.3f m, %zu vertices, %zu triangles, min height %.3f vs far ground top %.3f", radial,
               rim, disc.vertexCount(), disc.triangleCount(), minH, farTop),
           radial <= 0.3f && rim <= 0.5f && disc.triangleCount() < 60000 && minH >= farTop + kFarGroundGap - 1e-4f);
}

void testAgentsV2() {
    Scene scene;
    // Accelerated line of sight gives exactly the brute-force answer.
    std::vector<Occluder> occ;
    for (SceneNode* r : scene.roots())
        if (r->object == "Command Bunker" || r->object == "Environment") collectOccluders(*r, occ);
    OccluderGrid grid;
    grid.build(occ, 20.0f, 20);
    uint32_t seed = 12345u;
    auto rnd = [&seed](float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(seed >> 8) / 16777216.0f;
    };
    int mismatches = 0, blockedCount = 0;
    const int samples = 4000;
    for (int i = 0; i < samples; ++i) {
        const Vec3 a{rnd(-19, 19), rnd(0.0f, 1.0f), rnd(-19, 19)};
        const Vec3 b{rnd(-19, 19), rnd(1.0f, 7.0f), rnd(-19, 19)};
        const bool fast = grid.blocked(a, b), brute = lineOfSightBlocked(a, b, occ);
        mismatches += fast != brute ? 1 : 0;
        blockedCount += brute ? 1 : 0;
    }
    record("T44", "Bucket-grid line of sight equals the brute-force test over all occluders",
           fmt("0 mismatches over %d random segments", samples),
           fmt("%d mismatches, %d blocked, %zu occluders", mismatches, blockedCount, occ.size()),
           mismatches == 0 && blockedCount > 0 && blockedCount < samples);

    // Long headless run with no detections: both agents stay healthy on the 20 m terrain.
    Detection none;
    bool uavOk = true, roverOk = true;
    int hiddenFrames = 0;
    const int frames = 60 * 180;
    float maxR = 0.0f, minClear = 1e9f, maxRoverR = 0.0f;
    for (int i = 0; i < frames; ++i) {
        scene.update(1.0f / 60.0f, none);
        const Vec3 p = scene.drone.position, q = scene.rover.position;
        const float r = std::sqrt(p.x * p.x + p.z * p.z), rr = std::sqrt(q.x * q.x + q.z * q.z);
        maxR = std::max(maxR, r);
        maxRoverR = std::max(maxRoverR, rr);
        minClear = std::min(minClear, p.y - terrainHeight(p.x, p.z));
        uavOk = uavOk && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && r <= 19.51f &&
                p.y >= terrainHeight(p.x, p.z) + 1.99f;
        roverOk = roverOk && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && rr <= 18.6f;
        hiddenFrames += scene.rover.hidden ? 1 : 0;
    }
    record("T45", "180 s headless run: UAV inside 19.5 m and above ground, rover finite inside the free area",
           "UAV r <= 19.5 m, clearance >= 2 m; rover r <= 18.6 m; rover hides part of the time",
           fmt("UAV max r %.2f m, min clearance %.2f m; rover max r %.2f m, hidden %.0f%% of frames", maxR, minClear,
               maxRoverR, 100.0f * static_cast<float>(hiddenFrames) / static_cast<float>(frames)),
           uavOk && roverOk && hiddenFrames > 0);

    // Detections switch the UAV to TRACK; losing them switches it to REACQUIRE, still inside the area.
    DroneController ctl;
    DroneState s;
    s.position = {12.0f, terrainHeight(12.0f, 0.0f) + 5.0f, 0.0f};
    s.gimbalPitchDeg = 60.0f;
    Detection seen;
    seen.found = true;
    bool sawTrack = false, sawReacquire = false, inside = true;
    for (int i = 0; i < 600; ++i) {
        const Mat4 cam = translate(s.position) * rotateEulerYXZ({s.pitchDeg, s.yawDeg, s.rollDeg}) *
                         translate({0.0f, -0.11f, 0.1f}) * rotateEulerYXZ({s.gimbalPitchDeg, s.gimbalYawDeg, 0.0f});
        ctl.update(1.0f / 60.0f, static_cast<float>(i) / 60.0f, i < 240 ? seen : none, cam, s);
        sawTrack = sawTrack || s.mode == DroneMode::Track;
        sawReacquire = sawReacquire || (sawTrack && s.mode == DroneMode::Reacquire);
        inside = inside && std::sqrt(s.position.x * s.position.x + s.position.z * s.position.z) <= 19.51f;
    }
    record("T46", "UAV mode logic on the 20 m map: detections -> TRACK, loss -> REACQUIRE, stays inside",
           "TRACK then REACQUIRE, r <= 19.5 m",
           fmt("track=%d reacquire=%d inside=%d, estimate (%.1f, %.1f)", sawTrack, sawReacquire, inside,
               s.targetEstimate.x, s.targetEstimate.z),
           sawTrack && sawReacquire && inside);
}

void writeJson(const std::string& path) {
    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return;
    }
    auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o;
    };
    f << "[\n";
    for (size_t i = 0; i < g_results.size(); ++i) {
        const Result& r = g_results[i];
        f << "  {\"id\": \"" << esc(r.id) << "\", \"description\": \"" << esc(r.description) << "\", \"expected\": \""
          << esc(r.expected) << "\", \"observed\": \"" << esc(r.observed) << "\", \"status\": \""
          << (r.pass ? "PASS" : "FAIL") << "\"}" << (i + 1 < g_results.size() ? "," : "") << "\n";
    }
    f << "]\n";
}

}  // namespace

// ---------------------------------------------------------------- thermal footprints
void testHeatTrail() {
    // Exponential cooling: after one time constant a deposit keeps 1/e of its heat.
    HeatTrail trail;
    trail.deposit({2.0f, 0.0f, 3.0f}, 0.3f, 1.0f);
    const float h0 = trail.sample(2.0f, 3.0f);
    for (int i = 0; i < 45 * 60; ++i) trail.decay(1.0f / 60.0f, 45.0f);
    const float h1 = trail.sample(2.0f, 3.0f);
    record("T47", "Heat trail cools exponentially: after tau = 45 s a footprint keeps 1/e of its heat",
           "ratio = 0.368 +- 0.01", fmt("start %.3f, after 45 s %.3f, ratio %.3f", h0, h1, h1 / h0),
           h0 > 0.5f && near(h1 / h0, std::exp(-1.0f), 0.01f));

    // Deposits stay local (radius) and never exceed 1, so the ground stays below the tracker's blob threshold.
    HeatTrail t2;
    for (int i = 0; i < 50; ++i) t2.deposit({0.0f, 0.0f, 0.0f}, 0.1f, 0.5f);
    const float centre = t2.sample(0.0f, 0.0f), away = t2.sample(0.5f, 0.0f);
    const float groundMax = 0.15f + 0.11f * 0.75f + HeatTrail::kMaxExtraHeat * t2.maxValue();
    record("T48", "Heat trail: deposits are local, clamped to 1, and hot ground stays below the 0.80 blob threshold",
           "centre = 1, 0.5 m away = 0, max ground heat < 0.80",
           fmt("centre %.2f, 0.5 m away %.2f, max ground heat %.2f", centre, away, groundMax),
           near(centre, 1.0f, 1e-3f) && away == 0.0f && groundMax < 0.80f);

    // In the running scene the rover leaves warm tracks where it has driven.
    Scene scene;
    Detection none;
    for (int i = 0; i < 60 * 20; ++i) scene.update(1.0f / 60.0f, none);
    const float peak = scene.heatTrail().maxValue();
    const Vec3 r = scene.rover.position;
    record("T49", "Scene: after 20 s of driving the rover has left a heat trail on the ground",
           "trail peak > 0.2", fmt("trail peak %.2f, rover at (%.1f, %.1f)", peak, r.x, r.z), peak > 0.2f);
}

// ---------------------------------------------------------------- first-person walk camera
void testWalkCamera() {
    Camera cam;
    cam.mode = CameraMode::Walk;
    record("T50", "cameraModeName(Walk) is WALK and Walk comes after Cinematic (existing values unchanged)",
           "\"WALK\", Cinematic = 4, Walk = 5", fmt("\"%s\", Cinematic = %d, Walk = %d", cameraModeName(CameraMode::Walk),
           static_cast<int>(CameraMode::Cinematic), static_cast<int>(CameraMode::Walk)),
           std::string(cameraModeName(CameraMode::Walk)) == "WALK" && static_cast<int>(CameraMode::Cinematic) == 4 &&
               static_cast<int>(CameraMode::Walk) == 5);

    // Walk a loop over the hills; the eye stays 1.7 m above the terrain (head bob is a few cm).
    cam.placeWalk(6.0f, 6.0f);
    float worst = 0.0f;
    int steps = 0;
    for (int i = 0; i < 60 * 40; ++i) {
        cam.walkYawDeg += 0.6f;
        cam.moveWalk(1.0f, 0.0f, 1.0f / 60.0f, i % 400 > 300);
        const float dev = std::fabs(cam.walkEyeY - (terrainHeight(cam.walkX, cam.walkZ) + Camera::kWalkEyeHeight));
        worst = std::max(worst, dev);
        ++steps;
    }
    record("T51", "Walk camera: eye height stays 1.7 +- 0.05 m above the terrain while walking and running",
           "max deviation <= 0.05 m", fmt("max deviation %.3f m over %d steps", worst, steps), worst <= 0.05f);

    // Cannot enter the bunker circle.
    cam.placeWalk(0.0f, 8.0f);
    cam.walkYawDeg = 180.0f;  // facing the bunker
    float minR = 1e9f;
    for (int i = 0; i < 60 * 10; ++i) {
        cam.moveWalk(1.0f, 0.0f, 1.0f / 60.0f, true);
        minR = std::min(minR, std::sqrt(cam.walkX * cam.walkX + cam.walkZ * cam.walkZ));
    }
    record("T52", "Walk camera is pushed out of the 3.2 m bunker circle when walking straight at it",
           "radius >= 3.2 m", fmt("min radius %.2f m", minR), minR >= 3.2f);

    // Clamped to the maximum radius.
    cam.placeWalk(30.0f, 0.0f);
    cam.walkYawDeg = 90.0f;  // +X
    for (int i = 0; i < 60 * 30; ++i) cam.moveWalk(1.0f, 0.0f, 1.0f / 60.0f, true);
    const float rEnd = std::sqrt(cam.walkX * cam.walkX + cam.walkZ * cam.walkZ);
    record("T53", "Walk camera is clamped to a 45 m radius", "radius <= 45.01 m and eye near the rim height + 1.7",
           fmt("radius %.2f m, eye %.2f m, rim %.2f m", rEnd, cam.walkEyeY, terrainRimHeight()),
           rEnd <= 45.01f && rEnd >= 44.0f && std::fabs(cam.walkEyeY - (terrainRimHeight() + 1.7f)) < 0.05f);

    // compute() puts the view at the walker and looks along the yaw; pitch is clamped.
    Scene scene;
    cam.placeWalk(5.0f, -4.0f);
    cam.walkYawDeg = 90.0f;
    cam.walkPitchDeg = 0.0f;
    cam.drag(0.0f, -100000.0f);
    const CameraView v = cam.compute(scene, 16.0f / 9.0f);
    const float ex = std::fabs(v.position.x - 5.0f), ez = std::fabs(v.position.z + 4.0f);
    const float ey = std::fabs(v.position.y - (terrainHeight(5.0f, -4.0f) + 1.7f));
    record("T54", "Walk camera: compute() view sits at the walker (eye 1.7 m up) and mouse-look pitch clamps to +-80 deg",
           "position matches within 5 cm, pitch = 80", fmt("dx %.3f dz %.3f dy %.3f, pitch %.1f", ex, ez, ey, cam.walkPitchDeg),
           ex < 0.05f && ez < 0.05f && ey < 0.05f && near(cam.walkPitchDeg, 80.0f, 1e-3f));

    // C cycles Orbit -> Free -> Chase -> UAV -> Walk -> Orbit.
    Camera c2;
    std::string seq;
    for (int i = 0; i < 5; ++i) { seq += cameraModeName(c2.mode); seq += ">"; c2.cycle(); }
    record("T55", "C cycles Orbit, Free, Chase, UAV CAM, Walk, back to Orbit",
           "ORBIT>FREE>CHASE>UAV CAM>WALK>ORBIT", seq + cameraModeName(c2.mode),
           seq == "ORBIT>FREE>CHASE>UAV CAM>WALK>" && c2.mode == CameraMode::Orbit);
}

int main(int argc, char** argv) {
    testPrimitives();
    testMath();
    testScene();
    testTracker();
    testTrackerCore();
    testHide();
    testDrone();
    testShadows();
    testCone();
    testWorld();
    testTerrainV2();
    testAgentsV2();
    testHeatTrail();
    testWalkCamera();
    for (const auto& g : testkit::groups()) g.second();   // tests/test_*.cpp groups
    int failed = 0;
    for (const Result& r : g_results) failed += r.pass ? 0 : 1;
    std::printf("\n%zu tests, %d failed\n", g_results.size(), failed);
    writeJson(argc > 1 ? argv[1] : "build/test_results.json");
    return failed == 0 ? 0 : 1;
}
