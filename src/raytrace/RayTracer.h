// CPU reference ray tracers over an RtScene (the GPU tracer in rendering/RayTracerGL runs the same algorithms):
//   Whitted   recursive: emission + direct light with shadow rays (glass casts tinted shadows) + ambient sky,
//             mirror reflection weighted by Schlick's Fresnel, dielectric reflection + Snell refraction with the
//             exact Fresnel split (total internal reflection), depth limit.
//   Path      unidirectional path tracing: next-event estimation to every light (shadow rays), cosine / GGX
//             importance sampling of the BSDF (one-sample mix of the two lobes), Russian roulette after a few
//             bounces, sky as the environment. Unbiased for the lights it samples (the energy tests check this).
//   Light     "reverse" ray tracing: particle (light) tracing from the sun / moon, the searchlight, the beacon, the
//             lamps and the sky; every non-specular vertex is connected to the pinhole camera and splatted into the
//             image (caustics through glass and mirrors come for free); plus a photon map for the figures.
// The ray debugger (debugTrace) records the Whitted ray tree of one pixel: every primary, shadow, reflection and
// refraction ray with its depth, length and radiance contribution, plus one sampled diffuse bounce for information.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "raytrace/RtScene.h"

namespace ts::rt {

struct TraceSettings {
    int whittedDepth = 5;       // recursion limit (reflection + refraction)
    int maxBounces = 6;         // path / light tracing
    int rrStart = 3;            // Russian roulette from this bounce on
    bool softShadows = true;    // jitter shadow-ray targets over the light's size (needs an Rng)
    bool ambientOcclusion = false;  // Whitted: one short occlusion ray for the ambient term (needs an Rng)
    float emitRadius = 60.0f;   // light tracing: radius of the disc the sun / moon / sky photons start from
    Vec3 emitCentre{0, 0, 0};
};

enum class RayKind { Primary = 0, Shadow, Reflection, Refraction, Bounce, Light, Connect };
const char* rayKindName(RayKind k);

struct RayNode {
    RayKind kind = RayKind::Primary;
    int parent = -1;
    int depth = 0;
    Vec3 from, to;              // to = hit point, light position or a point far along the ray
    float t = 0.0f;
    bool hit = false;           // primary / secondary: hit a surface; shadow: the light is visible
    Vec3 weight{1, 1, 1};       // path throughput carried by this ray
    Vec3 contribution;          // radiance this ray adds to the pixel (weight * what it brought back)
    std::string object;         // what it hit (or the light / "sky")
    bool informational = false; // not part of the Whitted sum (the sampled bounce)
};

struct RayTree {
    std::vector<RayNode> nodes;
    Vec3 radiance;              // Whitted radiance of the pixel (sum over the non-informational tree)
    float u = 0.0f, v = 0.0f;
};

// Light seen from a surface point: unit direction to it, irradiance E (physical: pi * the raster colour, with the
// distance attenuation and cone factors), distance (1e30 for directional).
struct LightSample {
    Vec3 wi, E;
    float dist = 1e30f;
};
bool sampleLight(const RtLight& l, const Vec3& p, float u1, float u2, bool soft, LightSample& s);

// Physical BSDF value f(wo, wi) of an opaque material (1/sr): Lambert * (1 - F)(1 - metal) + GGX Cook-Torrance.
Vec3 evalBsdf(const RtMaterial& m, const Vec3& n, const Vec3& wo, const Vec3& wi);
struct BsdfSample {
    Vec3 wi;
    Vec3 weight;          // f cos / pdf (or the Fresnel-weighted tint for glass)
    bool specular = false;  // delta lobe (glass) or a near-mirror GGX sample
};
bool sampleBsdf(const RtMaterial& m, const Hit& h, const Vec3& wo, Rng& rng, BsdfSample& s);
// Probability of picking the GGX lobe (the rest goes to the cosine lobe).
float specularProbability(const RtMaterial& m, const Vec3& n, const Vec3& wo);

struct PixelAov {
    Vec3 albedo, normal;
    float depth = 1e30f;
};

class Tracer {
public:
    explicit Tracer(const RtScene& scene, TraceSettings ts = {}) : s_(scene), ts_(ts) {}
    // Whitted ray tracing. rng only for soft shadows / AO (nullptr = deterministic hard shadows).
    Vec3 whitted(const Ray& r, Rng* rng = nullptr, RayTree* tree = nullptr) const;
    // One path-traced radiance sample.
    Vec3 path(const Ray& r, Rng& rng, PixelAov* aov = nullptr) const;
    // One light path; splat(u, v, value) per camera connection, value already includes the pinhole importance for
    // a w x h image: the pixel estimate is (sum of its splats) / (number of light paths).
    void lightPath(const RtCamera& cam, int w, int h, Rng& rng, const std::function<void(float, float, const Vec3&)>& splat) const;
    // Picks a light (or the sky, index = lights.size()) proportional to its estimated power; pdf out.
    int pickEmitter(float u, float& pdf) const;
    // Direct light from every light at a surface point (shadow rays).
    Vec3 directLight(const Hit& h, const RtMaterial& m, const Vec3& wo, Rng* rng, RayTree* tree = nullptr, int parent = -1,
                     const Vec3& weight = {1, 1, 1}, int depth = 0) const;
    // Estimated emitted power of every light, then the sky (light tracing picks emitters proportionally).
    std::vector<float> emitterPower() const;
    Vec3 sky(const Vec3& d) const;          // primary misses / mirror reflections
    Vec3 ambient(const Vec3& n) const;      // raster-equivalent hemispherical ambient
    const TraceSettings& settings() const { return ts_; }
    mutable TraceStats stats;

private:
    Vec3 whittedRec(const Ray& r, int depth, Rng* rng, RayTree* tree, int parent, const Vec3& weight, RayKind kind) const;
    const RtScene& s_;
    TraceSettings ts_;
};

// --- images ---
struct Image {
    int w = 0, h = 0;
    std::vector<Vec3> px;
    Image() = default;
    Image(int W, int H) : w(W), h(H), px(static_cast<size_t>(W) * H) {}
    Vec3& at(int x, int y) { return px[static_cast<size_t>(y) * w + x]; }
    const Vec3& at(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
    Vec3 mean() const;
};

// Multithreaded renderers (threads = 0: all cores). Deterministic for a given seed regardless of the thread count.
void renderWhitted(const RtScene& s, const RtCamera& cam, Image& out, int spp = 1, uint32_t seed = 1, const TraceSettings& ts = {},
                   int threads = 0);
void renderPath(const RtScene& s, const RtCamera& cam, Image& out, int spp, uint32_t seed, const TraceSettings& ts = {},
                Image* albedo = nullptr, Image* normal = nullptr, std::vector<float>* depth = nullptr, int threads = 0);
void renderLight(const RtScene& s, const RtCamera& cam, Image& out, long long paths, uint32_t seed, const TraceSettings& ts = {},
                 int threads = 0);
// Edge-aware a-trous wavelet filter (Dammertz et al. 2010) on the albedo-demodulated colour, guided by normal and
// depth; 'iterations' passes with hole spacing 1, 2, 4, ...
void denoiseAtrous(Image& color, const Image& albedo, const Image& normal, const std::vector<float>& depth, int iterations = 5,
                   float sigmaColor = 0.6f);

// Ray debugger: the Whitted ray tree of the pixel at (u, v) (plus one informational diffuse bounce).
RayTree debugTrace(const RtScene& s, const RtCamera& cam, float u, float v, const TraceSettings& ts = {});

// One mirror reflection seen through the pixel (u, v) (v = 0 at the top): the primary hit, the mirror-law direction
// R = D - 2 (D.N) N, what R hits and the radiance it brings back (Whitted, depth-limited), the Schlick Fresnel weight of
// the surface. For the Frame Explorer's reflection stage (ray-traced mirror vs SSR) and other explanations.
struct ReflectionSample {
    bool hit = false;            // the primary ray hit a surface
    Vec3 point, normal, view;    // primary hit, its normal (facing the camera), unit view direction D
    Vec3 reflectDir;             // R
    bool reflectedHit = false;
    Vec3 reflectedPoint;         // where R lands (or 40 m along R)
    std::string surface, reflectedObject;
    Vec3 radiance;               // Whitted radiance arriving along -R
    float fresnel = 0.0f;        // Schlick F(cos) with the surface's F0 (luminance)
};
ReflectionSample traceReflection(const RtScene& s, const RtCamera& cam, float u, float v, const TraceSettings& ts = {});

// Photon map (figures): photons shot from the lights, stored at every non-specular hit.
struct Photon {
    Vec3 p, power, dir;
    int bounce = 0;
    bool caustic = false;   // the path went through glass / a mirror before landing
};
void tracePhotons(const RtScene& s, int count, uint32_t seed, std::vector<Photon>& out, const TraceSettings& ts = {});

}  // namespace ts::rt
