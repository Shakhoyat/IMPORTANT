// Ray tracing core types (CPU reference, shared layout with the GPU tracer in rendering/RayTracerGL).
// The scene is built only from analytic primitives (unit cube, sphere, cylinder, cone) plus the terrain height field,
// so every intersection is exact: a ray is moved into the primitive's local space (inverse world matrix, the direction
// is NOT renormalised so t stays in world units), intersected with the unit shape, and the normal is brought back with
// the inverse transpose. No triangles are involved except the two per height-field cell of the terrain.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/Math3D.h"

namespace ts::rt {

// Render modes cycled with ';' (raster first: the v6 renderer unchanged).
enum class RtMode { Raster = 0, Hybrid = 1, Whitted = 2, PathTrace = 3, LightTrace = 4 };
constexpr int kRtModeCount = 5;
const char* rtModeName(RtMode m);       // "RASTER", "HYBRID RT", ...
const char* rtModeCaption(RtMode m);    // one line: what the mode does
inline RtMode nextRtMode(RtMode m) { return static_cast<RtMode>((static_cast<int>(m) + 1) % kRtModeCount); }

// Unit shapes in local space (same as the raster meshes in primitives/):
//   Box      [-0.5, 0.5]^3                          Sphere   radius 0.5 at the origin
//   Cylinder radius 0.5, axis +Z, z in [-0.5, 0.5], both caps
//   Cone     base radius 0.5 at z = -0.5 (capped), apex at z = +0.5: r(z) = 0.25 - 0.5 z
//   Heightfield: world-space terrain grid (one per scene), see Heightfield.h
enum class Shape : int { Sphere = 0, Box = 1, Cylinder = 2, Cone = 3, Heightfield = 4 };
const char* shapeName(Shape s);

struct Ray {
    Vec3 o, d;              // d need not be normalised for intersection; tracers keep it unit length
    float tmin = 1e-4f, tmax = 1e30f;
};

struct Hit {
    float t = 1e30f;
    int prim = -1;          // index into RtScene::prims (BVH order)
    Vec3 p;                 // world position
    Vec3 n;                 // unit world normal, facing the side the ray came from
    Vec3 ng;                // unit geometric normal, outward (not flipped)
    bool front = true;      // the ray hit the outside of the surface
    bool valid() const { return prim >= 0; }
};

// Surface model shared by all tracers ("uber" material):
//   opaque: Lambert (1 - metal)(1 - F) + GGX microfacet specular, F0 = mix(specular, albedo, metal)
//   glass (transmission > 0): smooth dielectric, Fresnel-weighted reflection + Snell refraction, tinted by 'tint'
//   emission: radiance emitted (light proxies of the practical lamps are flagged, see PrimFlag)
struct RtMaterial {
    Vec3 albedo{0.5f, 0.5f, 0.5f};   // linear
    float roughness = 0.5f;          // perceptual (alpha = roughness^2)
    float metalness = 0.0f;
    float specular = 0.04f;          // F0 of the dielectric (non-metal) part; 0 = pure Lambert (furnace tests)
    float transmission = 0.0f;       // 1 = clear dielectric
    float ior = 1.5f;
    Vec3 tint{1, 1, 1};              // transmission colour per interface crossing
    Vec3 emission{0, 0, 0};          // radiance
    int surface = 0;                 // ts::Surface (procedural pattern id, for cheap variations)
};

enum PrimFlag : uint32_t {
    kNoShadow = 1u,        // does not occlude shadow / connection rays (sky bodies)
    kLightProxy = 2u,      // emissive part of a practical light: its emission is counted through NEE, not by paths
    kDynamic = 4u,         // moves: refit every frame
    kHidden = 8u,          // invisible this frame (fired missile, removed wreck): empty box, never hit
    kSkyBody = 16u,        // moon / sun ball
    kLightHolder = 32u,    // part of the UAV that carries the searchlight: ignored by the searchlight's shadow rays
    kFoliage = 64u,        // leaves / needles / camouflage net: alpha-tested by 3D value noise (leafCovered)
};

// PCG hash (same as the GLSL pcg()), 3D value noise and the foliage coverage pattern shared by the CPU and GPU
// tracers: a local-space point of a foliage primitive is solid where valueNoise3(11 p) > 0.40 (~62 % covered).
inline uint32_t pcgHash(uint32_t v) {
    const uint32_t s = v * 747796405u + 2891336453u;
    const uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
inline float latticeHash(int x, int y, int z) {
    return static_cast<float>(pcgHash(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^
                                      static_cast<uint32_t>(z) * 83492791u)) * (1.0f / 4294967296.0f);
}
inline float valueNoise3(const Vec3& p) {
    const int ix = static_cast<int>(std::floor(p.x)), iy = static_cast<int>(std::floor(p.y)), iz = static_cast<int>(std::floor(p.z));
    auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float fx = sm(p.x - std::floor(p.x)), fy = sm(p.y - std::floor(p.y)), fz = sm(p.z - std::floor(p.z));
    auto lx = [&](int y, int z) { return lerpf(latticeHash(ix, y, z), latticeHash(ix + 1, y, z), fx); };
    return lerpf(lerpf(lx(iy, iz), lx(iy + 1, iz), fy), lerpf(lx(iy, iz + 1), lx(iy + 1, iz + 1), fy), fz);
}
inline bool leafCovered(const Vec3& pl) { return valueNoise3(pl * 11.0f) > 0.40f; }

struct RtPrim {
    Shape shape = Shape::Box;
    Mat4 world;            // local -> world
    Mat4 inv;              // world -> local
    uint32_t flags = 0;
    int node = -1;         // index of the scene leaf it came from (SceneBridge), -1 for synthetic prims
    std::string name;      // leaf name (ray debugger labels)
};

// Light sources. Colours are in the raster renderer's units: a surface with albedo a facing the light receives
// a * colour * N.L (the raster shaders treat light colours as irradiance / pi). The tracers convert to physical
// irradiance E = pi * colour so that Lambert = a / pi.
enum class LightType : int { Directional = 0, Point = 1, Spot = 2, Practical = 3 };
struct RtLight {
    LightType type = LightType::Point;
    Vec3 position;               // point / spot / practical
    Vec3 direction{0, -1, 0};    // directional: direction the light travels; spot / practical: cone axis
    Vec3 color;                  // raster units (see above)
    float linear = 0.0f, quadratic = 0.0f;   // point / spot: 1 / (1 + kl d + kq d^2)
    float cosInner = 1.0f, cosOuter = -2.0f; // spot cone; practical: cosOuter = cone cosine (-2 = omni)
    float range = 0.0f;          // practical: smooth cut-off radius (window (1 - (d/r)^4)^2 / (1 + d^2))
    float radius = 0.0f;         // soft shadows: angular radius (rad) for directional, metres otherwise
};

// Sky / environment radiance for secondary rays: L(d) = max(B + (Z - B)(0.5 + 0.75 d.y), 0), which integrates to the
// raster's hemispherical ambient mix(B, Z, 0.5 + 0.5 N.y) over an unoccluded hemisphere. Optional sun / moon disc.
struct RtEnv {
    Vec3 zenith{0.030f, 0.040f, 0.075f}, bounce{0.010f, 0.010f, 0.011f};
    Vec3 fogColor{0.014f, 0.018f, 0.030f};
    float fogDensity = 0.0f;     // primary-hit fog: 1 - exp(-(density d)^2) towards fogColor
    Vec3 discDir{0, 1, 0};       // direction TO the disc
    Vec3 discRadiance{0, 0, 0};  // only for mirror reflections / primary misses (NEE handles the light itself)
    float discCos = 0.9999f;
    Vec3 radiance(const Vec3& d) const {
        const float k = 0.5f + 0.75f * d.y;
        const Vec3 l = bounce + (zenith - bounce) * k;
        return {l.x > 0 ? l.x : 0.0f, l.y > 0 ? l.y : 0.0f, l.z > 0 ? l.z : 0.0f};
    }
    static RtEnv uniform(float L) {
        RtEnv e;
        e.zenith = e.bounce = e.fogColor = {L, L, L};
        return e;
    }
};

struct RtCamera {
    Vec3 position;
    Vec3 forward{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0};
    float tanHalfY = 0.5f, aspect = 16.0f / 9.0f;
    // Primary ray through normalised image coordinates u, v in [0, 1] (v = 0 at the top row).
    Ray primary(float u, float v) const;
    // Projects a world point; false if behind the camera. u, v as above; cosTheta = angle to the optical axis.
    bool project(const Vec3& p, float& u, float& v) const;
    static RtCamera fromView(const Mat4& view, const Mat4& proj);   // OpenGL view / projection matrices
};

// --- random numbers (PCG32, identical sequence on every platform; the GLSL tracer uses the same hash) ---
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL, inc = 0xda3e39cb94b95bdbULL;
    explicit Rng(uint64_t seed = 1, uint64_t stream = 1) {
        state = 0;
        inc = (stream << 1u) | 1u;
        next();
        state += seed;
        next();
    }
    uint32_t next() {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }  // [0, 1)
};

inline float luminance(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
inline float maxComponent(const Vec3& c) { return c.x > c.y ? (c.x > c.z ? c.x : c.z) : (c.y > c.z ? c.y : c.z); }

}  // namespace ts::rt
