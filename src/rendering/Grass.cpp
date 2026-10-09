#include "rendering/Grass.h"

#include <algorithm>
#include <cstdint>

#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "primitives/Cube.h"
#include "primitives/GrassBlade.h"
#include "rendering/ShaderSources.h"
#include "assets/AssetPaths.h"
#include "assets/Image.h"

namespace ts {

namespace {

constexpr int kGrid = 10;  // culling cells per axis
constexpr float kCompoundRadius = 20.0f;                        // inner compound: full density, terrain heights
constexpr float kGrassRadius = layout::kBaseRadius;      // meadow continues onto the far ground, hiding the mesh edge
constexpr float kApronDensity = 0.45f;                          // blades per m^2 on the apron relative to the compound

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

float smoothstep01(float a, float b, float x) {
    const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// v9 M1: as grassKeep() in the vertex shader (share of a cell's blades drawn at distance d).
float grassKeep(float d) { return lerpf(1.0f, 0.4f, smoothstep01(26.0f, 75.0f, d)) * lerpf(1.0f, 0.6f, smoothstep01(75.0f, 120.0f, d)); }

// How much grass grows at (x, z): patchy meadow, none on the bunker pad, the vehicle track or the creek bed.
float grassDensity(float x, float z) {
    const float r = std::sqrt(x * x + z * z);
    if (r > kGrassRadius || r < 4.6f) return 0.0f;
    if (r > kCompoundRadius) return 0.5f * (1.0f - (r - kCompoundRadius) / (kGrassRadius - kCompoundRadius));
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
out float vHue;   // per-tuft colour variation
out vec3 vAmbD;   // ambient irradiance
out vec3 vMoonD;  // moon irradiance (shadowed per fragment), includes the back-light translucency
out vec3 vSheen;  // moon sheen on the bent blade (shadowed per fragment)
out vec3 vLocalD; // beacon irradiance
out vec3 vSpotD;  // searchlight irradiance (shadowed per fragment)
uniform float uTime;
uniform float uCellCount;   // v9 M1: blades in the cell this draw call submits from (distance LOD rank)
// v9 M6: textured blade cards (CC0 grass_medium_02 atlas): each blade shows one blade of the atlas (rectangles found
// from its alpha), drawn with alpha-to-coverage; the width is clamped to ~1 pixel (the alpha scales down instead), so
// distant blades never fall between the samples and flicker.
uniform int uTextured;
uniform vec4 uBladeRects[16];
uniform int uBladeCount;
uniform float uPixelScale;   // pixels per metre at 1 m (screen height / (2 tan(fov / 2)))
out vec2 vUV;
out float vCover;

// Share of a cell's blades drawn at distance d: all within 26 m, 40 % at 75 m (the v3-v8 thinning curve), 24 % past
// 120 m; the kept blades widen to cover.
float grassKeep(float d) { return mix(1.0, 0.4, smoothstep(26.0, 75.0, d)) * mix(1.0, 0.6, smoothstep(75.0, 120.0, d)); }

vec2 push(vec3 root, vec3 pos, vec2 params) {
    vec2 d = root.xz - pos.xz;
    float dist = length(d);
    if (params.x <= 0.0 || dist > params.x) return vec2(0.0);
    return d / max(dist, 1e-3) * (1.0 - dist / params.x) * params.y;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec3 root = iRoot.xyz;
    float t = aPos.y + 0.5;                          // 0 at the root, 1 at the tip
    // v9 M1 distance LOD: the blades of a cell are stored in random order, so the first 'keep' share of them is an
    // even thinning. The draw call submits only the share the cell's nearest point needs (GrassField::drawCells);
    // here each blade keeps itself if its rank in the cell is below the share its own distance needs, so density
    // falls off smoothly across cell borders. The kept blades widen by the inverse share: the meadow keeps its
    // coverage (area per m^2) with a fraction of the blades, and TAA resolves the rest.
    float dist = length(root - uCamPos);
    float keep = grassKeep(dist);
    float rank = (float(gl_InstanceID) + 0.5) / uCellCount;
    if (rank > keep) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }
    float lod = smoothstep(26.0, 75.0, dist);
    float widen = min(1.0 / keep, 3.0);
    // Width tapers to a point; the blade is a flat strip that faces its lean direction. v9 M6: a textured card is wider
    // (the atlas blade fills about 45 % of its rectangle) and tapers less (the texture draws the tip).
    float cardW = uTextured == 1 ? 2.2 : 1.0;
    float taper = uTextured == 1 ? 1.0 - 0.4 * t : 1.0 - 0.96 * t;
    vCover = 1.0;
    if (uTextured == 1) {
        float px = iShape.y * widen * cardW * 1.25 * uPixelScale / max(dist, 0.1);
        if (px < 2.0) { widen *= 2.0 / max(px, 1e-3); vCover = max(px / 2.0, 0.15); }
    }
    vec3 p = vec3(aPos.x * iShape.y * widen * cardW * taper, t * iShape.x, aPos.z * iShape.y * 0.3);
    if (uTextured == 1 && uBladeCount > 0) {
        vec4 r = uBladeRects[int(iShape.w * 997.0) % uBladeCount];
        vUV = vec2(r.x + (aPos.x / 1.25 + 0.5) * r.z, r.y + (1.0 - t) * r.w);
    } else {
        vUV = vec2(0.5, 0.5);
    }
    float c = cos(iRoot.w + 1.5708), s = sin(iRoot.w + 1.5708);
    p = vec3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z);

    // Travelling gusts: two large-scale bands sweep across the field along vec2(0.8, 0.45); taller blades
    // catch more wind. 'front' is the slow big wave, 'ripple' the faster small one riding on it.
    vec2 wd = vec2(0.8, 0.45);
    float along = dot(root.xz, wd) / length(wd);
    float across = dot(root.xz, vec2(-wd.y, wd.x)) / length(wd);
    float front = 0.5 + 0.5 * sin(along * 0.11 - uTime * 0.9 + 1.4 * sin(across * 0.05));
    float ripple = 0.5 + 0.5 * sin(along * 0.45 - uTime * 2.1 + across * 0.17);
    float gust = 0.12 + 0.88 * front * front * (0.6 + 0.4 * ripple);
    float flutter = sin(uTime * 2.3 + iShape.w * 6.2831 + root.x * 0.7) * 0.22 * (0.35 + gust);
    float tall = 0.6 + 0.8 * clamp(iShape.x / 0.6, 0.0, 1.0);
    vec2 wind = wd * (0.10 + 0.62 * gust) * tall + vec2(-wd.y, wd.x) * flutter;
    vec2 bend = vec2(cos(iRoot.w), sin(iRoot.w)) * iShape.z + wind
              + push(root, uPusherA, uPusherAParams) + push(root, uPusherB, uPusherBParams);
    float amount = min(length(bend), 1.4);
    bend = amount > 0.0 ? normalize(bend) * amount : bend;
    // v9: a gentle curve (mostly the straight lean of the v3-v8 blade, whose cube had vertices at root and tip only)
    float tb = mix(t, t * t, 0.3);
    p.xz += bend * tb * iShape.x;
    p.y -= amount * amount * tb * iShape.x * 0.3;  // keeps the blade length roughly constant
    vWorld = root + p;
    vT = t;
    vSeed = iShape.w;
    vHue = hash12(floor(root.xz * 2.5));               // neighbouring tufts differ in tint

    // Lighting per vertex (Gouraud): the blade normal is tilted by the bend (curved blades catch the moon).
    vec3 Nf = vec3(c * aNormal.x + s * aNormal.z, aNormal.y, -s * aNormal.x + c * aNormal.z);
    // v9 M1: one-sided strip, lit on the side the camera sees (decided by the blade's face normal, so both edges of a
    // blade agree)
    if (dot(vec3(s, 0.0, c), uCamPos - root) < 0.0) Nf = -Nf;
    vec3 N = normalize(mix(Nf, vec3(0.0, 1.0, 0.0), 0.55) + vec3(bend.x, 0.0, bend.y) * (0.5 + 0.8 * t));
    vec3 L = normalize(-uMoonDir);
    vec3 V = normalize(uCamPos - vWorld);
    vAmbD = uHdr == 1 ? hemiAmbient(N) : uAmbient;
    float back = pow(max(dot(-V, L), 0.0), 3.0);        // looking towards the moon: blades glow
    // v8: by day the translucency is a thin rim of the tips against the sun (full strength it lit the field like straw)
    vMoonD = uMoonColor * (mix(0.25, 0.12, uDayWeight) + mix(0.75, 0.88, uDayWeight) * max(dot(N, L), 0.0) +
                           mix(2.0, 0.55, uDayWeight) * back * t * vec3(0.85, 1.0, 0.55) / 0.85);
    vSheen = uMoonColor * pow(max(dot(N, normalize(L + V)), 0.0), 14.0) * 0.10 * (0.3 + 0.7 * t) * (0.6 + 0.4 * (1.0 - lod));
    vec3 d = uBeaconPos - vWorld;
    float dd = length(d);
    vLocalD = uBeaconColor * max(dot(N, d / dd), 0.2) / (1.0 + uBeaconAtt.x * dd + uBeaconAtt.y * dd * dd);
    for (int i = 0; i < TS_MAX_PRACTICALS; ++i) {
        if (i >= uPracticalCount) break;
        vec3 Lp;
        float att = practicalAttenuation(i, vWorld, Lp);
        vLocalD += uPracticalColor[i].rgb * att * max(dot(N, Lp), 0.25);
    }
    d = uSpotPos - vWorld;
    dd = length(d);
    vec3 Ls = d / dd;
    float cone = clamp((dot(-Ls, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    vSpotD = uSpotColor * cone * cone * max(dot(N, Ls), 0.25) / (1.0 + uSpotAtt.x * dd + uSpotAtt.y * dd * dd);
    gl_Position = uProj * uView * vec4(vWorld, 1.0);
}
)";

const char* kGrassFrag = R"(
in vec3 vWorld;
in float vT;
in float vSeed;
in float vHue;
in vec3 vAmbD;
in vec3 vMoonD;
in vec3 vSheen;
in vec3 vLocalD;
in vec3 vSpotD;
in vec2 vUV;
in float vCover;
layout(location = 0) out vec4 FragColor;
uniform int uTextured;
uniform int uA2C;
uniform sampler2D uGrassTex;
uniform float uTexLuma;     // mean linear luminance of the atlas blades (the texture modulates the calibrated tint)
uniform float uNightLift;   // v9 M6: night-parity gain of the moon / sky terms (see Renderer::drawGrass)

float oneTap(sampler2DArrayShadow m, mat4 vp, float layer, vec3 P) {
    vec4 lp = vp * vec4(P, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    return texture(m, vec4(p.xy, layer, p.z - 0.0015));
}

void main() {
    float alpha = 1.0;
    vec3 texMod = vec3(1.0);
    if (uTextured == 1) {
        vec4 tx = texture(uGrassTex, vUV, 0.75);   // mip bias: stable under the TAA jitter
        alpha = uA2C == 1 ? clamp((tx.a - 0.12) * 2.2, 0.0, 1.0) * vCover : (tx.a >= 0.4 ? 1.0 : 0.0);
        if (alpha <= 0.0) discard;
        vec3 lin = pow(tx.rgb, vec3(2.2));
        texMod = clamp(lin / max(uTexLuma, 1e-3), vec3(0.55), vec3(1.6));
        texMod = mix(vec3(luma(texMod)), texMod, 0.35);   // keep the calibrated hue, take the texture's detail
    }
    float heat = 0.13 + 0.03 * vT + 0.02 * (vSeed - 0.5) + 0.7 * trailHeat(vWorld);  // crushed, warmed blades   // blades are slightly cooler than soil
    if (uViewMode == 0 || uViewMode >= 3) { FragColor = finalColor(vec3(0.0), heat, vWorld); FragColor.a = alpha; return; }
    // Linear albedo: dark root, then per-tuft green / olive / straw towards the tip (vSeed = dryness of the tuft)
    vec3 base = vec3(0.010, 0.014, 0.005);
    vec3 green = vec3(0.040, 0.082, 0.018), olive = vec3(0.074, 0.088, 0.026), straw = vec3(0.150, 0.122, 0.060);
    // v8: mostly green / olive tufts with dry ones scattered (the field used to read as straw in daylight)
    vec3 tip = mix(mix(green, olive, smoothstep(0.0, 0.6, vSeed)), straw, smoothstep(0.62, 1.0, vSeed));
    tip *= 0.82 + 0.36 * vHue;
    vec3 albedo = uViewMode == 2 ? uBaseColor : mix(base, tip, pow(clamp(vT, 0.0, 1.0), 0.6)) * texMod;
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
    float ao = mix(0.30, 1.0, vT);                            // self-shadowing towards the root
    vec3 lit = albedo * ((vAmbD * ao + vMoonD * sm) * uNightLift + vLocalD + vSpotD * ss) + vSheen * sm;
    FragColor = finalColor(lit, heat, vWorld);
    FragColor.a = alpha;
}
)";

}  // namespace

void GrassField::loadAtlas() {
    // v9 M6: the CC0 blade atlas (assets/vegetation/grass_medium_02, baked RGBA). Without it the blades stay untextured.
    const std::string root = asset::findAssetRoot("vegetation/grass_medium_02/textures/grass_medium_02_diff_1k.png");
    if (root.empty()) return;
    asset::Image img;
    if (!asset::loadImage(root + "/vegetation/grass_medium_02/textures/grass_medium_02_diff_1k.png", img)) return;
    const std::vector<asset::BladeRect> rects = asset::findBladeRects(img);
    if (rects.empty()) return;
    bladeCount_ = std::min(static_cast<int>(rects.size()), 16);
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i < img.rgba.size(); i += 4)
        if (img.rgba[i + 3] > 128) {
            const double r = std::pow(img.rgba[i] / 255.0, 2.2), g = std::pow(img.rgba[i + 1] / 255.0, 2.2), b = std::pow(img.rgba[i + 2] / 255.0, 2.2);
            sum += 0.299 * r + 0.587 * g + 0.114 * b;
            ++n;
        }
    texLuma_ = n ? static_cast<float>(sum / n) : 0.1f;
    for (int i = 0; i < bladeCount_; ++i) {
        bladeRects_[4 * i] = rects[i].u;
        bladeRects_[4 * i + 1] = rects[i].v;
        bladeRects_[4 * i + 2] = rects[i].w;
        bladeRects_[4 * i + 3] = rects[i].h;
    }
    // Coverage-preserving mips (the blades keep their coverage at a distance: no thinning, no shimmer).
    const std::vector<asset::Image> mips = asset::buildMips(img, 0.4f);
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t l = 0; l < mips.size(); ++l)
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), GL_RGBA8, mips[l].w, mips[l].h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mips[l].rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GrassField::~GrassField() {
    if (tex_) glDeleteTextures(1, &tex_);
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool GrassField::init() {
    shader_ = std::make_unique<Shader>();
    if (!shader_->build("grass", glsl::withLighting(kGrassVert), glsl::withLightingAndOutput(kGrassFrag))) return false;
    // v9 M1: a curved, tapered strip (3 segments: 7 vertices, 5 triangles) instead of the unit cube (24 / 12): the
    // blade bends along its length (the cube had vertices at the root and tip only, so it could only lean straight)
    // and costs less than half the triangles. One-sided: the shader turns the normal to the viewer, culling is off.
    const MeshData cube = createGrassBlade();   // primitives/GrassBlade.cpp (library primitive 15)
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
    loadAtlas();
    shader_->use();
    shader_->set("uGrassTex", 3);
    shader_->set("uMoonShadow", 5);
    shader_->set("uSpotShadow", 6);
    shader_->set("uAO", 7);
    shader_->set("uGroundMap", 8);
    shader_->set("uHeatTrail", 9);
    return true;
}

void GrassField::setCount(int blades) {
    // v7 WP-Q: compare with the requested budget, not with the blades placed (fewer): the old test rebuilt and
    // re-uploaded all 450k EPIC blades every frame (~80 ms of CPU).
    if (blades == requested_) return;
    requested_ = blades;
    count_ = blades;
    std::vector<std::vector<Blade>> byCell(kGrid * kGrid);
    auto meadowAt = [&](float x, float z) { return map_ ? map_->meadow(x, z) : grassDensity(x, z); };
    // Blade budget: the compound keeps the density it had before the apron existed (v3 spread 'blades' over
    // the compound plus a thin 7 m ring); the apron gets kApronDensity of that per m^2 (weighted by meadow).
    double wCompound = 0.0, wApron = 0.0;
    constexpr int kProbe = 160;
    const float probeCell = 2.0f * kGrassRadius / kProbe;
    for (int j = 0; j < kProbe; ++j)
        for (int i = 0; i < kProbe; ++i) {
            const float x = -kGrassRadius + (static_cast<float>(i) + 0.5f) * probeCell;
            const float z = -kGrassRadius + (static_cast<float>(j) + 0.5f) * probeCell;
            const float r = std::sqrt(x * x + z * z);
            const double w = static_cast<double>(meadowAt(x, z)) * probeCell * probeCell;
            if (r <= kCompoundRadius) wCompound += w;
            else if (r <= kGrassRadius) wApron += w;
        }
    const double wOldRing = 165.0;  // v3 weighted area of the 20..27 m ring
    const double perWeight = static_cast<double>(blades) / (wCompound + wOldRing + 1e-6);
    const int budget[2] = {static_cast<int>(perWeight * wCompound), static_cast<int>(perWeight * kApronDensity * wApron)};
    int placed = 0;
    for (int zone = 0; zone < 2; ++zone) {
        Rng rng{zone == 0 ? 0x9E3779B9u : 0x85EBCA6Bu};
        const float extent = zone == 0 ? kCompoundRadius : kGrassRadius;
        int zonePlaced = 0, attempts = 0;
        // Tufts: a clump centre is accepted with the meadow density, then 7..12 blades fan out from it.
        while (zonePlaced < budget[zone] && attempts < budget[zone] * 40 + 100) {
            ++attempts;
            const float cxw = (rng.next() * 2.0f - 1.0f) * extent, czw = (rng.next() * 2.0f - 1.0f) * extent;
            const float r2 = cxw * cxw + czw * czw;
            if (zone == 0 ? r2 > kCompoundRadius * kCompoundRadius : (r2 <= kCompoundRadius * kCompoundRadius || r2 > kGrassRadius * kGrassRadius)) continue;
            const float density = meadowAt(cxw, czw);
            if (rng.next() > density) continue;
            const int bladesInTuft = 7 + static_cast<int>(rng.next() * 6.0f);
            // Short turf everywhere, tall meadow (0.5-0.9 m) where the meadow density is high AND a large-scale
            // noise says "uncut": patches of tall grass with soft edges instead of one uniform lawn.
            const float tallness = smoothstep01(0.50f, 0.80f, density * 0.55f + 0.65f * noise2(cxw * 0.16f + 5.0f, czw * 0.16f - 9.0f));
            const float shortH = (0.12f + 0.20f * rng.next()) * (0.6f + 0.6f * density);
            const float tuftHeight = lerpf(shortH, 0.50f + 0.40f * rng.next(), tallness);
            const float dry = rng.next() < 0.35f ? 0.6f + 0.4f * rng.next() : 0.3f * rng.next();  // straw vs green tufts
            for (int k = 0; k < bladesInTuft && zonePlaced < budget[zone]; ++k) {
                const float a = rng.next() * 2.0f * PI, rr = 0.07f * std::sqrt(rng.next());
                const float x = cxw + std::cos(a) * rr, z = czw + std::sin(a) * rr;
                const bool inCompound = x * x + z * z <= kCompoundRadius * kCompoundRadius;
                const float y = inCompound ? terrainHeight(x, z) : layout::kApronLevel;
                // yaw = direction the blade fans out to (outward from the tuft centre); lean grows towards the rim
                Blade b{x, y - 0.01f, z, a, tuftHeight * (0.55f + 0.6f * rng.next()),
                        (0.007f + 0.008f * rng.next()) * (0.7f + 0.9f * tuftHeight),   // taller blades are a little broader
                        (0.15f + 0.9f * rr / 0.07f * (0.4f + 0.6f * rng.next())) * (0.8f + 0.5f * tallness), clampf(dry + 0.15f * (rng.next() - 0.5f), 0.0f, 1.0f)};
                const int cx = std::clamp(static_cast<int>((x + kGrassRadius) / (2 * kGrassRadius) * kGrid), 0, kGrid - 1);
                const int cz = std::clamp(static_cast<int>((z + kGrassRadius) / (2 * kGrassRadius) * kGrid), 0, kGrid - 1);
                byCell[cz * kGrid + cx].push_back(b);
                ++placed;
                ++zonePlaced;
            }
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
        // v9 M1: random order inside the cell (deterministic), so any prefix of it is an even thinning.
        Rng shuffle{0x2545F491u + static_cast<uint32_t>(i) * 7919u};
        for (size_t k = byCell[i].size(); k > 1; --k)
            std::swap(byCell[i][k - 1], byCell[i][static_cast<size_t>(shuffle.next() * static_cast<float>(k)) % k]);
        all.insert(all.end(), byCell[i].begin(), byCell[i].end());
    }
    count_ = placed;
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, all.size() * sizeof(Blade), all.data(), GL_STATIC_DRAW);
}

int GrassField::drawCells(const Mat4& viewProj, const Vec3& camPos) {
    const Frustum frustum(viewProj);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);   // one-sided strips
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(2, 1);
    glVertexAttribDivisor(3, 1);
    int draws = 0;
    submitted_ = 0;
    for (const Cell& c : cells_) {
        if (!frustum.intersects(c.centre, c.radius)) continue;
        // v9 M1: only the share of the cell's (shuffled) blades its nearest point needs (grassKeep in the shader).
        const float nearest = std::max(0.0f, length(Vec3{c.centre.x - camPos.x, 0.0f, c.centre.z - camPos.z}) - c.radius);
        const int n = std::min(c.count, static_cast<int>(std::ceil(c.count * grassKeep(nearest))) + 1);
        shader_->set("uCellCount", static_cast<float>(c.count));
        // Point the per-instance attributes at this cell's range (no base-instance draw in GL 3.3).
        const size_t offset = static_cast<size_t>(c.first) * sizeof(Blade);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Blade), reinterpret_cast<void*>(offset));
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Blade), reinterpret_cast<void*>(offset + 4 * sizeof(float)));
        glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, n);
        submitted_ += n;
        ++draws;
    }
    glBindVertexArray(0);
    if (cull) glEnable(GL_CULL_FACE);
    return draws;
}

}  // namespace ts
