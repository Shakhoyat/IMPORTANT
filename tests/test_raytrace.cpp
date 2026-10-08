// v7 WP-H ray tracing: analytic intersectors, SAH BVH (vs brute force, refit), terrain height field, optics
// (Fresnel, Snell), Whitted ray tree, path tracer energy (furnace tests), light tracing vs path tracing,
// determinism, the scene bridge on the real camp, the GPU packing layout and the denoiser. Test IDs T260-T279.
#include <cmath>
#include <cstring>

#include "TestKit.h"
#include "objects/Terrain.h"
#include "raytrace/GpuLayout.h"
#include "raytrace/Optics.h"
#include "raytrace/RayTracer.h"
#include "raytrace/SceneBridge.h"
#include "scene/HudMath.h"
#include "scene/Scene.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;
using namespace ts::rt;

namespace {

RtMaterial diffuse(float a) {
    RtMaterial m;
    m.albedo = {a, a, a};
    m.roughness = 1.0f;
    m.metalness = 0.0f;
    m.specular = 0.0f;   // pure Lambert
    return m;
}

Vec3 randomDir(Rng& r) { return sampleSphere(r.uniform(), r.uniform()); }

// Random primitives in a 20 m box (all four shapes, random rotation and non-uniform scale).
void randomScene(RtScene& s, int n, uint32_t seed) {
    Rng r(seed, 3);
    for (int i = 0; i < n; ++i) {
        const Vec3 p{r.uniform() * 20 - 10, r.uniform() * 20 - 10, r.uniform() * 20 - 10};
        const Vec3 sc{0.2f + r.uniform() * 1.5f, 0.2f + r.uniform() * 1.5f, 0.2f + r.uniform() * 1.5f};
        const Mat4 w = translate(p) * rotateEulerYXZ({r.uniform() * 360, r.uniform() * 360, r.uniform() * 360}) * scale(sc);
        s.addPrim(static_cast<Shape>(i % 4), w, diffuse(0.5f));
    }
}

}  // namespace

TS_TEST_GROUP(raytrace) {
    // ---------------------------------------------------------------- T260 sphere
    {
        float t;
        Vec3 n;
        const bool a = intersectUnitSphere({0, 0, -5}, {0, 0, 1}, 1e-4f, 1e30f, t, n);
        const bool okA = a && std::fabs(t - 4.5f) < 1e-5f && std::fabs(n.z + 1.0f) < 1e-5f;
        float t2;
        Vec3 n2;
        const bool b = intersectUnitSphere({0, 0, 0}, {0, 2, 0}, 1e-4f, 1e30f, t2, n2);   // from inside, unnormalised d
        const bool okB = b && std::fabs(t2 - 0.25f) < 1e-5f && n2.y > 0.999f;
        float t3;
        Vec3 n3;
        const bool miss = !intersectUnitSphere({0.6f, 0, -5}, {0, 0, 1}, 1e-4f, 1e30f, t3, n3);
        // tangent-ish ray at x = 0.3: t = 5 - sqrt(0.25 - 0.09) = 4.6
        float t4;
        Vec3 n4;
        const bool c = intersectUnitSphere({0.3f, 0, -5}, {0, 0, 1}, 1e-4f, 1e30f, t4, n4);
        const bool okC = c && std::fabs(t4 - 4.6f) < 1e-4f;
        record("T260", "Ray / sphere: quadratic roots (outside, inside with unnormalised d, miss, off-centre)",
               "t = 4.5, 0.25, miss, 4.6", fmt("t = %.4f, %.4f, %s, %.4f", t, t2, miss ? "miss" : "HIT", t4), okA && okB && miss && okC);
    }
    // ---------------------------------------------------------------- T261 box
    {
        float t;
        Vec3 n;
        const bool a = intersectUnitBox({2, 0.1f, 0.2f}, {-1, 0, 0}, 1e-4f, 1e30f, t, n);
        const bool okA = a && std::fabs(t - 1.5f) < 1e-5f && n.x > 0.999f;
        float t2;
        Vec3 n2;
        const bool b = intersectUnitBox({0, 0, 0}, normalize(Vec3{1, 1, 0}), 1e-4f, 1e30f, t2, n2);   // exits through an edge
        const bool okB = b && std::fabs(t2 - 0.5f * std::sqrt(2.0f)) < 1e-4f;
        float t3;
        Vec3 n3;
        const bool miss = !intersectUnitBox({0.6f, 0.0f, -3}, {0, 0, 1}, 1e-4f, 1e30f, t3, n3);
        float t4;
        Vec3 n4;
        const bool parallel = intersectUnitBox({0.2f, 0.0f, -3}, {0, 0, 1}, 1e-4f, 1e30f, t4, n4) && n4.z < -0.999f && std::fabs(t4 - 2.5f) < 1e-5f;
        record("T261", "Ray / box: slab method (face normal, exit from inside, miss, axis-parallel ray)",
               "t = 1.5 (+X), 0.7071, miss, 2.5 (-Z)", fmt("t = %.4f, %.4f, %s, %.4f", t, t2, miss ? "miss" : "HIT", t4), okA && okB && miss && parallel);
    }
    // ---------------------------------------------------------------- T262 cylinder
    {
        float t1, t2, t3;
        Vec3 n1, n2, n3;
        const bool side = intersectUnitCylinder({3, 0, 0.2f}, {-1, 0, 0}, 1e-4f, 1e30f, t1, n1) && std::fabs(t1 - 2.5f) < 1e-5f && n1.x > 0.999f;
        const bool cap = intersectUnitCylinder({0.1f, 0.1f, 4}, {0, 0, -1}, 1e-4f, 1e30f, t2, n2) && std::fabs(t2 - 3.5f) < 1e-5f && n2.z > 0.999f;
        const bool above = !intersectUnitCylinder({3, 0, 0.7f}, {-1, 0, 0}, 1e-4f, 1e30f, t3, n3);   // passes over the top
        float t4;
        Vec3 n4;
        const bool slant = intersectUnitCylinder({2, 0, 2}, normalize(Vec3{-1, 0, -1}), 1e-4f, 1e30f, t4, n4);
        const Vec3 p = Vec3{2, 0, 2} + normalize(Vec3{-1, 0, -1}) * t4;
        const bool onSurface = slant && std::fabs(unitShapeDistance(Shape::Cylinder, p)) < 1e-4f;
        record("T262", "Ray / capped cylinder: side quadratic, cap planes, ray over the top, slanted hit on the rim region",
               "t = 2.5 (side), 3.5 (cap), miss, |F(p)| < 1e-4", fmt("t = %.4f, %.4f, %s, F = %.2e", t1, t2, above ? "miss" : "HIT", unitShapeDistance(Shape::Cylinder, p)),
               side && cap && above && onSurface);
    }
    // ---------------------------------------------------------------- T263 cone
    {
        float t1, t2, t3, t4;
        Vec3 n1, n2, n3, n4;
        // at z = 0 the radius is 0.25: from x = 2 along -x -> t = 1.75; normal tilted up (+z) by atan(0.5)
        const bool side = intersectUnitCone({2, 0, 0}, {-1, 0, 0}, 1e-4f, 1e30f, t1, n1) && std::fabs(t1 - 1.75f) < 1e-5f &&
                          std::fabs(n1.z - 0.4472136f) < 1e-4f && std::fabs(n1.x - 0.8944272f) < 1e-4f;
        const bool base = intersectUnitCone({0.3f, 0, -3}, {0, 0, 1}, 1e-4f, 1e30f, t2, n2) && std::fabs(t2 - 2.5f) < 1e-5f && n2.z < -0.999f;
        const bool apexMiss = !intersectUnitCone({0.2f, 0, 3}, {0, 0, -1}, 1e-4f, 1e30f, t3, n3) || (t3 > 2.0f);   // r(z)=0.2 at z=0.1
        const bool apexHit = intersectUnitCone({0.2f, 0, 3}, {0, 0, -1}, 1e-4f, 1e30f, t4, n4) && std::fabs(t4 - 2.9f) < 1e-4f;
        // never hits the mirrored nappe above the apex
        float t5;
        Vec3 n5;
        const bool nappe = !intersectUnitCone({2, 0, 1.0f}, {-1, 0, 0}, 1e-4f, 1e30f, t5, n5);
        record("T263", "Ray / cone: side (exact normal (0.894, 0, 0.447)), base cap, hit near the apex, no hit on the mirrored nappe",
               "t = 1.75, 2.5, 2.9, miss above apex", fmt("t = %.4f, %.4f, %.4f, n = (%.3f, %.3f), above: %s", t1, t2, t4, n1.x, n1.z, nappe ? "miss" : "HIT"),
               side && base && apexMiss && apexHit && nappe);
    }
    // ---------------------------------------------------------------- T264 transformed primitives: hit on the surface, normal = gradient
    {
        Rng r(42, 1);
        int hits = 0, bad = 0;
        float worstF = 0.0f, worstN = 0.0f;
        for (int i = 0; i < 4000; ++i) {
            RtScene s;
            const Shape sh = static_cast<Shape>(i % 4);
            const Mat4 w = translate({r.uniform() * 4 - 2, r.uniform() * 4 - 2, r.uniform() * 4 - 2}) *
                           rotateEulerYXZ({r.uniform() * 360, r.uniform() * 360, r.uniform() * 360}) *
                           scale({0.3f + r.uniform() * 2, 0.3f + r.uniform() * 2, 0.3f + r.uniform() * 2});
            s.addPrim(sh, w, diffuse(0.5f));
            s.build();
            Ray ray;
            ray.o = randomDir(r) * 8.0f;
            ray.d = normalize(transformPoint(w, {r.uniform() - 0.5f, r.uniform() - 0.5f, r.uniform() - 0.5f}) * 0.6f - ray.o);
            Hit h;
            if (!s.intersect(ray, h)) continue;
            ++hits;
            const Mat4 inv = inverse(w);
            const Vec3 pl = transformPoint(inv, h.p);
            const float F = std::fabs(unitShapeDistance(sh, pl));
            // numerical gradient of the implicit function in world space
            const float e = 1e-3f;
            auto f = [&](const Vec3& q) { return unitShapeDistance(sh, transformPoint(inv, q)); };
            const Vec3 g = normalize(Vec3{f(h.p + Vec3{e, 0, 0}) - f(h.p - Vec3{e, 0, 0}), f(h.p + Vec3{0, e, 0}) - f(h.p - Vec3{0, e, 0}),
                                          f(h.p + Vec3{0, 0, e}) - f(h.p - Vec3{0, 0, e})});
            const float dn = 1.0f - std::fabs(dot(g, h.ng));
            const bool nearEdge = [&] {   // the gradient is undefined on edges / rims / the apex
                const float ax = std::fabs(pl.x), ay = std::fabs(pl.y), az = std::fabs(pl.z), rr = std::sqrt(pl.x * pl.x + pl.y * pl.y);
                switch (sh) {
                    case Shape::Box: return (ax > 0.48f) + (ay > 0.48f) + (az > 0.48f) >= 2;
                    case Shape::Cylinder: return az > 0.48f && rr > 0.48f;
                    case Shape::Cone: return (pl.z < -0.48f && rr > 0.47f) || pl.z > 0.45f;
                    default: return false;
                }
            }();
            worstF = std::max(worstF, F);
            if (!nearEdge) worstN = std::max(worstN, dn);
            if (F > 1e-3f || (!nearEdge && dn > 2e-3f) || dot(h.n, ray.d) > 0.0f) ++bad;
        }
        record("T264", "Rotated, non-uniformly scaled primitives (4000 rays): hit point on the implicit surface, normal = its gradient",
               "|F(p)| < 1e-3, 1 - |n.grad| < 2e-3, n faces the ray", fmt("%d hits, max |F| = %.1e, max normal error %.1e, %d bad", hits, worstF, worstN, bad),
               hits > 1500 && bad == 0);
    }
    // ---------------------------------------------------------------- T265 terrain height field
    {
        Heightfield hf;
        hf.buildTerrain(289, 36.0f);
        Rng r(5, 9);
        int hits = 0, bad = 0, holes = 0;
        float worst = 0.0f;
        long long cells = 0;
        for (int i = 0; i < 2000; ++i) {
            const float x = r.uniform() * 50 - 25, z = r.uniform() * 50 - 25;
            Vec3 o{x + r.uniform() * 6 - 3, 30.0f, z + r.uniform() * 6 - 3};
            const Vec3 target{x, terrainHeight(x, z), z};
            const Vec3 d = normalize(target - o);
            float t;
            Vec3 n;
            const bool trench = std::fabs(x) < 1.85f && z > 3.7f && z < 13.0f;
            if (!hf.intersect(o, d, 1e-4f, 1e30f, t, n)) {
                holes += trench ? 1 : 0;
                if (!trench) ++bad;
                continue;
            }
            cells += hf.lastCells;
            ++hits;
            const Vec3 p = o + d * t;
            const float err = std::fabs(p.y - terrainHeight(p.x, p.z));
            worst = std::max(worst, err);
            if (err > 0.03f || n.y < 0.3f) ++bad;
        }
        record("T265", "Terrain height field (2-level DDA, 289^2 samples): hits lie on terrainHeight(); the trench is a hole",
               "|y - h(x,z)| < 3 cm on every hit", fmt("%d hits (%.1f cells / ray), worst %.1f mm, %d trench rays passed through, %d bad", hits,
                                                      hits ? static_cast<double>(cells) / hits : 0.0, worst * 1000, holes, bad),
               hits > 1700 && bad == 0);
    }
    // ---------------------------------------------------------------- T266 BVH == brute force
    {
        RtScene s;
        randomScene(s, 600, 11);
        s.build();
        Rng r(77, 2);
        int mismatch = 0, hits = 0;
        TraceStats st;
        for (int i = 0; i < 20000; ++i) {
            Ray ray;
            ray.o = randomDir(r) * 25.0f;
            ray.d = normalize(Vec3{r.uniform() * 20 - 10, r.uniform() * 20 - 10, r.uniform() * 20 - 10} - ray.o);
            Hit a, b;
            const bool ha = s.intersect(ray, a, &st), hb = s.intersectBrute(ray, b);
            hits += ha;
            if (ha != hb || (ha && (a.prim != b.prim || std::fabs(a.t - b.t) > 1e-5f))) ++mismatch;
        }
        record("T266", "SAH BVH closest hit == brute force over 600 random primitives (20000 random rays)",
               "0 mismatches", fmt("%d mismatches, %d hits, %.1f prim tests / ray (brute force: 600), %zu nodes, depth %d", mismatch, hits,
                                   static_cast<double>(st.primTests) / st.rays, s.bvh().nodes().size(), s.bvh().depth()),
               mismatch == 0 && hits > 5000);
    }
    // ---------------------------------------------------------------- T267 refit + SAH quality
    {
        RtScene s;
        randomScene(s, 600, 12);
        s.build();
        const float built = s.bvh().sahCost();
        Rng r(3, 3);
        for (RtPrim& p : s.prims) p.world = translate({r.uniform() - 0.5f, r.uniform() - 0.5f, r.uniform() - 0.5f}) * p.world;   // small moves
        s.prims[5].flags |= kHidden;
        const float ratio = s.refit();
        int mismatch = 0;
        for (int i = 0; i < 5000; ++i) {
            Ray ray;
            ray.o = randomDir(r) * 25.0f;
            ray.d = normalize(Vec3{r.uniform() * 20 - 10, r.uniform() * 20 - 10, r.uniform() * 20 - 10} - ray.o);
            Hit a, b;
            const bool ha = s.intersect(ray, a), hb = s.intersectBrute(ray, b);
            if (ha != hb || (ha && a.prim != b.prim)) ++mismatch;
            if (ha && a.prim == 5) ++mismatch;
        }
        std::vector<Aabb> bounds;
        for (const RtPrim& p : s.prims) bounds.push_back(shapeBounds(p.shape, p.world));
        Bvh median;
        median.buildMedian(bounds);
        Bvh sah;
        sah.build(bounds);
        record("T267", "BVH refit after moving every primitive (and hiding one) stays exact; SAH build beats a median split",
               "0 mismatches, hidden never hit, SAH cost < median cost", fmt("%d mismatches, refit cost ratio %.2f, SAH %.1f vs median %.1f (build %.1f)", mismatch,
                                                                              ratio, sah.sahCost(), median.sahCost(), built),
               mismatch == 0 && sah.sahCost() < median.sahCost());
    }
    // ---------------------------------------------------------------- T268 Fresnel
    {
        const float n = 1.5f;
        const float r0 = fresnelDielectric(1.0f, 1.0f / n);
        const float expect0 = (n - 1) * (n - 1) / ((n + 1) * (n + 1));
        const float grazing = fresnelDielectric(0.001f, 1.0f / n);
        // Brewster angle: tan(theta_B) = n, p-polarised reflectance 0 -> R = rs^2 / 2
        const float thetaB = std::atan(n);
        const float ci = std::cos(thetaB), ct = std::cos(std::asin(std::sin(thetaB) / n));
        const float rs = (ci - n * ct) / (ci + n * ct);
        const float brewster = fresnelDielectric(ci, 1.0f / n);
        const float schlick = fresnelSchlick(0.5f, expect0);
        const float exactHalf = fresnelDielectric(0.5f, 1.0f / n);
        // symmetry: same transmitted fraction from inside at the refracted angle
        const float inside = fresnelDielectric(ct, n);
        const bool ok = std::fabs(r0 - expect0) < 1e-6f && grazing > 0.99f && std::fabs(brewster - 0.5f * rs * rs) < 1e-5f &&
                        std::fabs(schlick - exactHalf) < 0.025f && std::fabs(inside - brewster) < 1e-4f;
        record("T268", "Fresnel (glass n = 1.5): normal incidence ((n-1)/(n+1))^2, grazing -> 1, Brewster angle, Schlick vs exact, reciprocity",
               "R0 = 0.04, R(89.9 deg) > 0.99, R_B = rs^2/2, |Schlick - exact| < 0.025 at 60 deg, R_in = R_out",
               fmt("R0 = %.5f, grazing %.4f, Brewster %.5f (rs^2/2 = %.5f), Schlick %.4f vs %.4f, inside %.5f", r0, grazing, brewster,
                   0.5f * rs * rs, schlick, exactHalf, inside), ok);
    }
    // ---------------------------------------------------------------- T269 Snell
    {
        Rng r(9, 9);
        float worst = 0.0f;
        int tirOk = 0, tirTotal = 0;
        for (int i = 0; i < 1000; ++i) {
            const float th = r.uniform() * 1.5f;
            const Vec3 d = normalize(Vec3{std::sin(th), -std::cos(th), 0.0f});
            const Vec3 nrm{0, 1, 0};
            Vec3 t;
            if (refractDir(d, nrm, 1.0f / 1.33f, t)) {
                const float s1 = std::sin(th), s2 = std::sqrt(t.x * t.x + t.z * t.z);
                worst = std::max(worst, std::fabs(1.0f * s1 - 1.33f * s2));
            }
            // from inside water: TIR beyond the critical angle asin(1/1.33) = 48.75 deg
            const bool beyond = th > std::asin(1.0f / 1.33f) + 1e-3f, below = th < std::asin(1.0f / 1.33f) - 1e-3f;
            if (beyond || below) {
                ++tirTotal;
                const bool refr = refractDir(d, nrm, 1.33f, t);
                tirOk += (beyond && !refr && fresnelDielectric(std::cos(th), 1.33f) == 1.0f) || (below && refr);
            }
        }
        record("T269", "Snell's law n1 sin(t1) = n2 sin(t2) (air -> water) and total internal reflection beyond 48.75 deg (water -> air)",
               "|n1 s1 - n2 s2| < 1e-5, TIR exactly beyond the critical angle", fmt("max error %.1e, TIR classification %d / %d", worst, tirOk, tirTotal),
               worst < 1e-5f && tirOk == tirTotal);
    }
    // ---------------------------------------------------------------- T270 Whitted: mirror + glass ray tree
    {
        RtScene s;
        RtMaterial glass;
        glass.transmission = 1.0f;
        glass.ior = 1.5f;
        RtMaterial mirror;
        mirror.albedo = {0.95f, 0.95f, 0.95f};
        mirror.metalness = 1.0f;
        mirror.roughness = 0.05f;
        s.addPrim(Shape::Sphere, translate({-1.2f, 1, 0}) * scale({2, 2, 2}), glass, 0, -1, "glass ball");
        s.addPrim(Shape::Sphere, translate({1.2f, 1, 0}) * scale({2, 2, 2}), mirror, 0, -1, "mirror ball");
        s.addPrim(Shape::Box, translate({0, -0.5f, 0}) * scale({20, 1, 20}), diffuse(0.6f), 0, -1, "floor");
        RtLight sun;
        sun.type = LightType::Directional;
        sun.direction = normalize(Vec3{-0.3f, -1, -0.4f});
        sun.color = {1, 1, 1};
        s.lights.push_back(sun);
        s.build();
        RtCamera cam = RtCamera::fromView(lookAt({0, 1.5f, 6}, {0, 1, 0}, {0, 1, 0}), perspective(radians(50), 1.0f, 0.1f, 100));
        float u, v;
        cam.project({-1.2f, 1, 0}, u, v);
        const RayTree g = debugTrace(s, cam, u, v);
        int refr = 0, refl = 0, shadow = 0, maxDepth = 0;
        for (const RayNode& n : g.nodes) {
            refr += n.kind == RayKind::Refraction;
            refl += n.kind == RayKind::Reflection;
            shadow += n.kind == RayKind::Shadow;
            maxDepth = std::max(maxDepth, n.depth);
        }
        cam.project({1.2f, 1, 0}, u, v);
        const RayTree m = debugTrace(s, cam, u, v);
        const bool mirrorRefl = m.nodes.size() > 1 && m.nodes[0].object == "mirror ball" && [&] {
            for (const RayNode& n : m.nodes) if (n.kind == RayKind::Reflection && n.parent == 0) return true;
            return false;
        }();
        const Tracer tr(s);
        const Vec3 direct = tr.whitted(cam.primary(u, v));
        const bool same = std::fabs(luminance(direct) - luminance(m.radiance)) < 1e-5f;
        // traceReflection (Frame Explorer API): mirror law R = D - 2 (D.N) N, |R| = 1, angle in = angle out
        const ReflectionSample rs = traceReflection(s, cam, u, v);
        const bool mirrorLaw = rs.hit && std::fabs(length(rs.reflectDir) - 1.0f) < 1e-4f &&
                               std::fabs(dot(rs.reflectDir, rs.normal) + dot(rs.view, rs.normal)) < 1e-4f && rs.surface == "mirror ball";
        record("T270", "Whitted ray tree: glass ball spawns reflection + refraction children (depth limited), mirror ball a reflection ray",
               "refraction >= 2, reflection >= 1, depth <= 5, mirror reflection child, tree radiance = whitted(), traceReflection obeys the mirror law",
               fmt("glass: %zu rays (%d refr, %d refl, %d shadow, depth %d); mirror: %zu rays, radiance %.4f vs %.4f", g.nodes.size(), refr, refl,
                   shadow, maxDepth, m.nodes.size(), luminance(m.radiance), luminance(direct)),
               g.nodes[0].object == "glass ball" && refr >= 2 && refl >= 1 && maxDepth <= 5 && mirrorRefl && same && mirrorLaw);
    }
    // ---------------------------------------------------------------- T271 furnace: convex white / grey sphere under a uniform sky
    {
        float res[2];
        for (int k = 0; k < 2; ++k) {
            RtScene s;
            s.addPrim(Shape::Sphere, scale({2, 2, 2}), diffuse(k == 0 ? 1.0f : 0.5f));
            s.env = RtEnv::uniform(1.0f);
            s.build();
            const RtCamera cam = RtCamera::fromView(lookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), perspective(radians(25), 1.0f, 0.1f, 100));
            Image img(32, 32);
            renderPath(s, cam, img, 32, 5, {}, nullptr, nullptr, nullptr, 1);
            res[k] = luminance(img.at(16, 16) + img.at(12, 14) + img.at(18, 20) + img.at(15, 11)) / 4.0f;
        }
        record("T271", "White furnace: a convex diffuse sphere (albedo 1 / 0.5) under a uniform sky of radiance 1 returns albedo * 1",
               "1.000 +- 0.02, 0.500 +- 0.02", fmt("%.4f, %.4f", res[0], res[1]), std::fabs(res[0] - 1.0f) < 0.02f && std::fabs(res[1] - 0.5f) < 0.02f);
    }
    // ---------------------------------------------------------------- T272 closed furnace: multiple bounces + Russian roulette
    {
        // Camera inside a closed box whose walls emit Le = 0.1 and reflect with albedo 0.7: L = Le / (1 - a) = 0.3333
        RtScene s;
        RtMaterial wall = diffuse(0.7f);
        wall.emission = {0.1f, 0.1f, 0.1f};
        const float k = 4.0f;
        s.addPrim(Shape::Box, translate({0, -k, 0}) * scale({2 * k, 0.5f, 2 * k}), wall);
        s.addPrim(Shape::Box, translate({0, k, 0}) * scale({2 * k, 0.5f, 2 * k}), wall);
        s.addPrim(Shape::Box, translate({-k, 0, 0}) * scale({0.5f, 2 * k, 2 * k}), wall);
        s.addPrim(Shape::Box, translate({k, 0, 0}) * scale({0.5f, 2 * k, 2 * k}), wall);
        s.addPrim(Shape::Box, translate({0, 0, -k}) * scale({2 * k, 2 * k, 0.5f}), wall);
        s.addPrim(Shape::Box, translate({0, 0, k}) * scale({2 * k, 2 * k, 0.5f}), wall);
        s.env = RtEnv::uniform(0.0f);
        s.build();
        TraceSettings ts;
        ts.maxBounces = 60;
        ts.rrStart = 2;
        const RtCamera cam = RtCamera::fromView(lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0}), perspective(radians(60), 1.0f, 0.1f, 100));
        Image img(16, 16);
        renderPath(s, cam, img, 64, 9, ts, nullptr, nullptr, nullptr, 1);
        const float mean = luminance(img.mean());
        record("T272", "Closed furnace (emitting walls Le = 0.1, albedo 0.7, Russian roulette from bounce 2): L = Le / (1 - a)",
               "0.3333 +- 2%", fmt("%.4f", mean), std::fabs(mean - 1.0f / 3.0f) < 0.0067f);
    }
    // ---------------------------------------------------------------- T273 importance sampling unbiased (GGX lobe + cosine)
    {
        // Estimate the directional albedo of a glossy material by BSDF sampling and by uniform hemisphere sampling.
        RtMaterial m;
        m.albedo = {0.5f, 0.5f, 0.5f};
        m.roughness = 0.35f;
        m.metalness = 0.3f;
        Hit h;
        h.n = {0, 0, 1};
        h.ng = h.n;
        const Vec3 wo = normalize(Vec3{0.5f, 0.0f, 0.8f});
        Rng r(21, 4);
        double imp = 0.0, uni = 0.0;
        const int N = 400000;
        for (int i = 0; i < N; ++i) {
            BsdfSample bs;
            if (sampleBsdf(m, h, wo, r, bs)) imp += luminance(bs.weight);
            Vec3 d = sampleSphere(r.uniform(), r.uniform());
            d.z = std::fabs(d.z);
            uni += luminance(evalBsdf(m, h.n, wo, d)) * d.z * 2.0 * PI;
        }
        imp /= N;
        uni /= N;
        // cosine sampling alone: pdf integrates to 1 -> mean of cos/pdf = pi * E[1/pi] = 1 for a white Lambertian
        RtMaterial white = diffuse(1.0f);
        double lam = 0.0;
        for (int i = 0; i < 100000; ++i) {
            BsdfSample bs;
            white.roughness = 1.0f;
            if (sampleBsdf(white, h, {0, 0, 1}, r, bs)) lam += luminance(bs.weight);
        }
        lam /= 100000;
        record("T273", "Importance sampling is unbiased: GGX + cosine one-sample mix == uniform hemisphere estimate of the albedo",
               "relative difference < 1%", fmt("importance %.4f, uniform %.4f, white Lambert (cosine sampling) %.4f", imp, uni, lam),
               std::fabs(imp - uni) / uni < 0.01 && lam > 0.9 && lam < 1.01);
    }
    // ---------------------------------------------------------------- T274 light tracing == path tracing
    {
        RtScene s;
        s.addPrim(Shape::Box, translate({0, -0.5f, 0}) * scale({12, 1, 12}), diffuse(0.6f));
        s.addPrim(Shape::Box, translate({0.6f, 0.5f, 0}) * scale({1, 1, 1}), diffuse(0.8f));
        s.addPrim(Shape::Sphere, translate({-0.9f, 0.6f, 0.3f}) * scale({1.2f, 1.2f, 1.2f}), diffuse(0.4f));
        RtLight lamp;
        lamp.type = LightType::Point;
        lamp.position = {0.5f, 3.0f, 1.5f};
        lamp.color = {1, 1, 1};
        lamp.linear = 0.1f;
        lamp.quadratic = 0.3f;
        s.lights.push_back(lamp);
        s.env = RtEnv::uniform(0.0f);
        s.build();
        TraceSettings ts;
        ts.maxBounces = 4;
        const RtCamera cam = RtCamera::fromView(lookAt({0, 3, 5}, {0, 0.3f, 0}, {0, 1, 0}), perspective(radians(45), 1.0f, 0.1f, 100));
        Image pt(24, 24), lt(24, 24);
        renderPath(s, cam, pt, 256, 3, ts);
        renderLight(s, cam, lt, 3000000, 4, ts);
        // compare 4x4 block means (light tracing is noisy per pixel)
        double worst = 0.0;
        for (int by = 0; by < 6; ++by)
            for (int bx = 0; bx < 6; ++bx) {
                double a = 0, b = 0;
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        a += luminance(pt.at(bx * 4 + x, by * 4 + y));
                        b += luminance(lt.at(bx * 4 + x, by * 4 + y));
                    }
                if (a > 0.05) worst = std::max(worst, std::fabs(a - b) / a);
            }
        const float mp = luminance(pt.mean()), ml = luminance(lt.mean());
        record("T274", "Light tracing (reverse: particles from the lamp, camera connections) agrees with path tracing on a small scene",
               "image means within 3%, every lit 4x4 block within 15%",
               fmt("path %.4f, light %.4f (%.1f%%), worst block %.1f%%", mp, ml, 100.0 * std::fabs(mp - ml) / mp, 100.0 * worst),
               std::fabs(mp - ml) / mp < 0.03f && worst < 0.15);
    }
    // ---------------------------------------------------------------- T275 determinism with a seed
    {
        RtScene s;
        randomScene(s, 80, 31);
        s.addPrim(Shape::Box, translate({0, -11, 0}) * scale({40, 1, 40}), diffuse(0.5f));
        RtLight sun;
        sun.type = LightType::Directional;
        sun.direction = normalize(Vec3{0.2f, -1, 0.3f});
        sun.color = {1, 1, 1};
        sun.radius = 0.02f;
        s.lights.push_back(sun);
        s.build();
        const RtCamera cam = RtCamera::fromView(lookAt({0, 5, 30}, {0, 0, 0}, {0, 1, 0}), perspective(radians(50), 1.0f, 0.1f, 100));
        Image a(40, 40), b(40, 40), c(40, 40);
        renderPath(s, cam, a, 4, 1234, {}, nullptr, nullptr, nullptr, 1);
        renderPath(s, cam, b, 4, 1234, {}, nullptr, nullptr, nullptr, 7);
        renderPath(s, cam, c, 4, 4321, {}, nullptr, nullptr, nullptr, 7);
        const bool same = std::memcmp(a.px.data(), b.px.data(), a.px.size() * sizeof(Vec3)) == 0;
        const bool differ = std::memcmp(a.px.data(), c.px.data(), a.px.size() * sizeof(Vec3)) != 0;
        record("T275", "Path tracer determinism: same seed -> bit-identical image (1 vs 7 threads); another seed -> different noise",
               "identical, different", fmt("%s, %s", same ? "identical" : "DIFFERENT", differ ? "different" : "IDENTICAL"), same && differ);
    }
    // ---------------------------------------------------------------- T276 bridge on the real camp
    {
        Scene scene;
        scene.update(0.1f, Detection{});
        SceneBridge br;
        RtScene rs;
        LightOptions lo;
        br.sync(scene, lo, rs);
        int traced = 0;
        for (const SceneNode* n : scene.leaves())
            if (n->primitive != PrimitiveType::BeamCone && n->primitive != PrimitiveType::TerrainDisc && n->material.opacity >= 1.0f) ++traced;
        // A ray from outside aimed at each visible primitive's centre must hit something no farther than that centre.
        int checked = 0, bad = 0;
        for (size_t i = 0; i < rs.prims.size(); i += 7) {
            const RtPrim& p = rs.prims[i];
            if (p.flags & (kHidden | kSkyBody | kFoliage)) continue;   // foliage has gaps by design
            const Vec3 c = transformPoint(p.world, {0, 0, 0});
            Ray ray;
            ray.o = c + normalize(Vec3{0.3f, 1.0f, 0.2f}) * 60.0f;
            ray.d = normalize(c - ray.o);
            Hit h;
            ++checked;
            if (!rs.intersect(ray, h) || h.t > length(c - ray.o) + 1e-3f) ++bad;
        }
        const bool moved = [&] {   // a refit follows the scene: advance and compare a dynamic part
            for (int k = 0; k < 120; ++k) scene.update(1.0f / 30.0f, Detection{});
            const bool rebuilt = br.sync(scene, lo, rs);
            for (const RtPrim& p : rs.prims)
                if (p.flags & kDynamic) {
                    const SceneNode* leaf = br.leafOf(rs, static_cast<int>(&p - rs.prims.data()));
                    if (!leaf) return false;
                    for (int k = 0; k < 16; ++k)
                        if (leaf->world.m[k] != p.world.m[k]) return false;
                }
            (void)rebuilt;
            return true;
        }();
        record("T276", "Scene bridge on the camp: one analytic primitive per solid leaf, every primitive reachable, refit follows the moving parts",
               "prims == solid leaves, 0 misses, dynamic matrices in sync", fmt("%zu prims / %d solid leaves, %zu lights, %d / %d centre rays ok, BVH depth %d, refit ratio %.2f",
                                                                                rs.prims.size(), traced, rs.lights.size(), checked - bad, checked,
                                                                                rs.bvh().depth(), br.lastRefitRatio()),
               static_cast<int>(rs.prims.size()) == traced && bad == 0 && moved && rs.terrain() != nullptr);
    }
    // ---------------------------------------------------------------- T277 ray tree bookkeeping
    {
        RtScene s;
        s.addPrim(Shape::Box, translate({0, -0.5f, 0}) * scale({10, 1, 10}), diffuse(0.5f), 0, -1, "floor");
        s.addPrim(Shape::Cylinder, translate({0, 1, -1}) * rotateX(radians(90)) * scale({1, 1, 2}), diffuse(0.5f), 0, -1, "post");
        RtLight a, b;
        a.type = LightType::Directional;
        a.direction = normalize(Vec3{0, -1, -0.2f});
        a.color = {0.5f, 0.5f, 0.5f};
        b.type = LightType::Point;
        b.position = {1, 2, 1};
        b.color = {1, 0.5f, 0.2f};
        s.lights = {a, b};
        s.build();
        const RtCamera cam = RtCamera::fromView(lookAt({0, 2, 4}, {0, 0, 0}, {0, 1, 0}), perspective(radians(50), 1.0f, 0.1f, 100));
        const RayTree t = debugTrace(s, cam, 0.5f, 0.6f);
        int shadows = 0, parentsOk = 1;
        Vec3 sumShadow;
        for (size_t i = 0; i < t.nodes.size(); ++i) {
            const RayNode& n = t.nodes[i];
            if (i > 0 && (n.parent < 0 || n.parent >= static_cast<int>(i))) parentsOk = 0;
            if (n.kind == RayKind::Shadow && n.parent == 0) {
                ++shadows;
                sumShadow = sumShadow + n.contribution;
            }
        }
        // primary radiance = emission(0) + direct (sum of its shadow-ray contributions) + ambient (env = night default)
        const Tracer tr(s);
        Hit h;
        s.intersect(cam.primary(0.5f, 0.6f), h);
        const RtMaterial& m = s.material(h);
        const Vec3 amb = mul(m.albedo, tr.ambient(h.n));
        const float err = std::fabs(luminance(sumShadow + amb) - luminance(t.radiance));
        record("T277", "Ray debugger tree: primary first, parents precede children, one shadow ray per light, contributions add up to the pixel",
               "2 shadow rays, |direct + ambient - radiance| < 1e-5", fmt("%zu rays (first %s on %s), %d shadow rays, error %.1e", t.nodes.size(),
                                                                         rayKindName(t.nodes[0].kind), t.nodes[0].object.c_str(), shadows, err),
               t.nodes[0].kind == RayKind::Primary && parentsOk && shadows == 2 && err < 1e-5f);
    }
    // ---------------------------------------------------------------- T278 modes + GPU layout
    {
        RtMode m = RtMode::Raster;
        std::string order;
        for (int i = 0; i < kRtModeCount; ++i) {
            order += rtModeName(m);
            order += i + 1 < kRtModeCount ? " > " : "";
            m = nextRtMode(m);
        }
        RtScene s;
        randomScene(s, 50, 2);
        s.build();
        GpuScene g;
        packScene(s, g);
        const bool sizes = sizeof(GpuPrim) == 64 && sizeof(GpuMaterial) == 48 && sizeof(GpuNode) == 32 && sizeof(GpuLight) == 64;
        // the packed inverse rows reproduce the local coordinates of a world point
        const Vec3 wp{0.3f, -1.2f, 2.0f};
        const Vec3 lp = transformPoint(s.prims[7].inv, wp);
        const GpuPrim& gp = g.prims[7];
        const float lx = gp.inv[0] * wp.x + gp.inv[1] * wp.y + gp.inv[2] * wp.z + gp.inv[3];
        const float lz = gp.inv[8] * wp.x + gp.inv[9] * wp.y + gp.inv[10] * wp.z + gp.inv[11];
        const bool rows = std::fabs(lx - lp.x) < 1e-5f && std::fabs(lz - lp.z) < 1e-5f;
        const bool nodes = g.nodes.size() == s.bvh().nodes().size() && g.nodes[0].count == 0;
        bool helpKeys = false, tableKeys = false;   // ';' and ''' in the F1 help and the v7 key table
        for (const HelpLine& l : helpLines()) helpKeys = helpKeys || (l.text[0] == ';' || l.text[0] == '\'');
        int keyRows = 0;
        for (const KeyBinding& k : keyBindingsV7()) keyRows += std::string(k.keys) == ";" || std::string(k.keys) == "'";
        tableKeys = keyRows == 2;
        record("T278", "Render modes cycle raster -> hybrid -> Whitted -> path -> light; std430 GPU records (64/48/32/64 bytes) pack the scene",
               "RASTER first and back to RASTER, record sizes, inverse rows reproduce local coordinates, ; and ' in help + key table",
               fmt("%s > %s; sizes %zu/%zu/%zu/%zu; rows %s; %zu nodes; keys help %d table %d", order.c_str(), rtModeName(m), sizeof(GpuPrim),
                   sizeof(GpuMaterial), sizeof(GpuNode), sizeof(GpuLight), rows ? "ok" : "WRONG", g.nodes.size(), helpKeys, tableKeys),
               m == RtMode::Raster && sizes && rows && nodes && helpKeys && tableKeys);
    }
    // ---------------------------------------------------------------- T279 a-trous denoiser
    {
        // A flat grey wall left, a bright wall right (depth / normal edge); noisy 1 spp-like values.
        const int w = 48, h = 32;
        Image c(w, h), al(w, h), nm(w, h), clean(w, h);
        std::vector<float> dp(static_cast<size_t>(w) * h);
        Rng r(4, 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const bool right = x >= w / 2;
                const float v = right ? 0.8f : 0.2f;
                clean.at(x, y) = {v, v, v};
                const float noise = (r.uniform() - 0.5f) * 0.8f * v;
                c.at(x, y) = {v + noise, v + noise, v + noise};
                al.at(x, y) = {0.5f, 0.5f, 0.5f};
                nm.at(x, y) = right ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
                dp[static_cast<size_t>(y) * w + x] = right ? 3.0f : 5.0f;
            }
        auto rmse = [&](const Image& a) {
            double e = 0.0;
            for (int i = 0; i < w * h; ++i) e += std::pow(a.px[i].x - clean.px[i].x, 2.0);
            return std::sqrt(e / (w * h));
        };
        const double before = rmse(c);
        denoiseAtrous(c, al, nm, dp, 5, 0.6f);
        const double after = rmse(c);
        const float edgeL = c.at(w / 2 - 1, h / 2).x, edgeR = c.at(w / 2, h / 2).x;
        record("T279", "Edge-aware a-trous denoiser: removes most of the noise but keeps the normal / depth edge sharp",
               "RMSE drops > 3x, pixels next to the edge keep their side's value (+-0.05)",
               fmt("RMSE %.4f -> %.4f, edge %.3f | %.3f (truth 0.2 | 0.8)", before, after, edgeL, edgeR),
               after * 3.0 < before && std::fabs(edgeL - 0.2f) < 0.05f && std::fabs(edgeR - 0.8f) < 0.05f);
    }
}
