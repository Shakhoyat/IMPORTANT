// v10 M3: primitive library of 16 (T410-T439). CPU only.
// T410-T419: the new generators (closed-form vertex / triangle counts for several parameter sets, unit normals pointing
// outwards, CCW winding that agrees with the normals, inside the unit cube, watertight where the solid is closed), the
// library table and the flat corner tables. T420-T429: the scene's use of the new primitives (which parts, kept
// materials / heat / flags / bounds), occlusion and ray-tracing approximations. T430-T439: export fields (S_rest,
// hidden_at_export, hierarchy order, build stages, object instances) and the part-kind grouping of the capture modes.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "TestKit.h"
#include "primitives/Annulus.h"
#include "primitives/Capsule.h"
#include "primitives/Cube.h"
#include "primitives/FlatFaces.h"
#include "primitives/GrassBlade.h"
#include "primitives/Helix.h"
#include "primitives/Hemisphere.h"
#include "primitives/Library.h"
#include "primitives/Pyramid.h"
#include "primitives/TaperedBox.h"
#include "primitives/Torus.h"
#include "primitives/Wedge.h"
#include "ai/Occlusion.h"
#include "objects/PropKit.h"
#include "objects/Tank.h"
#include "raytrace/SceneBridge.h"
#include "scene/PartOrder.h"
#include "scene/Scene.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

// ------------------------------------------------------------------------------------------------ mesh checks
struct MeshCheck {
    float normalErr = 0.0f;   // max | |n| - 1 |
    int outside = 0;          // vertices outside [-0.5, 0.5]^3 (+1e-5)
    int inward = 0;           // vertices whose normal does not point outwards (dot <= 0)
    int wrongWinding = 0;     // triangles whose CCW geometric normal disagrees with their vertex normals
    int degenerate = 0;       // zero-area triangles
    bool inRange = true;      // indices < vertex count, count % 3 == 0
    bool closed = false;      // every edge (welded by position) shared by exactly two triangles
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
};

bool weldedClosed(const MeshData& m) {
    using Key = std::tuple<long, long, long>;
    auto key = [](const Vec3& p) { return Key{std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f)}; };
    std::map<std::pair<Key, Key>, int> edges;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
        for (size_t e = 0; e < 3; ++e) {
            Key a = key(m.vertices[m.indices[t + e]].position), b = key(m.vertices[m.indices[t + (e + 1) % 3]].position);
            if (a == b) continue;
            if (b < a) std::swap(a, b);
            ++edges[{a, b}];
        }
    for (const auto& kv : edges)
        if (kv.second != 2) return false;
    return !edges.empty();
}

// outward(p, n) > 0 must hold at every vertex.
MeshCheck checkMesh(const MeshData& m, const std::function<float(const Vec3&, const Vec3&)>& outward) {
    MeshCheck c;
    for (const Vertex& v : m.vertices) {
        c.normalErr = std::max(c.normalErr, std::fabs(length(v.normal) - 1.0f));
        const Vec3& p = v.position;
        if (std::fabs(p.x) > 0.5f + 1e-5f || std::fabs(p.y) > 0.5f + 1e-5f || std::fabs(p.z) > 0.5f + 1e-5f) ++c.outside;
        if (outward && outward(p, v.normal) <= 0.0f) ++c.inward;
        c.lo = {std::min(c.lo.x, p.x), std::min(c.lo.y, p.y), std::min(c.lo.z, p.z)};
        c.hi = {std::max(c.hi.x, p.x), std::max(c.hi.y, p.y), std::max(c.hi.z, p.z)};
    }
    c.inRange = m.indices.size() % 3 == 0;
    for (uint32_t i : m.indices) c.inRange = c.inRange && i < m.vertices.size();
    if (!c.inRange) return c;
    for (size_t t = 0; t < m.indices.size(); t += 3) {
        const Vertex& a = m.vertices[m.indices[t]];
        const Vertex& b = m.vertices[m.indices[t + 1]];
        const Vertex& d = m.vertices[m.indices[t + 2]];
        const Vec3 g = cross(b.position - a.position, d.position - a.position);
        if (length(g) < 1e-9f) {
            ++c.degenerate;
            continue;
        }
        if (dot(g, a.normal + b.normal + d.normal) <= 0.0f) ++c.wrongWinding;
    }
    c.closed = weldedClosed(m);
    return c;
}

Vec3 centroid(const MeshData& m) {
    Vec3 s{0, 0, 0};
    for (const Vertex& v : m.vertices) s = s + v.position;
    return s / static_cast<float>(std::max<size_t>(1, m.vertices.size()));
}

std::function<float(const Vec3&, const Vec3&)> fromPoint(const Vec3& c) {
    return [c](const Vec3& p, const Vec3& n) { return dot(n, p - c); };
}

bool good(const MeshCheck& c, bool closed) {
    return c.normalErr < 1e-4f && c.outside == 0 && c.inward == 0 && c.wrongWinding == 0 && c.degenerate == 0 && c.inRange &&
           (!closed || c.closed);
}

std::string summary(const MeshCheck& c) {
    return fmt("normal err %.1e, outside %d, inward %d, wrong winding %d, degenerate %d, closed %d, box [%.3f..%.3f, %.3f..%.3f, "
               "%.3f..%.3f]",
               c.normalErr, c.outside, c.inward, c.wrongWinding, c.degenerate, c.closed, c.lo.x, c.hi.x, c.lo.y, c.hi.y, c.lo.z,
               c.hi.z);
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// ------------------------------------------------------------------------------------------------ T410-T419
void testGenerators() {
    // T410 hemisphere: V = s (k + 1) + 2, T = 2 s k.
    {
        bool ok = true;
        std::string obs;
        for (auto [s, k] : std::vector<std::pair<int, int>>{{36, 9}, {8, 3}, {5, 1}, {24, 6}}) {
            const MeshData m = createHemisphere(s, k);
            const MeshCheck c = checkMesh(m, fromPoint({0, -0.25f, 0}));
            const bool counts = m.vertexCount() == static_cast<size_t>(s * (k + 1) + 2) && m.triangleCount() == static_cast<size_t>(2 * s * k);
            const bool fills = near(c.lo.y, -0.5f) && near(c.hi.y, 0.5f) && near(c.hi.x, 0.5f);
            ok = ok && counts && good(c, true) && fills;
            obs += fmt("(%d,%d): V %zu T %zu; ", s, k, m.vertexCount(), m.triangleCount());
            if (s == 36) obs += summary(c) + "; ";
        }
        record("T410", "Hemisphere generator: V = s(k+1)+2, T = 2sk; unit normals outwards, CCW, in the unit cube, closed",
               "(36,9) 362/648, (8,3) 34/48, (5,1) 12/10, (24,6) 170/288; all checks clean, y spans [-0.5, 0.5]", obs, ok);
    }
    // T411 torus: V = u v, T = 2 u v; outward = away from the tube's centre circle.
    {
        bool ok = true;
        std::string obs;
        struct P { float R, r; int u, v; };
        for (const P& p : std::vector<P>{{0.35f, 0.15f, 36, 16}, {0.25f, 0.25f, 3, 3}, {0.4f, 0.1f, 12, 7}}) {
            const MeshData m = createTorus(p.R, p.r, p.u, p.v);
            const float R = p.R;
            auto outward = [R](const Vec3& q, const Vec3& n) {
                const float a = std::atan2(q.y, q.x);
                const Vec3 c{R * std::cos(a), R * std::sin(a), 0.0f};
                return dot(n, q - c);
            };
            const MeshCheck c = checkMesh(m, outward);
            const bool counts = m.vertexCount() == static_cast<size_t>(p.u * p.v) && m.triangleCount() == static_cast<size_t>(2 * p.u * p.v);
            const bool zr = c.hi.z <= p.r + 1e-5f && (p.v % 4 != 0 || near(c.hi.z, p.r));   // z = r sin(v) reaches r when 4 | v
            const bool k = counts && good(c, true) && zr;
            ok = ok && k;
            obs += fmt("(R %.2f r %.2f u %d v %d): V %zu T %zu %s; ", p.R, p.r, p.u, p.v, m.vertexCount(), m.triangleCount(),
                       k ? "ok" : summary(c).c_str());
        }
        record("T411", "Torus generator: V = uv, T = 2uv; normals point away from the tube centre circle, CCW, closed",
               "(36,16) 576/1152, (3,3) 9/18, (12,7) 84/168; all checks clean, z spans [-r, r]", obs, ok);
    }
    // T412 wedge: 18 / 8, 6 corners, 5 faces.
    {
        const MeshData m = createWedge();
        const MeshCheck c = checkMesh(m, fromPoint(centroid(m)));
        const FlatCorners fc = flatCorners(m);
        int tri = 0, quad = 0;
        for (const auto& f : fc.faces) (f.size() == 3 ? tri : quad) += 1;
        const bool ok = m.vertexCount() == 18 && m.triangleCount() == 8 && good(c, true) && fc.corners.size() == 6 && tri == 2 &&
                        quad == 3 && near(c.lo.x, -0.5f) && near(c.hi.y, 0.5f) && near(c.hi.z, 0.5f);
        record("T412", "Wedge (triangular prism): 18 vertices / 8 triangles, flat faces, outward, closed, 6 corners",
               "18 / 8, 6 corners, 3 quads + 2 triangles, all checks clean",
               fmt("%zu / %zu, %zu corners, %d quads + %d triangles; %s", m.vertexCount(), m.triangleCount(), fc.corners.size(), quad, tri,
                   summary(c).c_str()),
               ok);
    }
    // T413 tapered box: 24 / 12 for any top; the top face has the requested size.
    {
        bool ok = true;
        std::string obs;
        for (auto [tx, tz] : std::vector<std::pair<float, float>>{{0.8f, 0.8f}, {0.5f, 0.9f}, {1.0f, 1.0f}, {0.2f, 0.6f}}) {
            const MeshData m = createTaperedBox(tx, tz);
            const MeshCheck c = checkMesh(m, fromPoint(centroid(m)));
            float topX = 0, topZ = 0;
            for (const Vertex& v : m.vertices)
                if (v.position.y > 0.49f) {
                    topX = std::max(topX, 2.0f * v.position.x);
                    topZ = std::max(topZ, 2.0f * v.position.z);
                }
            const bool k = m.vertexCount() == 24 && m.triangleCount() == 12 && good(c, true) && near(topX, tx) && near(topZ, tz) &&
                           flatCorners(m).corners.size() == 8 && flatCorners(m).faces.size() == 6;
            ok = ok && k;
            obs += fmt("top %.1fx%.1f: %zu/%zu top %.2fx%.2f %s; ", tx, tz, m.vertexCount(), m.triangleCount(), topX, topZ, k ? "ok" : summary(c).c_str());
        }
        record("T413", "Tapered box: 24 vertices / 12 triangles for any top size, top face = topX x topZ, outward, closed",
               "24 / 12 for tops 0.8x0.8, 0.5x0.9, 1x1, 0.2x0.6; 8 corners, 6 faces", obs, ok);
    }
    // T414 pyramid: 16 / 6, 5 corners, apex at (0, 0.5, 0).
    {
        const MeshData m = createPyramid();
        const MeshCheck c = checkMesh(m, fromPoint(centroid(m)));
        const FlatCorners fc = flatCorners(m);
        const bool apex = fc.corners.size() == 5 && near(fc.corners[4].y, 0.5f) && near(fc.corners[4].x, 0.0f);
        const bool ok = m.vertexCount() == 16 && m.triangleCount() == 6 && good(c, true) && apex && fc.faces.size() == 5;
        record("T414", "Pyramid (square base): 16 vertices / 6 triangles, outward, closed, apex (0, 0.5, 0)",
               "16 / 6, 5 corners, 5 faces, all checks clean",
               fmt("%zu / %zu, %zu corners, %zu faces; %s", m.vertexCount(), m.triangleCount(), fc.corners.size(), fc.faces.size(),
                   summary(c).c_str()),
               ok);
    }
    // T415 capsule: V = 2cs + 2, T = 4cs.
    {
        bool ok = true;
        std::string obs;
        struct P { int s, c; float h; };
        for (const P& p : std::vector<P>{{16, 3, 0.25f}, {36, 6, 0.1f}, {5, 1, 0.45f}, {12, 4, 0.3f}}) {
            const MeshData m = createCapsule(p.s, p.c, p.h);
            const MeshCheck c = checkMesh(m, fromPoint({0, 0, 0}));
            const bool counts = m.vertexCount() == static_cast<size_t>(2 * p.c * p.s + 2) && m.triangleCount() == static_cast<size_t>(4 * p.c * p.s);
            ok = ok && counts && good(c, true) && near(c.lo.z, -0.5f) && near(c.hi.z, 0.5f);
            obs += fmt("(%d,%d,%.2f): V %zu T %zu; ", p.s, p.c, p.h, m.vertexCount(), m.triangleCount());
            if (p.s == 16) obs += summary(c) + "; ";
        }
        record("T415", "Capsule generator: V = 2cs+2, T = 4cs; outward, CCW, in the unit cube (z spans [-0.5, 0.5]), closed",
               "(16,3) 98/192, (36,6) 434/864, (5,1) 12/20, (12,4) 98/192; all checks clean", obs, ok);
    }
    // T416 annulus: V = 4n, T = 4n, both faces, inner radius.
    {
        bool ok = true;
        std::string obs;
        for (auto [ri, n] : std::vector<std::pair<float, int>>{{0.43f, 48}, {0.1f, 3}, {0.3f, 10}}) {
            const MeshData m = createAnnulus(ri, n);
            // Local outward direction = the side the face looks to: the normal is exactly +-Z and the CCW winding agrees.
            auto out = [](const Vec3&, const Vec3& nn) { return std::fabs(nn.z) - 0.999f; };
            const MeshCheck c = checkMesh(m, out);
            int front = 0, back = 0;
            float rmin = 1, rmax = 0;
            for (const Vertex& v : m.vertices) {
                (v.normal.z > 0 ? front : back) += 1;
                const float r = std::sqrt(v.position.x * v.position.x + v.position.y * v.position.y);
                rmin = std::min(rmin, r);
                rmax = std::max(rmax, r);
            }
            const bool k = m.vertexCount() == static_cast<size_t>(4 * n) && m.triangleCount() == static_cast<size_t>(4 * n) && good(c, false) &&
                           front == back && near(rmin, ri) && near(rmax, 0.5f) && near(c.hi.z, 0.0f) && near(c.lo.z, 0.0f);
            ok = ok && k;
            obs += fmt("(r_i %.2f, n %d): V %zu T %zu, front %d back %d, r [%.3f, %.3f] %s; ", ri, n, m.vertexCount(), m.triangleCount(),
                       front, back, rmin, rmax, k ? "ok" : summary(c).c_str());
        }
        record("T416", "Annulus (flat ring, both faces): V = 4n, T = 4n, front +Z / back -Z, radii [r_i, 0.5], z = 0",
               "(0.43,48) 192/192, (0.1,3) 12/12, (0.3,10) 40/40; normals +-Z agree with the winding", obs, ok);
    }
    // T417 helix: V = (n+1)m + 2(m+1), T = 2nm + 2m; outward = away from the centre curve c(t).
    {
        bool ok = true;
        std::string obs;
        struct P { float R, r, k; int n, m; };
        for (const P& p : std::vector<P>{{0.475f, 0.025f, 5.0f, 120, 6}, {0.3f, 0.1f, 1.0f, 10, 3}, {0.4f, 0.05f, 2.5f, 37, 8}}) {
            const MeshData mesh = createHelix(p.R, p.r, p.k, p.n, p.m);
            const float L = 1.0f - 2.0f * p.r;
            std::vector<Vec3> curve;
            for (int i = 0; i <= 6000; ++i) {
                const float t = 0.002f + 0.996f * static_cast<float>(i) / 6000.0f;
                const float a = 2.0f * PI * p.k * t;
                curve.push_back({p.R * std::cos(a), p.R * std::sin(a), L * (t - 0.5f)});
            }
            auto outward = [&curve](const Vec3& q, const Vec3& n) {
                float best = 1e9f;
                Vec3 c{};
                for (const Vec3& x : curve)
                    if (length(q - x) < best) { best = length(q - x); c = x; }
                return dot(n, q - c);
            };
            const MeshCheck c = checkMesh(mesh, outward);
            const size_t V = static_cast<size_t>((p.n + 1) * p.m + 2 * (p.m + 1)), T = static_cast<size_t>(2 * p.n * p.m + 2 * p.m);
            const bool k = mesh.vertexCount() == V && mesh.triangleCount() == T && good(c, true);
            ok = ok && k;
            obs += fmt("(R %.3f r %.3f k %.1f n %d m %d): V %zu T %zu %s; ", p.R, p.r, p.k, p.n, p.m, mesh.vertexCount(), mesh.triangleCount(),
                       k ? "ok" : summary(c).c_str());
        }
        record("T417", "Helix coil: V = (n+1)m + 2(m+1), T = 2nm + 2m; normals point away from c(t), CCW, in the unit cube, closed",
               "(120,6) 740/1452, (10,3) 41/66, (37,8) 322/608; all checks clean", obs, ok);
    }
    // T418 library: 16 entries in report order, one mesh per type, every unit primitive inside the unit cube.
    {
        const std::vector<LibraryEntry> lib = primitiveLibrary();
        const char* names[16] = {"cube", "cuboid", "sphere", "hemisphere", "cylinder", "frustum", "cone", "torus", "wedge",
                                 "tapered box", "pyramid", "capsule", "annulus", "helix", "grass blade", "terrain disc"};
        bool order = lib.size() == 16;
        for (size_t i = 0; order && i < 16; ++i) order = lib[i].name == names[i] && lib[i].number == static_cast<int>(i) + 1;
        int bad = 0;
        std::string worst;
        for (const LibraryEntry& e : lib) {
            if (e.type == PrimitiveType::TerrainDisc) continue;
            const MeshCheck c = checkMesh(e.mesh, nullptr);
            const bool blade = e.type == PrimitiveType::GrassBlade;   // 1.25 wide by design (rendering/Grass.cpp)
            if (e.mesh.vertexCount() == 0 || (!blade && c.outside) || c.wrongWinding || c.degenerate || c.normalErr > 1e-4f) {
                ++bad;
                worst += e.name + " ";
            }
        }
        const MeshData blade = createGrassBlade();
        const bool ok = order && bad == 0 && blade.vertexCount() == 7 && blade.triangleCount() == 5 && lib[1].cuboid && lib[0].flat &&
                        lib[8].flat && lib[9].flat && lib[10].flat && !lib[2].flat;
        record("T418", "Library of 16 primitives in report order; scene meshes non-empty, unit-cube fitted, CCW, unit normals",
               "16 entries cube..terrain disc; 0 bad meshes; grass blade 7 / 5; flat = cube, cuboid, wedge, tapered box, pyramid",
               fmt("%zu entries, order %s, bad %d %s, blade %zu / %zu", lib.size(), order ? "ok" : "WRONG", bad, worst.c_str(),
                   blade.vertexCount(), blade.triangleCount()),
               ok);
    }
    // T419 corner tables: cube 8 corners / 6 quads, Euler V - E + F = 2 for the four flat solids.
    {
        bool ok = true;
        std::string obs;
        for (auto [name, mesh] : std::vector<std::pair<const char*, MeshData>>{
                 {"cube", createCube()}, {"wedge", createWedge()}, {"tapered box", createTaperedBox(0.8f, 0.8f)}, {"pyramid", createPyramid()}}) {
            const FlatCorners fc = flatCorners(mesh);
            std::set<std::pair<int, int>> edges;
            for (const auto& f : fc.faces)
                for (size_t i = 0; i < f.size(); ++i) edges.insert({std::min(f[i], f[(i + 1) % f.size()]), std::max(f[i], f[(i + 1) % f.size()])});
            const int euler = static_cast<int>(fc.corners.size()) - static_cast<int>(edges.size()) + static_cast<int>(fc.faces.size());
            ok = ok && euler == 2;
            obs += fmt("%s V %zu E %zu F %zu; ", name, fc.corners.size(), edges.size(), fc.faces.size());
        }
        const FlatCorners cube = flatCorners(createCube());
        ok = ok && cube.corners.size() == 8 && cube.faces.size() == 6;
        record("T419", "Flat primitives' corner tables (unique corners + faces as corner tuples) satisfy Euler V - E + F = 2",
               "cube 8/12/6, wedge 6/9/5, tapered box 8/12/6, pyramid 5/8/5", obs, ok);
    }
}

// ------------------------------------------------------------------------------------------------ T420-T429 scene
std::vector<const SceneNode*> named(const Scene& sc, const std::string& name) {
    std::vector<const SceneNode*> out;
    for (const SceneNode* n : sc.leaves())
        if (n->name == name) out.push_back(n);
    return out;
}

// Largest distance of a leaf's mesh vertices outside the leaf's world AABB of the unit cube (worldBounds).
float outsideBounds(const SceneNode& n, const MeshData& mesh) {
    const Box b = worldBounds(n.world);
    float worst = 0.0f;
    for (const Vertex& v : mesh.vertices) {
        const Vec3 p = transformPoint(n.world, v.position);
        worst = std::max({worst, b.min.x - p.x, p.x - b.max.x, b.min.y - p.y, p.y - b.max.y, b.min.z - p.z, p.z - b.max.z});
    }
    return worst;
}

// World extent of a leaf's mesh along a world direction.
float extentAlong(const SceneNode& n, const Vec3& dir) {
    const MeshData mesh = sceneMesh(n.primitive);
    float lo = 1e9f, hi = -1e9f;
    for (const Vertex& v : mesh.vertices) {
        const float d = dot(transformPoint(n.world, v.position), dir);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    return hi - lo;
}

void testScene() {
    Scene sc;
    sc.updateWorld();
    // T420 which parts use which new primitive.
    {
        const std::vector<std::pair<std::vector<const char*>, PrimitiveType>> map = {
            {{"Wire coil"}, PrimitiveType::Helix},
            {{"Tyre", "Tyre tread", "Muzzle ring", "Cupola ring", "Motor cooling ring"}, PrimitiveType::Torus},
            {{"Radome", "Helmet", "Earth berm"}, PrimitiveType::Hemisphere},
            {{"Ramp floor", "Glacis plate", "Wing wall slope"}, PrimitiveType::Wedge},
            {{"Turret body", "Torso"}, PrimitiveType::TaperedBox},
            {{"Revetment sandbag", "Sandbag", "Wing sandbag"}, PrimitiveType::Capsule},
            {{"Gate post cap"}, PrimitiveType::Pyramid},
            {{"Pad ring"}, PrimitiveType::Annulus}};
        int wrong = 0, missing = 0;
        std::string obs;
        for (const auto& [names, type] : map)
            for (const char* nm : names) {
                const auto parts = named(sc, nm);
                if (parts.empty()) ++missing;
                for (const SceneNode* n : parts) wrong += n->primitive != type;
                obs += fmt("%s %zu, ", nm, parts.size());
            }
        const size_t oldTurns = named(sc, "Wire coil turn").size();
        std::map<PrimitiveType, int> used;
        for (const SceneNode* n : sc.leaves()) ++used[n->primitive];
        int newTypes = 0;
        for (PrimitiveType t : {PrimitiveType::Hemisphere, PrimitiveType::Torus, PrimitiveType::Wedge, PrimitiveType::TaperedBox,
                                PrimitiveType::Pyramid, PrimitiveType::Capsule, PrimitiveType::Annulus, PrimitiveType::Helix})
            newTypes += used[t] > 0;
        record("T420", "Scene parts use the new primitives (helix wire, torus rings, hemisphere domes, wedges, tapered boxes, capsule "
                       "sandbags, pyramid caps, annulus pad ring)",
               "every listed part present with its new primitive, no 'Wire coil turn' struts left, all 8 new types used",
               obs + fmt("wrong %d, missing %d, old turns %zu, new types used %d", wrong, missing, oldTurns, newTypes),
               wrong == 0 && missing == 0 && oldTurns == 0 && newTypes == 8);
    }
    // T421 materials and flags of the replaced parts.
    {
        const Material bag = mats::sandbag();
        int bad = 0, n = 0, occ = 0, bunkerBags = 0;
        for (const char* nm : {"Revetment sandbag", "Sandbag", "Wing sandbag"})
            for (const SceneNode* p : named(sc, nm)) {
                ++n;
                if (p->material.surface != bag.surface) ++bad;   // the bunker's own sand material has another heat
                if (p->object == "Command Bunker") {
                    ++bunkerBags;
                    occ += p->occluder ? 1 : 0;
                }
            }
        int camo = 0, camoN = 0;
        for (const char* nm : {"Glacis plate", "Turret body"})
            for (const SceneNode* p : named(sc, nm)) {
                ++camoN;
                camo += p->material.surface == Surface::Camo;
            }
        int rubber = 0;
        for (const SceneNode* p : named(sc, "Tyre"))
            rubber += p->material.surface == Surface::Rubber || p->material.surface == Surface::SteelPlate || p->material.surface == Surface::Metal;
        const size_t tyres = named(sc, "Tyre").size();
        const bool ok = bad == 0 && n > 0 && bunkerBags > 0 && occ == bunkerBags && camo == camoN && camoN > 0 &&
                        rubber == static_cast<int>(tyres);
        record("T421", "Replaced parts keep their materials and flags (sandbag sand surface, bunker sandbags occlude, camo plates, "
                       "rubber / steel tyres)",
               "0 sandbags with another material, every bunker sandbag an occluder, glacis + turret camo, tyres rubber or steel",
               fmt("%d sandbags (%d wrong), bunker occluders %d / %d, camo %d / %d, tyres %d / %zu", n, bad, occ, bunkerBags, camo, camoN,
                   rubber, tyres),
               ok);
    }
    // T422 every new-primitive leaf lies inside the AABB of its unit cube (the bounding / occlusion / collision paths).
    {
        int checked = 0;
        float worst = 0.0f;
        std::map<PrimitiveType, MeshData> meshes;
        for (const SceneNode* n : sc.leaves()) {
            if (n->primitive < PrimitiveType::Hemisphere || n->primitive == PrimitiveType::GrassBlade) continue;
            if (!meshes.count(n->primitive)) meshes[n->primitive] = sceneMesh(n->primitive);
            worst = std::max(worst, outsideBounds(*n, meshes[n->primitive]));
            ++checked;
        }
        record("T422", "New-primitive parts stay inside worldBounds(world) (unit-cube AABB used by culling, occlusion grid, collision)",
               "all new-primitive leaves, 0 m outside", fmt("%d leaves, worst %.2e m outside", checked, worst), checked > 0 && worst < 1e-4f);
    }
    // T423 dimensions of the replaced parts.
    {
        std::string obs;
        bool ok = true;
        // Tank road wheel tyre: diameter 0.64 m across the wheel, width 0.4 m along the axle (the part's local Z).
        const SceneNode* tyre = nullptr;
        for (const SceneNode* n : named(sc, "Tyre"))
            if (n->object == "Main Battle Tank" && n->material.surface == Surface::Rubber) {
                tyre = n;
                break;
            }
        if (tyre) {
            const Vec3 axle = normalize(transformDirection(tyre->world, {0, 0, 1}));
            const Vec3 across = normalize(transformDirection(tyre->world, {1, 0, 0}));
            const float w = extentAlong(*tyre, axle), d = extentAlong(*tyre, across);
            ok = ok && near(w, 0.4f, 2e-3f) && near(d, 0.64f, 2e-3f);
            obs += fmt("tyre width %.3f dia %.3f; ", w, d);
        } else {
            ok = false;
        }
        // Concertina coil: centre-line radius 0.38 m, wire 4 cm, 0.84 m long (+ the wire's thickness).
        const auto coils = named(sc, "Wire coil");
        if (!coils.empty()) {
            const SceneNode& c = *coils.front();
            const float sx = length(transformDirection(c.world, {1, 0, 0}));
            const float len = extentAlong(c, normalize(transformDirection(c.world, {0, 0, 1})));
            const float R = lib::kHelixR * sx, wire = 2.0f * lib::kHelixTube * sx;
            ok = ok && near(R, 0.38f, 1e-3f) && near(wire, 0.04f, 1e-3f) && len > 0.84f && len < 0.84f + wire + 2e-3f;
            obs += fmt("coil R %.3f wire %.3f length %.3f; ", R, wire, len);
        } else {
            ok = false;
        }
        // Glacis wedge: its slope face runs from (z 1.8, y 1.48) to (z 3.1, y 0.84) in hull space (the old plate's top face).
        int gl = 0;
        const auto glacis = named(sc, "Glacis plate");
        for (const SceneNode* g : glacis) {
            const Mat4 local = g->local.matrix();
            const Vec3 top = transformPoint(local, {0, 0.5f, -0.5f}), low = transformPoint(local, {0, -0.5f, 0.5f});
            gl += near(top.z, 1.8f, 1e-3f) && near(top.y, 1.48f, 1e-3f) && near(low.z, 3.1f, 1e-3f) && near(low.y, 0.84f, 1e-3f);
        }
        ok = ok && !glacis.empty() && gl == static_cast<int>(glacis.size());
        obs += fmt("glacis slope ok %d / %zu; ", gl, glacis.size());
        // Gate post cap pyramid: 0.85 m base resting on the 3.4 m post.
        for (const SceneNode* p : named(sc, "Gate post cap")) {
            const Vec3 base = transformPoint(p->local.matrix(), {0, -0.5f, 0});
            ok = ok && near(base.y, 3.375f, 1e-3f) && near(p->local.scale.x, 0.85f);
            obs += fmt("cap base y %.3f; ", base.y);
        }
        record("T423", "Replaced parts keep their size: tank tyre 0.64 m x 0.4 m, coil R 0.38 m / wire 4 cm / 0.84 m, glacis slope on the "
                       "old plate face, gate cap on its post",
               "within 1-2 mm", obs, ok);
    }
    // T424 exact occlusion of the hemisphere and the flat solids, conservative cylinder for the ring / tube shapes.
    {
        const Mat4 W = translate({0, 1, 0}) * scale({2, 2, 2});   // the unit box spans y 0..2, x, z -1..1
        auto hits = [&](PrimitiveType t, const Vec3& a, const Vec3& b) {
            const Occluder o{t, inverse(W), worldBounds(W)};
            return segmentHitsPrimitive(a, b, o);
        };
        const bool domeTop = hits(PrimitiveType::Hemisphere, {-5, 1.8f, 0}, {5, 1.8f, 0});          // dome radius there 0.6 m
        const bool domeCorner = hits(PrimitiveType::Hemisphere, {-5, 1.9f, 0.9f}, {5, 1.9f, 0.9f});  // in the box, outside the dome
        const bool wedgeSolid = hits(PrimitiveType::Wedge, {-5, 0.4f, -0.5f}, {5, 0.4f, -0.5f});
        const bool wedgeEmpty = hits(PrimitiveType::Wedge, {-5, 1.8f, 0.8f}, {5, 1.8f, 0.8f});      // above the slope
        const bool pyrAxis = hits(PrimitiveType::Pyramid, {0, 3, 0}, {0, -1, 0});
        const bool pyrEdge = hits(PrimitiveType::Pyramid, {-5, 1.8f, 0.9f}, {5, 1.8f, 0.9f});
        const bool taperIn = hits(PrimitiveType::TaperedBox, {-5, 0.2f, 0.9f}, {5, 0.2f, 0.9f});
        const bool taperOut = hits(PrimitiveType::TaperedBox, {-5, 1.95f, 0.9f}, {5, 1.95f, 0.9f});  // top is only 0.8 wide
        const bool torusHole = hits(PrimitiveType::Torus, {0, 1, -5}, {0, 1, 5});                    // conservative: hole counts
        record("T424", "Occlusion: hemisphere / wedge / pyramid / tapered box exact (face planes), torus by its bounding cylinder",
               "dome hit, box corner miss; wedge solid hit, above slope miss; pyramid axis hit, near-top edge miss; taper base hit, "
               "beside the narrow top miss; torus hole counted (conservative)",
               fmt("dome %d corner %d, wedge %d/%d, pyramid %d/%d, taper %d/%d, torus hole %d", domeTop, domeCorner, wedgeSolid, wedgeEmpty,
                   pyrAxis, pyrEdge, taperIn, taperOut, torusHole),
               domeTop && !domeCorner && wedgeSolid && !wedgeEmpty && pyrAxis && !pyrEdge && taperIn && !taperOut && torusHole);
    }
    // T425 ray tracing bridge: helices hidden; a hemisphere is traced as its parent ellipsoid.
    {
        rt::SceneBridge br;
        rt::RtScene rs;
        rt::LightOptions lo;
        br.sync(sc, lo, rs);
        int helixHidden = 0, helices = 0, hemiOk = 0, hemis = 0;
        for (size_t i = 0; i < rs.prims.size(); ++i) {
            const SceneNode* leaf = br.leafOf(rs, static_cast<int>(i));
            if (!leaf) continue;
            if (leaf->primitive == PrimitiveType::Helix) {
                ++helices;
                helixHidden += (rs.prims[i].flags & rt::kHidden) != 0;
            }
            if (leaf->primitive == PrimitiveType::Hemisphere) {
                ++hemis;
                const Vec3 pole = transformPoint(leaf->world, {0, 0.5f, 0});         // the dome's pole
                const Vec3 top = transformPoint(rs.prims[i].world, {0, 0.5f, 0});    // the traced ellipsoid's top
                hemiOk += length(pole - top) < 1e-3f && rs.prims[i].shape == rt::Shape::Sphere;
            }
        }
        record("T425", "Ray-tracing bridge: helix wire kept as a hidden prim, hemisphere traced as its parent ellipsoid (pole matches)",
               "every helix hidden, every hemisphere's ellipsoid top at the dome pole",
               fmt("helices %d/%d hidden, hemispheres %d/%d ok, %zu prims", helixHidden, helices, hemiOk, hemis, rs.prims.size()),
               helices > 0 && helixHidden == helices && hemis > 0 && hemiOk == hemis);
    }
    // T426 the export's primitive instances partition the leaves (cube vs cuboid by uniform S, grass blade none).
    {
        std::map<std::string, int> inst;
        for (const SceneNode* n : sc.leaves())
            ++inst[n->primitive == PrimitiveType::Cube && isCuboidScale(restScaleOf(*n)) ? "Cuboid" : primitiveName(n->primitive)];
        int sum = 0;
        for (const LibraryEntry& e : primitiveLibrary()) sum += inst[e.cuboid ? "Cuboid" : primitiveName(e.type)];
        record("T426", "Library instance counts partition the scene leaves (cube = uniform S, cuboid = non-uniform S)",
               "sum over the 16 entries == leaf count, both cube and cuboid used",
               fmt("sum %d, leaves %zu, cube %d, cuboid %d", sum, sc.leaves().size(), inst["Cube"], inst["Cuboid"]),
               sum == static_cast<int>(sc.leaves().size()) && inst["Cube"] > 0 && inst["Cuboid"] > 0);
    }
}

// ------------------------------------------------------------------------------------------------ T430-T435 export / capture
void testExport() {
    Scene sc;
    sc.updateWorld();
    // T430 S_rest / hidden_at_export.
    {
        const auto casings = named(sc, "Shell casing");
        const SceneNode* flash = nullptr;
        for (const SceneNode* n : named(sc, "Muzzle flash"))
            if (n->object == "Main Battle Tank") flash = n;
        bool ok = !casings.empty() && flash;
        int hidden = 0;
        for (const SceneNode* c : casings) {
            hidden += hiddenAtRest(*c);
            const Vec3 r = restScaleOf(*c);
            ok = ok && near(r.x, 0.03f) && near(r.z, 0.1f);
        }
        ok = ok && hidden == static_cast<int>(casings.size()) && hiddenAtRest(*flash) && near(restScaleOf(*flash).z, 0.9f);
        const auto hulls = named(sc, "Hull core");
        ok = ok && !hulls.empty() && !hiddenAtRest(*hulls.front()) && restScaleOf(*hulls.front()).x == hulls.front()->local.scale.x;
        record("T430", "Export S_rest / hidden_at_export: zero-scaled casings and the hidden tank flash keep their modelled scale",
               "casings S_rest 0.03 x 0.03 x 0.1 and hidden, tank muzzle flash S_rest z 0.9 and hidden, hull core visible with S_rest = S",
               fmt("%zu casings (%d hidden), flash hidden %d S_rest z %.2f", casings.size(), hidden, flash ? hiddenAtRest(*flash) : -1,
                   flash ? restScaleOf(*flash).z : 0.0f),
               ok);
    }
    // T431 hierarchy order and build stages.
    {
        const SceneNode* tank = nullptr;
        std::map<const SceneNode*, int> uavRigs;
        int tankLeaves = 0, uavLeaves = 0;
        for (const SceneNode* n : sc.leaves()) {
            if (n->object == "Main Battle Tank") {
                ++tankLeaves;
                if (!tank) tank = rigRoot(*n);
            }
            if (n->object == "Autonomous UAV") {
                ++uavLeaves;
                ++uavRigs[rigRoot(*n)];
            }
        }
        const std::vector<const SceneNode*> order = tank ? rigLeavesDfs(*tank) : std::vector<const SceneNode*>{};
        std::vector<const SceneNode*> pre;   // independent pre-order walk of the tank's tree
        std::function<void(const SceneNode&)> walk = [&](const SceneNode& n) {
            if (n.isLeaf()) pre.push_back(&n);
            for (const auto& c : n.children) walk(*c);
        };
        if (tank) walk(*tank);
        int prev = 0;
        bool mono = true;
        for (int p : kBuildStagePct) {
            const int k = buildStageCount(static_cast<int>(order.size()), p);
            mono = mono && k >= prev && k >= 1;
            prev = k;
        }
        int uavSum = 0;
        for (auto& kv : uavRigs) uavSum += kv.second;
        const bool ok = static_cast<int>(order.size()) == tankLeaves && order == pre && mono && prev == tankLeaves &&
                        uavRigs.size() == 2 && uavSum == uavLeaves && buildStageCount(100, 12) == 12 && buildStageCount(75, 12) == 9 &&
                        buildStageCount(3, 12) == 1;
        record("T431", "Hierarchy (depth-first) part order per rig and build-stage counts ceil(pct N / 100)",
               "tank order = pre-order of its tree, all its leaves; stages increasing to N; 2 UAV rigs; ceil(75 x 12 %) = 9",
               fmt("tank %zu / %d leaves, stages %d %d %d %d, UAV rigs %zu (%d leaves of %d)", order.size(), tankLeaves,
                   buildStageCount(tankLeaves, 12), buildStageCount(tankLeaves, 35), buildStageCount(tankLeaves, 65),
                   buildStageCount(tankLeaves, 100), uavRigs.size(), uavSum, uavLeaves),
               ok);
    }
    // T432 part kinds (capture parts:<object>): suffix tokens dropped, counts add up.
    {
        const bool names = partKindName("Track link L07") == "Track link" && partKindName("Exhaust pipe R") == "Exhaust pipe" &&
                           partKindName("Bedroll A") == "Bedroll" && partKindName("Motor mount FL") == "Motor mount" &&
                           partKindName("HESCO cell (upper)") == "HESCO cell (upper)" && partKindName("Tyre") == "Tyre" &&
                           partKindName("Soldier 1") == "Soldier";
        const SceneNode* tank = nullptr;
        for (const SceneNode* n : sc.leaves())
            if (n->object == "Main Battle Tank") {
                tank = rigRoot(*n);
                break;
            }
        int sum = 0, links = 0;
        size_t kinds = 0;
        if (tank) {
            const std::vector<PartKind> k = partKinds(*tank);
            kinds = k.size();
            for (const PartKind& p : k) {
                sum += p.count;
                if (p.name == "Track link") links = p.count;
            }
        }
        const int leaves = tank ? static_cast<int>(rigLeavesDfs(*tank).size()) : -1;
        record("T432", "Part kinds for the parts:<object> tiles: name without side / index suffix + primitive, counts sum to the rig",
               "suffix rules ok; tank kinds < leaves, counts sum to the leaves, 'Track link' = 2 x links per side",
               fmt("names %d, %zu kinds, sum %d / %d leaves, track links %d", names, kinds, sum, leaves, links),
               names && sum == leaves && static_cast<int>(kinds) < leaves && links == 2 * Tank::kLinksPerSide);
    }
    // T433 object families (variant_of / instances): numbered variants share the rig structure.
    {
        std::map<std::string, std::vector<std::pair<std::string, PrimitiveType>>> sig;
        std::map<std::string, std::set<std::string>> family;
        for (const SceneNode* r : sc.roots()) {
            family[objectFamily(r->object)].insert(r->object);
            auto& s = sig[r->object];
            if (!s.empty()) continue;
            for (const SceneNode* l : rigLeavesDfs(*r)) s.emplace_back(l->name, l->primitive);
        }
        bool same = true;
        for (const char* fam : {"Soldier", "Main Battle Tank"})
            for (const std::string& m : family[fam]) same = same && sig[m] == sig[*family[fam].begin()];
        const bool ok = objectFamily("Soldier 3") == "Soldier" && objectFamily("Main Battle Tank 2") == "Main Battle Tank" &&
                        objectFamily("Autonomous UAV") == "Autonomous UAV" && family["Soldier"].size() >= 2 &&
                        family["Main Battle Tank"].size() >= 2 && same;
        record("T433", "Object families for per-unit tables: 'Soldier N' -> Soldier, 'Main Battle Tank N' -> Main Battle Tank, identical rigs",
               "families found, every member has the same (part name, primitive) sequence",
               fmt("soldier objects %zu, tank objects %zu, identical %d", family["Soldier"].size(), family["Main Battle Tank"].size(), same), ok);
    }
    // T434 slugs for the capture file names.
    {
        const bool ok = slug("Main Battle Tank") == "main_battle_tank" && slug("Soldier 1") == "soldier_1" &&
                        slug("HESCO cell (upper)") == "hesco_cell_upper" && slug("Air-defence radar") == "air_defence_radar";
        record("T434", "File-name slugs of the capture modes (parts_<slug>/NN_<part>.png, build_<slug>_<pct>.png)",
               "main_battle_tank, soldier_1, hesco_cell_upper, air_defence_radar",
               slug("Main Battle Tank") + ", " + slug("Soldier 1") + ", " + slug("HESCO cell (upper)") + ", " + slug("Air-defence radar"), ok);
    }
    // T435 every mesh type has a scene mesh and exactly one library entry (cuboid shares the cube).
    {
        std::map<PrimitiveType, int> seen;
        for (const LibraryEntry& e : primitiveLibrary())
            if (!e.cuboid) ++seen[e.type];
        int missing = 0, dup = 0;
        const size_t types = sizeof(kMeshTypes) / sizeof(kMeshTypes[0]);
        for (PrimitiveType t : kMeshTypes) {
            missing += sceneMesh(t).vertexCount() == 0 || seen[t] == 0;
            dup += seen[t] > 1;
        }
        record("T435", "Every PrimitiveType except None has a scene mesh and exactly one library entry (cuboid = cube mesh)",
               "15 mesh types, 0 missing, 0 duplicated", fmt("%zu mesh types, missing %d, duplicated %d", types, missing, dup),
               missing == 0 && dup == 0 && types == 15);
    }
}

}  // namespace

TS_TEST_GROUP(V10M3) {
    testGenerators();
    testScene();
    testExport();
}
