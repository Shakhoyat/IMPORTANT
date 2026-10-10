#include "rendering/Foliage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "primitives/Cube.h"
#include "rendering/ShaderSources.h"

namespace ts {

namespace {

constexpr int kGrid = 12;          // culling cells per axis over the card bounding box
constexpr uint32_t kSeed = 0xA341316Cu;

// Instance = 3 x vec4:
//   A: card base position (world), kind + seed (kind = floor, seed = fract; kind 0 pine spray, 1 leaf cluster)
//   B: long axis of the card * length, width
//   C: card normal (bent towards "outward"), colour variation
struct Card {
    float px, py, pz, kindSeed;
    float ax, ay, az, width;
    float nx, ny, nz, tint;
};

struct Rng {
    uint32_t s;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / static_cast<float>(0x1000000);
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

Vec3 normalMul(const Mat3& m, const Vec3& v) {  // column-major 3x3 times vector
    return {m.m[0] * v.x + m.m[3] * v.y + m.m[6] * v.z, m.m[1] * v.x + m.m[4] * v.y + m.m[7] * v.z,
            m.m[2] * v.x + m.m[5] * v.y + m.m[8] * v.z};
}

Vec3 randomUnit(Rng& rng) {
    const float z = rng.range(-1.0f, 1.0f), a = rng.range(0.0f, 2.0f * PI), r = std::sqrt(1.0f - z * z);
    return {r * std::cos(a), z, r * std::sin(a)};
}

Vec3 rotateAround(const Vec3& v, const Vec3& axis, float angle) {  // Rodrigues
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

// Stochastic rounding keeps the total count proportional to the density, however small each part's share is.
int roundCount(float n, Rng& rng) {
    const int base = static_cast<int>(n);
    return base + (rng.next() < n - static_cast<float>(base) ? 1 : 0);
}

// ---------------------------------------------------------------- shaders
// Shared by the colour and the depth shader: card placement, wind, and the procedural cut-out.
const char* kCardVertex = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 iA;   // base xyz, kind + seed
layout(location = 3) in vec4 iB;   // axis * length, width
layout(location = 4) in vec4 iC;   // normal, tint
uniform float uTime;

// The unit cube becomes a plate: only its +Y face is kept (u = x, v = z), the other faces collapse off screen.
bool cardVertex(out vec3 P, out vec3 N, out vec2 uv, out float kind, out float seed) {
    kind = floor(iA.w);
    seed = fract(iA.w);
    float len = tsLength(iB.xyz);
    vec3 ax = iB.xyz / len;
    vec3 side = tsNormalize(tsCross(ax, iC.xyz));
    N = tsCross(side, ax);
    uv = vec2(aPos.x + 0.5, aPos.z + 0.5);
    float along = uv.y * len;
    P = iA.xyz + ax * along + side * (aPos.x * iB.w);
    if (kind < 0.5) P.y -= uv.y * uv.y * len * 0.22;      // sprays droop
    // Wind: the whole crown leans with a travelling gust (more with height), tips flutter on top of it.
    vec2 wd = vec2(0.8, 0.45);
    float phase = tsDot(iA.xz, wd);
    float gust = 0.5 + 0.5 * sin(uTime * 0.7 + phase * 0.22);
    float travel = sin(uTime * 1.25 + phase * 0.55 + seed * 1.5);
    float hgt = tsClamp(iA.y * 0.10, 0.0, 1.0);
    P.xz += wd * (0.02 + 0.05 * gust) * (0.25 + 0.75 * hgt) * (0.4 + travel);
    float f1 = sin(uTime * (2.6 + seed * 2.2) + seed * 40.0 + iA.x * 0.9);
    float f2 = sin(uTime * (3.3 + seed * 1.7) + seed * 23.0 + iA.z * 0.8);
    float tip = (kind < 0.5 ? uv.y * uv.y : 0.5 + 0.5 * uv.y) * len;
    P += side * f1 * tip * 0.10 * (0.4 + gust) + N * f2 * tip * 0.09 * (0.4 + gust);
    return aNormal.y > 0.5;
}
)";

const char* kCardCoverage = R"(
float fhash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += tsDot(p, p + 45.32);
    return fract(p.x * p.y);
}
// 1 where the card is opaque. Pines: a central stem with needles sweeping towards the tip (random reach);
// broadleaf: two grids of small pointed leaves with random rotation, jitter and gaps. 'shade' = 0 stem/base .. 1 tips.
float cardCoverage(vec2 uv, float kind, float seed, float dist, out float shade) {
    float widen = tsSmoothstep(12.0, 60.0, dist);   // far away thicken slightly so the pattern does not shimmer
    shade = 0.5;
    if (kind < 0.5) {
        float x = (uv.x - 0.5) * 2.0, ax = abs(x), v = uv.y;
        float env = pow(max(sin(3.14159 * pow(tsClamp(v, 0.0, 1.0), 0.7)), 0.0), 0.7) * (0.75 + 0.25 * fhash(vec2(seed, 3.0)));
        float stem = tsStep(ax, 0.045 * (1.0 - 0.5 * v) + 0.02 * widen);
        float n = 12.0 + floor(seed * 6.0);
        float s = v * n - ax * 5.0;
        float cell = floor(s), f = fract(s);
        float reach = env * (0.7 + 0.3 * fhash(vec2(cell, sign(x) + seed * 17.0)));
        float th = (0.5 + 0.3 * widen) * (1.0 - 0.5 * ax / max(reach, 1e-3)) + 0.06 * widen;
        float needle = tsStep(ax, reach) * tsStep(f, th);
        shade = tsClamp(ax / max(reach, 1e-3), 0.0, 1.0) * needle;
        return max(stem, needle);
    }
    float cov = 0.0;
    for (int L = 0; L < 2; ++L) {
        float sc = L == 0 ? 3.0 : 4.6;
        vec2 p = uv * sc + vec2(seed * 7.0 + float(L) * 0.37, seed * 3.0);
        vec2 id = floor(p), f = fract(p) - 0.5;
        float h = fhash(id + float(L) * 11.0 + seed * 29.0);
        vec2 j = vec2(fhash(id + 5.1), fhash(id + 9.7)) - 0.5;
        f -= j * 0.35;
        float a = h * 6.2832, c = cos(a), s2 = sin(a);
        vec2 q = vec2(c * f.x + s2 * f.y, -s2 * f.x + c * f.y);
        float lx = q.x / 0.5;
        float hw = (0.20 + 0.10 * widen) * (1.0 - lx * lx);
        float inside = tsStep(abs(lx), 1.0) * tsStep(abs(q.y), hw) * tsStep(0.16, h);
        if (inside > 0.5 && cov < 0.5) shade = fract(h * 7.3) * 0.6 + 0.4 * (lx * 0.5 + 0.5);
        cov = max(cov, inside);
    }
    return cov;
}
)";

const char* kFoliageVert = R"(
out vec3 vWorld;
out vec3 vN;
out vec2 vUV;
out float vSeed;
out float vKind;
out float vTint;
void main() {
    vec3 P, N; vec2 uv; float kind, seed;
    bool keep = cardVertex(P, N, uv, kind, seed);
    vWorld = P; vN = N; vUV = uv; vSeed = seed; vKind = kind; vTint = iC.w;
    gl_Position = keep ? uProj * uView * vec4(P, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
)";

const char* kFoliageFrag = R"(
in vec3 vWorld;
in vec3 vN;
in vec2 vUV;
in float vSeed;
in float vKind;
in float vTint;
layout(location = 0) out vec4 FragColor;

float oneTap(sampler2DArrayShadow m, mat4 vp, float layer, vec3 P) {
    vec4 lp = vp * vec4(P, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    return texture(m, vec4(p.xy, layer, p.z - 0.0025));
}

void main() {
    float dist = tsLength(vWorld - uCamPos);
    float shade;
    if (cardCoverage(vUV, vKind, vSeed, dist, shade) < 0.5) discard;
    float heat = 0.13 + 0.03 * vUV.y + 0.02 * (vSeed - 0.5);
    if (uViewMode == 0 || uViewMode >= 3) { FragColor = finalColor(vec3(0.0), heat, vWorld); return; }

    vec3 albedo;
    if (vKind < 0.5) {      // spruce: bluish dark green, lighter young needles at the tips
        // v8: deep green (was blue-green: under the blue daylight ambient the spruces read teal)
        albedo = tsMix(vec3(0.011, 0.024, 0.013), vec3(0.036, 0.066, 0.030), tsClamp(0.7 * vUV.y + 0.5 * shade, 0.0, 1.0));
    } else {                // broadleaf: olive, some yellowish leaves
        albedo = tsMix(vec3(0.026, 0.036, 0.010), vec3(0.100, 0.125, 0.034), shade);
        albedo = tsMix(albedo, vec3(0.110, 0.100, 0.030), tsStep(0.86, vTint) * 0.5);
    }
    albedo *= 0.75 + 0.5 * vTint;
    if (uViewMode == 2) albedo = uBaseColor;

    vec3 V = tsNormalize(uCamPos - vWorld);
    vec3 N = tsNormalize(vN);
    float ny = N.y;
    if (tsDot(N, V) < 0.0) N = -N;                       // two-sided
    vec3 Lm = tsNormalize(-uMoonDir);
    float ndl = tsDot(N, Lm);
    float sm = 1.0, ss = 1.0;
    if (uShadows > 0) {
        vec3 Ps = vWorld + Lm * 0.05;
        bool nearC = dist < uCascadeSplit;
        sm = nearC ? oneTap(uMoonShadow, uMoonVP0, 0.0, Ps) : oneTap(uMoonShadow, uMoonVP1, 1.0, Ps);
    }
    // Moon: diffuse on the lit side, subsurface transmission (yellow-green) when the moon is behind the card.
    float front = max(ndl, 0.0), back = max(-ndl, 0.0);
    float through = pow(max(tsDot(-V, Lm), 0.0), 3.0);   // looking towards the moon: leaves glow
    vec3 transTint = vKind < 0.5 ? vec3(0.55, 0.85, 0.65) : vec3(0.85, 1.0, 0.45);
    float glow = tsMix(2.2, 0.9, uDayWeight);   // v8: sunlight through needles / leaves is a rim, not a lamp
    vec3 moon = uMoonColor * sm * (albedo * (0.25 + 0.75 * front) + albedo * transTint * (0.9 * back + 0.9 * through) * glow);
    vec3 sheen = uMoonColor * sm * pow(max(tsDot(N, tsNormalize(Lm + V)), 0.0), 40.0) * 0.012;   // waxy leaf highlight

    vec3 amb = uHdr == 1 ? hemiAmbient(tsNormalize(vN)) : uAmbient;
    float ao = tsMix(0.30, 1.0, tsClamp(0.35 + 0.5 * (0.5 + 0.5 * ny) + 0.35 * vUV.y * (vKind < 0.5 ? 1.0 : 0.0), 0.0, 1.0));
    vec3 lit = albedo * amb * ao + moon + sheen;

    vec3 d = uBeaconPos - vWorld;
    float dd = tsLength(d);
    lit += albedo * uBeaconColor * tsMix(0.35, 1.0, abs(tsDot(N, d / dd))) / (1.0 + uBeaconAtt.x * dd + uBeaconAtt.y * dd * dd);
    for (int i = 0; i < TS_MAX_PRACTICALS; ++i) {
        if (i >= uPracticalCount) break;
        vec3 Lp;
        float att = practicalAttenuation(i, vWorld, Lp);
        lit += albedo * uPracticalColor[i].rgb * att * tsMix(0.35, 1.0, abs(tsDot(N, Lp)));
    }
    d = uSpotPos - vWorld;
    dd = tsLength(d);
    vec3 Ls = d / dd;
    float cone = tsClamp((tsDot(-Ls, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    vec3 spot = uSpotColor * cone * cone * tsMix(0.35, 1.0, abs(tsDot(N, Ls))) / (1.0 + uSpotAtt.x * dd + uSpotAtt.y * dd * dd);
    if (uShadows > 1 && max(spot.r, spot.g) > 1e-4) {
        vec4 lp = uSpotVP * vec4(vWorld, 1.0);
        vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
        if (lp.w > 0.0 && all(greaterThan(p, vec3(0.0))) && all(lessThan(p, vec3(1.0))))
            ss = texture(uSpotShadow, vec3(p.xy, p.z - 0.0005));
    }
    lit += albedo * spot * ss;
    FragColor = finalColor(lit, heat, vWorld);
}
)";

const char* kDepthVert = R"(
uniform mat4 uLightVP;
out vec2 vUV;
out float vSeed;
out float vKind;
out vec3 vWorld;
void main() {
    vec3 P, N; vec2 uv; float kind, seed;
    bool keep = cardVertex(P, N, uv, kind, seed);
    vUV = uv; vSeed = seed; vKind = kind; vWorld = P;
    gl_Position = keep ? uLightVP * vec4(P, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
}
)";

const char* kDepthFrag = R"(
in vec2 vUV;
in float vSeed;
in float vKind;
in vec3 vWorld;
void main() {
    float shade;
    if (cardCoverage(vUV, vKind, vSeed, 8.0, shade) < 0.5) discard;
}
)";

}  // namespace

FoliageField::~FoliageField() {
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool FoliageField::init() {
    const std::string vert = std::string(kCardVertex) + "uniform mat4 uView, uProj;\n" + kFoliageVert;
    shader_ = std::make_unique<Shader>();
    if (!shader_->build("foliage", glsl::withLighting(vert.c_str()),
                        glsl::withLightingAndOutput((std::string(kCardCoverage) + kFoliageFrag).c_str())))
        return false;
    depthShader_ = std::make_unique<Shader>();
    if (!depthShader_->build("foliage-depth", std::string("#version 330 core\n") + kCardVertex + kDepthVert,
                             std::string("#version 330 core\n") + kCardCoverage + kDepthFrag))
        return false;

    const MeshData cube = createCube();  // the same unit cube generator as every other part
    // Only the cube's +Y face is used as the card plate (2 triangles instead of 12): find it in the index list.
    size_t plate = 0;
    for (size_t i = 0; i < cube.vertices.size(); ++i)
        if (cube.vertices[i].normal.y > 0.5f) { plate = i / 4; break; }
    plateOffset_ = plate * 6 * sizeof(uint32_t);
    indexCount_ = 6;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &instanceVbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, cube.vertices.size() * sizeof(Vertex), cube.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(Vec3)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cube.indices.size() * sizeof(uint32_t), cube.indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    shader_->use();
    shader_->set("uMoonShadow", 5);
    shader_->set("uSpotShadow", 6);
    shader_->set("uAO", 7);
    shader_->set("uGroundMap", 8);
    shader_->set("uHeatTrail", 9);
    return true;
}

void FoliageField::build(const Scene& scene, float density, const std::unordered_set<const SceneNode*>* skip) {
    const size_t skipCount = skip ? skip->size() : 0;
    if (std::fabs(density - density_) < 1e-4f && leafCount_ == scene.leaves().size() && skipCount_ == skipCount) return;
    density_ = density;
    leafCount_ = scene.leaves().size();
    skipCount_ = skipCount;
    std::vector<Card> cards;
    Rng rng{kSeed};
    const Vec3 up{0, 1, 0};
    for (const SceneNode* n : scene.leaves()) {
        if (n->material.surface != Surface::Foliage) continue;
        if (skip && skip->count(n)) continue;
        const Mat4& M = n->world;
        if (n->primitive == PrimitiveType::Cone) {
            // Pine whorl: branch sprays leave the lateral surface, droop outward and down, rolled a little about
            // their own axis. Reach and count scale with the world radius of the whorl.
            const Vec3 colX = transformDirection(M, {1, 0, 0}), colY = transformDirection(M, {0, 1, 0});
            const float rw = 0.5f * 0.5f * (length(colX) + length(colY));
            const int count = roundCount(density * clampf(130.0f * rw * rw + 34.0f, 34.0f, 230.0f), rng);
            const Vec3 apex = transformPoint(M, {0, 0, 0.5f});
            for (int i = 0; i < count; ++i) {
                const float h = std::pow(rng.range(0.01f, 1.0f), 0.38f)   /* biased to the rim: that is the silhouette */, phi = rng.range(0.0f, 2.0f * PI);
                const float cp = std::cos(phi), sp = std::sin(phi);
                const Vec3 P = transformPoint(M, {0.5f * h * cp, 0.5f * h * sp, 0.5f - h});
                const Vec3 onAxis = transformPoint(M, {0, 0, 0.5f - h});
                const Vec3 radial = normalize(P - onAxis);
                const Vec3 down = normalize(transformPoint(M, {0.5f * cp, 0.5f * sp, -0.5f}) - apex);
                Vec3 axis = normalize(down + radial * 0.7f + Vec3{0, -0.05f, 0} + randomUnit(rng) * 0.25f);   // stands off the cone surface
                Vec3 nrm = normalize(up + radial * 0.4f);
                nrm = normalize(nrm - axis * dot(nrm, axis));
                nrm = rotateAround(nrm, axis, rng.range(-0.7f, 0.7f));
                const float len = clampf(0.50f * rw, 0.30f, 0.85f) * rng.range(0.7f, 1.3f) * (h > 0.85f ? 1.4f : 1.0f);   // longer at the rim
                const Vec3 base = P - radial * (len * 0.15f);
                cards.push_back({base.x, base.y, base.z, 0.0f + rng.next() * 0.999f, axis.x * len, axis.y * len,
                                 axis.z * len, len * rng.range(0.6f, 0.85f), nrm.x, nrm.y, nrm.z, rng.next()});
            }
        } else if (n->primitive == PrimitiveType::Sphere) {
            const Vec3 colX = transformDirection(M, {1, 0, 0}), colY = transformDirection(M, {0, 1, 0}),
                       colZ = transformDirection(M, {0, 0, 1});
            const float rw = 0.5f * (length(colX) + length(colY) + length(colZ)) / 3.0f;
            const int count = roundCount(density * clampf(380.0f * rw * rw + 60.0f, 60.0f, 420.0f), rng);
            const Mat3 nm = normalMatrix(M);
            for (int i = 0; i < count; ++i) {
                const Vec3 d = randomUnit(rng);
                const Vec3 P = transformPoint(M, d * 0.5f);
                const Vec3 nOut = normalize(normalMul(nm, d));
                if (nOut.y < -0.5f && rng.next() < 0.6f) continue;   // fewer cards on the shaded underside
                // Half of the clusters lie tangent to the clump, the others stand off it at up to ~50 degrees so
                // the silhouette is no longer the smooth sphere.
                const Vec3 tangent = normalize(cross(nOut, randomUnit(rng)));
                const float tilt = rng.next() < 0.5f ? 0.0f : rng.range(0.3f, 1.0f);
                const Vec3 axis = normalize(tangent + nOut * tilt);
                const Vec3 shadeN = normalize(nOut + up * 0.25f + randomUnit(rng) * 0.3f);
                const float size = clampf(0.22f * rw + 0.05f, 0.10f, 0.30f) * rng.range(0.75f, 1.25f);
                const Vec3 base = P + nOut * 0.03f - axis * (size * 0.35f);
                cards.push_back({base.x, base.y, base.z, 1.0f + rng.next() * 0.999f, axis.x * size, axis.y * size,
                                 axis.z * size, size, shadeN.x, shadeN.y, shadeN.z, rng.next()});
            }
        }
    }

    // Sort into a grid over the cards' own bounding box; each cell carries a tight bounding sphere.
    cells_.clear();
    count_ = static_cast<int>(cards.size());
    if (cards.empty()) return;
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const Card& c : cards) {
        minX = std::min(minX, c.px); maxX = std::max(maxX, c.px);
        minZ = std::min(minZ, c.pz); maxZ = std::max(maxZ, c.pz);
    }
    const float sx = std::max(maxX - minX, 1.0f), sz = std::max(maxZ - minZ, 1.0f);
    std::vector<std::vector<Card>> byCell(kGrid * kGrid);
    for (const Card& c : cards) {
        const int cx = std::clamp(static_cast<int>((c.px - minX) / sx * kGrid), 0, kGrid - 1);
        const int cz = std::clamp(static_cast<int>((c.pz - minZ) / sz * kGrid), 0, kGrid - 1);
        byCell[cz * kGrid + cx].push_back(c);
    }
    std::vector<Card> all;
    all.reserve(cards.size());
    for (const auto& list : byCell) {
        if (list.empty()) continue;
        Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (const Card& c : list) {
            lo = {std::min(lo.x, c.px), std::min(lo.y, c.py), std::min(lo.z, c.pz)};
            hi = {std::max(hi.x, c.px), std::max(hi.y, c.py), std::max(hi.z, c.pz)};
        }
        Cell cell;
        cell.first = static_cast<int>(all.size());
        cell.count = static_cast<int>(list.size());
        cell.centre = (lo + hi) * 0.5f;
        cell.radius = length(hi - lo) * 0.5f + 1.0f;   // + card reach and wind sway
        cells_.push_back(cell);
        all.insert(all.end(), list.begin(), list.end());
    }
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, all.size() * sizeof(Card), all.data(), GL_STATIC_DRAW);
}

int FoliageField::drawCells(const Mat4& viewProj) {
    const Frustum frustum(viewProj);
    const bool culling = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    if (culling) glDisable(GL_CULL_FACE);   // cards are two-sided
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    for (int a = 2; a <= 4; ++a) {
        glEnableVertexAttribArray(a);
        glVertexAttribDivisor(a, 1);
    }
    int draws = 0;
    for (const Cell& c : cells_) {
        if (!frustum.intersects(c.centre, c.radius)) continue;
        // Point the per-instance attributes at this cell's range (no base-instance draw in GL 3.3).
        const size_t offset = static_cast<size_t>(c.first) * sizeof(Card);
        for (int a = 2; a <= 4; ++a)
            glVertexAttribPointer(a, 4, GL_FLOAT, GL_FALSE, sizeof(Card),
                                  reinterpret_cast<void*>(offset + static_cast<size_t>(a - 2) * 4 * sizeof(float)));
        glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, reinterpret_cast<void*>(plateOffset_), c.count);
        ++draws;
    }
    glBindVertexArray(0);
    if (culling) glEnable(GL_CULL_FACE);
    return draws;
}

int FoliageField::drawDepth(const Mat4& lightViewProj, float time) {
    if (count_ <= 0) return 0;
    depthShader_->use();
    depthShader_->set("uLightVP", lightViewProj);
    depthShader_->set("uTime", time);
    return drawCells(lightViewProj);
}

}  // namespace ts
