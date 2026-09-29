#include "rendering/Grass.h"

#include <algorithm>
#include <cstdint>

#include "objects/Terrain.h"
#include "primitives/Cube.h"
#include "rendering/ShaderSources.h"

namespace ts {

namespace {

constexpr int kGrid = 10;  // culling cells per axis
constexpr float kGrassRadius = kTerrainRadius + 7.0f;  // meadow continues onto the far ground, hiding the disc edge

struct Blade {
    float x, y, z, yaw;           // root position on the terrain, rotation about Y
    float height, width, lean, seed;
};

// Small deterministic generator (same field every run, so captures are reproducible).
struct Rng {
    uint32_t s;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / static_cast<float>(0x1000000);
    }
};

float hash2(int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

float noise2(float x, float z) {  // value noise, same idea as the shaders' valueNoise
    const int ix = static_cast<int>(std::floor(x)), iz = static_cast<int>(std::floor(z));
    float fx = x - ix, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    const float a = hash2(ix, iz), b = hash2(ix + 1, iz), c = hash2(ix, iz + 1), d = hash2(ix + 1, iz + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}

// How much grass grows at (x, z): patchy meadow, none on the bunker pad, the vehicle track or the creek bed.
float grassDensity(float x, float z) {
    const float r = std::sqrt(x * x + z * z);
    if (r > kGrassRadius || r < 4.6f) return 0.0f;
    if (r > kTerrainRadius) return 0.8f * (1.0f - (r - kTerrainRadius) / (kGrassRadius - kTerrainRadius));
    if (z > 0.0f && std::fabs(x - trackCentreX(z)) < kTrackHalfWidth * 0.85f) return 0.0f;
    if (std::fabs(z - creekCentreZ(x)) < kCreekHalfWidth * 0.7f) return 0.1f;
    float d = 0.25f + 0.9f * noise2(x * 0.35f + 11.0f, z * 0.35f - 4.0f) + 0.3f * noise2(x * 1.3f, z * 1.3f);
    if (terrainNormal(x, z).y < 0.93f) d *= 0.5f;  // sparse on steep slopes
    return clampf(d, 0.0f, 1.0f);
}

const char* kGrassVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 iRoot;    // x, y, z, yaw
layout(location = 3) in vec4 iShape;   // height, width, lean, seed
uniform mat4 uView, uProj;
uniform vec3 uPusherA; uniform vec2 uPusherAParams;
uniform vec3 uPusherB; uniform vec2 uPusherBParams;
out vec3 vWorld;
out float vT;
out float vSeed;
out vec3 vAmbD;   // ambient irradiance
out vec3 vMoonD;  // moon irradiance (shadowed per fragment)
out vec3 vLocalD; // beacon irradiance
out vec3 vSpotD;  // searchlight irradiance (shadowed per fragment)
uniform float uTime;

vec2 push(vec3 root, vec3 pos, vec2 params) {
    vec2 d = root.xz - pos.xz;
    float dist = length(d);
    if (params.x <= 0.0 || dist > params.x) return vec2(0.0);
    return d / max(dist, 1e-3) * (1.0 - dist / params.x) * params.y;
}

void main() {
    float t = aPos.y + 0.5;                          // 0 at the root, 1 at the tip
    vec3 p = vec3(aPos.x * iShape.y * (1.0 - 0.85 * t), t * iShape.x, aPos.z * iShape.y * 0.3);
    float c = cos(iRoot.w + 1.5708), s = sin(iRoot.w + 1.5708);   // blade face turned towards its lean
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);
    vec3 root = iRoot.xyz;
    float gust = 0.5 + 0.5 * sin(uTime * 0.9 + root.x * 0.25 + root.z * 0.15);
    float sway = sin(uTime * 2.3 + iShape.w * 6.2831 + root.x * 0.7) * (0.35 + 0.65 * gust);
    // Each blade leans outward from its tuft (iRoot.w) and the wind adds a shared, gusting sway.
    vec2 bend = vec2(cos(iRoot.w), sin(iRoot.w)) * iShape.z + vec2(0.8, 0.45) * 0.3 * (0.4 + sway)
              + push(root, uPusherA, uPusherAParams) + push(root, uPusherB, uPusherBParams);
    float amount = min(length(bend), 1.3);
    bend = amount > 0.0 ? normalize(bend) * amount : bend;
    p.xz += bend * t * t * iShape.x;
    p.y -= amount * amount * t * t * iShape.x * 0.3;  // keeps the blade length roughly constant
    vWorld = root + p;
    vT = t;
    vSeed = iShape.w;

    // Lighting per vertex (Gouraud) with a soft, translucent blade normal.
    vec3 N = normalize(mix(vec3(c * aNormal.x + s * aNormal.z, aNormal.y, -s * aNormal.x + c * aNormal.z),
                           vec3(0.0, 1.0, 0.0), 0.55));
    vec3 L = normalize(-uMoonDir);
    vAmbD = uHdr == 1 ? mix(uGroundBounce, uSkyZenith, 0.5 + 0.5 * N.y) : uAmbient;
    vMoonD = uMoonColor * (0.25 + 0.75 * max(dot(N, L), 0.0));      // wrap lighting: light passes through
    vec3 d = uBeaconPos - vWorld;
    float dist = length(d);
    vLocalD = uBeaconColor * max(dot(N, d / dist), 0.2) / (1.0 + uBeaconAtt.x * dist + uBeaconAtt.y * dist * dist);
    for (int i = 0; i < 8; ++i) {
        if (i >= uPracticalCount) break;
        vec3 Lp;
        float att = practicalAttenuation(i, vWorld, Lp);
        vLocalD += uPracticalColor[i].rgb * att * max(dot(N, Lp), 0.25);
    }
    d = uSpotPos - vWorld;
    dist = length(d);
    vec3 Ls = d / dist;
    float cone = clamp((dot(-Ls, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    vSpotD = uSpotColor * cone * cone * max(dot(N, Ls), 0.25) / (1.0 + uSpotAtt.x * dist + uSpotAtt.y * dist * dist);
    gl_Position = uProj * uView * vec4(vWorld, 1.0);
}
)";

const char* kGrassFrag = R"(
in vec3 vWorld;
in float vT;
in float vSeed;
in vec3 vAmbD;
in vec3 vMoonD;
in vec3 vLocalD;
in vec3 vSpotD;
out vec4 FragColor;

float oneTap(sampler2DArrayShadow m, mat4 vp, float layer, vec3 P) {
    vec4 lp = vp * vec4(P, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    return texture(m, vec4(p.xy, layer, p.z - 0.0015));
}

void main() {
    float heat = 0.13 + 0.03 * vT + 0.02 * (vSeed - 0.5) + 0.7 * trailHeat(vWorld);  // crushed, warmed blades   // blades are slightly cooler than soil
    if (uViewMode == 0 || uViewMode >= 3) { FragColor = finalColor(vec3(0.0), heat, vWorld); return; }
    // Linear albedo: dark base, green or straw-coloured tips (vSeed = dryness of the tuft)
    vec3 base = vec3(0.018, 0.022, 0.008);
    vec3 tip = mix(vec3(0.050, 0.085, 0.022), vec3(0.170, 0.140, 0.070), vSeed);
    vec3 albedo = uViewMode == 2 ? uBaseColor : mix(base, tip, pow(clamp(vT, 0.0, 1.0), 0.6));
    float sm = 1.0, ss = 1.0;
    if (uShadows > 0) {
        bool nearC = length(vWorld - uCamPos) < uCascadeSplit;
        sm = nearC ? oneTap(uMoonShadow, uMoonVP0, 0.0, vWorld) : oneTap(uMoonShadow, uMoonVP1, 1.0, vWorld);
    }
    if (uShadows > 1 && max(vSpotD.r, vSpotD.g) > 1e-4) {
        vec4 lp = uSpotVP * vec4(vWorld, 1.0);
        vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
        if (lp.w > 0.0 && all(greaterThan(p, vec3(0.0))) && all(lessThan(p, vec3(1.0))))
            ss = texture(uSpotShadow, vec3(p.xy, p.z - 0.0003));
    }
    float ao = mix(0.35, 1.0, vT);                            // self-shadowing towards the root
    vec3 lit = albedo * (vAmbD * ao + vMoonD * sm + vLocalD + vSpotD * ss);
    FragColor = finalColor(lit, heat, vWorld);
}
)";

}  // namespace

GrassField::~GrassField() {
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool GrassField::init() {
    shader_ = std::make_unique<Shader>();
    if (!shader_->build("grass", glsl::withLighting(kGrassVert), glsl::withLightingAndOutput(kGrassFrag))) return false;
    const MeshData cube = createCube();  // the same unit cube generator as every other part
    indexCount_ = static_cast<GLsizei>(cube.indices.size());
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

void GrassField::setCount(int blades) {
    if (blades == count_) return;
    count_ = blades;
    std::vector<std::vector<Blade>> byCell(kGrid * kGrid);
    Rng rng{0x9E3779B9u};
    int placed = 0, attempts = 0;
    // Tufts: a clump centre is accepted with the meadow density, then 7..12 blades fan out from it.
    while (placed < blades && attempts < blades * 40) {
        ++attempts;
        const float cxw = (rng.next() * 2.0f - 1.0f) * kGrassRadius, czw = (rng.next() * 2.0f - 1.0f) * kGrassRadius;
        const float density = map_ ? map_->meadow(cxw, czw) : grassDensity(cxw, czw);
        if (rng.next() > density) continue;
        const int bladesInTuft = 7 + static_cast<int>(rng.next() * 6.0f);
        const float tuftHeight = (0.14f + 0.30f * rng.next()) * (0.6f + 0.6f * density);
        const float dry = rng.next() < 0.35f ? 0.6f + 0.4f * rng.next() : 0.3f * rng.next();  // straw vs green tufts
        for (int k = 0; k < bladesInTuft && placed < blades; ++k) {
            const float a = rng.next() * 2.0f * PI, rr = 0.07f * std::sqrt(rng.next());
            const float x = cxw + std::cos(a) * rr, z = czw + std::sin(a) * rr;
            const bool onDisc = x * x + z * z <= kTerrainRadius * kTerrainRadius;
            const float y = onDisc ? terrainHeight(x, z) : terrainRimHeight() - 0.1f;  // far ground: just below the rim
            // yaw = direction the blade fans out to (outward from the tuft centre); lean grows towards the rim
            Blade b{x, y - 0.01f, z, a, tuftHeight * (0.55f + 0.6f * rng.next()), 0.007f + 0.008f * rng.next(),
                    0.15f + 0.9f * rr / 0.07f * (0.4f + 0.6f * rng.next()), clampf(dry + 0.15f * (rng.next() - 0.5f), 0.0f, 1.0f)};
            const int cx = std::clamp(static_cast<int>((x + kGrassRadius) / (2 * kGrassRadius) * kGrid), 0, kGrid - 1);
            const int cz = std::clamp(static_cast<int>((z + kGrassRadius) / (2 * kGrassRadius) * kGrid), 0, kGrid - 1);
            byCell[cz * kGrid + cx].push_back(b);
            ++placed;
        }
    }
    std::vector<Blade> all;
    all.reserve(placed);
    cells_.clear();
    const float cellSize = 2 * kGrassRadius / kGrid;
    for (int i = 0; i < kGrid * kGrid; ++i) {
        if (byCell[i].empty()) continue;
        Cell c;
        c.first = static_cast<int>(all.size());
        c.count = static_cast<int>(byCell[i].size());
        c.centre = {-kGrassRadius + (static_cast<float>(i % kGrid) + 0.5f) * cellSize, 0.3f,
                    -kGrassRadius + (static_cast<float>(i / kGrid) + 0.5f) * cellSize};
        c.radius = cellSize * 0.75f + 1.0f;
        cells_.push_back(c);
        all.insert(all.end(), byCell[i].begin(), byCell[i].end());
    }
    count_ = placed;
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, all.size() * sizeof(Blade), all.data(), GL_STATIC_DRAW);
}

int GrassField::drawCells(const Mat4& viewProj) {
    const Frustum frustum(viewProj);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    int draws = 0;
    for (const Cell& c : cells_) {
        if (!frustum.intersects(c.centre, c.radius)) continue;
        // Point the per-instance attributes at this cell's range (no base-instance draw in GL 3.3).
        const size_t offset = static_cast<size_t>(c.first) * sizeof(Blade);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Blade), reinterpret_cast<void*>(offset));
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Blade), reinterpret_cast<void*>(offset + 4 * sizeof(float)));
        glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, c.count);
        ++draws;
    }
    glBindVertexArray(0);
    return draws;
}

}  // namespace ts
