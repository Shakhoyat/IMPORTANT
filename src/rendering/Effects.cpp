#include "rendering/Effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "rendering/ShaderSources.h"

namespace ts {

namespace {

// Effect kinds (instance attribute iC.w).
enum Kind {
    kFire = 0, kSmoke = 1, kTrail = 2, kDust = 3, kFlash = 4, kSpark = 5, kEmber = 6,
    kShock = 7, kTracer = 8, kGlow = 9, kScorch = 10, kFlame = 11
};

constexpr size_t kMaxInstances = 24000;
constexpr int kFloatsPerInstance = 16;

const char* kSpriteVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 iA;   // centre xyz, radius (streaks: half width)
layout(location = 3) in vec4 iB;   // velocity / axis xyz, brightness
layout(location = 4) in vec4 iC;   // age / life, seed, heat, kind
layout(location = 5) in vec4 iD;   // age (s), life (s), half length (streaks), unused
uniform mat4 uView, uProj;
out vec2 vLocal;
out vec3 vWorld;
flat out vec3 vCentre;
flat out vec4 vP;    // radius, kind, extent, age
flat out vec4 vQ;    // t, seed, heat, brightness
flat out vec2 vR;    // life, half length
void main() {
    int kind = int(iC.w + 0.5);
    vP = vec4(iA.w, float(kind), 1.0, iD.x);
    vQ = vec4(iC.x, iC.y, iC.z, iB.w);
    vR = vec2(iD.y, iD.z);
    vCentre = iA.xyz;
    vLocal = aPos.xy * 2.0;
    vWorld = iA.xyz;
    if (aNormal.z < 0.5) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }   // only the +Z face is a sprite
    vec3 c = iA.xyz;
    float rad = iA.w;
    vec2 l = aPos.xy * 2.0;
    vec3 view = normalize(c - uCamPos);
    vec3 wp;
    if (kind == 5 || kind == 6 || kind == 8 || kind == 11) {            // velocity-aligned streak
        vec3 ax = iB.xyz;
        float al = length(ax);
        ax = al > 1e-4 ? ax / al : vec3(0.0, 1.0, 0.0);
        vec3 side = cross(ax, view);
        float sl = length(side);
        side = sl > 1e-4 ? side / sl : normalize(cross(ax, vec3(1.0, 0.0, 0.0)));
        wp = c + ax * l.y * iD.z + side * l.x * rad;
    } else if (kind == 7) {                                              // shock wave ring on the ground
        wp = vec3(c.x + l.x * rad, c.y + 0.07, c.z + l.y * rad);
    } else if (kind == 10) {                                             // scorch decal
        wp = vec3(c.x + l.x * rad * 1.3, c.y + 0.05 + rad * 0.02, c.z + l.y * rad * 1.3);
    } else {                                                             // camera-facing puff
        vec3 right = cross(view, vec3(0.0, 1.0, 0.0));
        float rl = length(right);
        right = rl > 1e-3 ? right / rl : vec3(1.0, 0.0, 0.0);
        vec3 upv = cross(right, view);
        float ext = (kind == 4 || kind == 9) ? 1.0 : 1.5;                // smoke / fire edges may erode out to 1.5 r
        wp = c + (right * l.x + upv * l.y) * rad * ext;
        vP.z = ext;
    }
    vWorld = wp;
    gl_Position = uProj * uView * vec4(wp, 1.0);
}
)";

const char* kSpriteFrag = R"(
in vec2 vLocal;
in vec3 vWorld;
flat in vec3 vCentre;
flat in vec4 vP;
flat in vec4 vQ;
flat in vec2 vR;
layout(location = 0) out vec4 FragColor;
uniform sampler2D uSceneDepth;
uniform int uHasSceneDepth;

float linZ(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
// Soft particles: fade out where the sprite plane meets scene geometry.
float softDepth(float radius) {
    if (uHasSceneDepth == 0) return 1.0;
    float sd = texture(uSceneDepth, gl_FragCoord.xy / uResolution).r;
    if (sd >= 1.0) return 1.0;
    return clamp((linZ(sd) - linZ(gl_FragCoord.z)) / max(radius * 0.6, 0.05), 0.0, 1.0);
}
vec2 rot(vec2 p, float a) { float c = cos(a), s = sin(a); return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }
// Blackbody-like ramp: 0 black, deep red, orange, yellow, white-yellow
vec3 bb(float T) {
    vec3 c = mix(vec3(0.0), vec3(0.45, 0.03, 0.0), smoothstep(0.0, 0.2, T));
    c = mix(c, vec3(1.0, 0.28, 0.03), smoothstep(0.15, 0.45, T));
    c = mix(c, vec3(1.0, 0.62, 0.14), smoothstep(0.4, 0.7, T));
    c = mix(c, vec3(1.0, 0.90, 0.60), smoothstep(0.65, 1.0, T));
    return c;
}
vec3 fogLit(vec3 lit) { return uVolumetric == 1 || uHdr != 1 ? lit : lit * (1.0 - fogAmount(vWorld)); }

void main() {
    if (uViewMode == 2 || uViewMode == 3) discard;
    bool thermal = (uViewMode == 0 || uViewMode == 4);
    int kind = int(vP.y + 0.5);
    float radius = vP.x, ext = vP.z, age = vP.w;
    float t = vQ.x, seed = vQ.y, heat = vQ.z, bright = vQ.w;
    vec2 L = vLocal;
    float r = length(L) * ext;
    float soft = (kind == 7 || kind == 10) ? 1.0 : softDepth(radius);
    float ang = seed * 6.2831853 + age * (seed - 0.5) * 0.9;
    vec2 q = rot(L * ext, ang) * 1.8 + seed * 53.0;

    vec3 emit = vec3(0.0);   // premultiplied emissive colour (HDR, linear)
    vec3 body = vec3(0.0);   // premultiplied lit colour (alpha blended part)
    float alpha = 0.0;       // coverage of the alpha blended part / soot
    float cover = 0.0;       // thermal coverage
    float h = heat;          // thermal scalar

    if (kind == 1 || kind == 2 || kind == 3) {
        // ---- smoke / trail / dust: noise-eroded round puff, lit like a volume ----
        float n = fbm(q + vec2(0.0, -age * 0.25));
        float n2 = valueNoise(q * 3.1 + 7.0);
        float d = r + (n - 0.5) * 0.95 + (n2 - 0.5) * 0.25 + t * 0.35;
        float dens = smoothstep(1.0, 0.2, d);
        vec3 albedo; float op;
        if (kind == 1)      { albedo = vec3(0.085, 0.080, 0.076); op = 0.92; }
        else if (kind == 2) { albedo = vec3(0.34, 0.34, 0.35);    op = 0.38; }
        else                { albedo = vec3(0.20, 0.15, 0.10);    op = 0.55; }
        float fade = pow(max(1.0 - t, 0.0), 1.2) * smoothstep(0.0, 0.03, t + 0.03);
        alpha = dens * op * fade * soft * mix(0.75, 1.15, n);
        if (alpha < 0.004) discard;
        vec3 fwd = normalize(vCentre - uCamPos);
        vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)) + vec3(1e-4, 0.0, 0.0));
        vec3 upv = cross(right, fwd);
        float z = sqrt(max(1.0 - r * r * 0.8, 0.06));
        vec3 N = normalize(right * (L.x * ext * 0.8 + (n - 0.5) * 1.6) + upv * (L.y * ext * 0.8 + (n2 - 0.5) * 1.6) - fwd * z);
        vec3 Lm = normalize(-uMoonDir);
        float wd = dot(N, Lm) * 0.5 + 0.5;
        vec3 light = uMoonColor * wd * wd * 0.9 + uAmbient + uSkyZenith * (0.7 + 0.5 * N.y);
        vec3 toSpot = vCentre - uSpotPos;
        float ds = max(length(toSpot), 1e-3);
        float cone = clamp((dot(toSpot / ds, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
        light += uSpotColor * cone * cone / (1.0 + uSpotAtt.x * ds + uSpotAtt.y * ds * ds) * 0.6;
        for (int i = 0; i < 8; ++i) {
            if (i >= uPracticalCount) break;
            vec3 Lp;
            float att = practicalAttenuation(i, vCentre, Lp);
            float dd = dot(N, Lp) * 0.5 + 0.5;
            light += uPracticalColor[i].rgb * att * dd * dd * 2.0;
        }
        // Young smoke is lit from inside / below by the fire that made it
        float warm = heat * exp(-age * (kind == 1 ? 0.9 : 2.5));
        float below = 0.55 + 0.45 * clamp(-N.y + 0.2, 0.0, 1.0);
        light += vec3(1.0, 0.36, 0.08) * warm * below * (kind == 1 ? 2.2 : 0.8);
        body = fogLit(albedo * light * mix(0.55, 1.25, n2)) * alpha;
        h = 0.08 + (heat - 0.08) * exp(-age * (kind == 2 ? 0.8 : 0.35));
        cover = alpha * 0.6;
    } else if (kind == 0) {
        // ---- fire: animated temperature field -> blackbody ----
        vec2 qf = q * 0.9 + vec2(0.0, -age * 2.2);
        float n = fbm(qf);
        float n2 = fbm(qf * 2.3 + vec2(3.0, -age * 3.1));
        float d = r + (n - 0.5) * 0.95 + (n2 - 0.5) * 0.4;
        float T = clamp(1.0 - d, 0.0, 1.0);
        T = T * T * (3.0 - 2.0 * T);
        float life = max(1.0 - t, 0.0);
        T *= pow(life, 0.6) * mix(0.65, 1.0, clamp(heat, 0.0, 1.0));
        float cov = smoothstep(0.0, 0.2, T);
        emit = bb(T) * (1.5 + 16.0 * T * T) * bright * cov * soft;
        alpha = 0.5 * smoothstep(0.35, 0.95, t) * smoothstep(1.05, 0.4, d) * soft;   // soot edge darkens old flames
        emit = fogLit(emit);
        h = heat * mix(1.0, 0.75, t) * (0.85 + 0.15 * smoothstep(0.0, 0.5, T));
        cover = max(cov * 0.95, alpha) * soft;
        if (cover < 0.004) discard;
    } else if (kind == 4) {
        // ---- flash: very bright short sphere ----
        float g = exp(-r * r * 6.0) * smoothstep(1.0, 0.6, r);
        float f = pow(max(1.0 - t, 0.0), 1.5);
        emit = fogLit(mix(vec3(1.0, 0.55, 0.2), vec3(1.0, 0.92, 0.75), g) * g * f * bright * 18.0 * soft);
        h = 1.0;
        cover = g * f;
        if (cover < 0.003) discard;
    } else if (kind == 5 || kind == 6) {
        // ---- sparks / embers: hot streaks, cooling ----
        float Tn = clamp(heat * (1.0 - t * (kind == 5 ? 0.9 : 0.85)), 0.0, 1.0);
        float prof = kind == 5 ? exp(-L.x * L.x * 6.0) * smoothstep(1.0, 0.7, abs(L.y))
                               : exp(-dot(L, L) * 4.0) * smoothstep(1.0, 0.6, length(L));
        float grad = kind == 5 ? mix(0.2, 1.0, L.y * 0.5 + 0.5) : 1.0;
        float flick = kind == 6 ? 0.75 + 0.25 * sin(uTime * 17.0 + seed * 40.0) : 1.0;
        emit = fogLit(bb(0.1 + 0.9 * Tn) * (2.0 + 9.0 * Tn) * bright * prof * grad * flick);
        h = Tn;
        cover = prof * grad * smoothstep(0.0, 0.15, Tn);
        if (cover < 0.004) discard;
    } else if (kind == 7) {
        // ---- shock wave: thin bright ring racing across the ground ----
        float R = length(L);
        float ring = smoothstep(0.80, 0.93, R) * smoothstep(1.0, 0.94, R);
        float f = (1.0 - t) * (1.0 - t);
        float breakup = 0.65 + 0.35 * valueNoise(rot(L, ang) * 9.0);
        emit = fogLit(vec3(1.0, 0.82, 0.62) * ring * f * breakup * bright * 3.0);
        h = 0.45 * heat;
        cover = ring * f * 0.7;
        if (cover < 0.004) discard;
    } else if (kind == 8 || kind == 11) {
        // ---- tracer / exhaust flame: streak, bright head (+Y), fading tail ----
        float head = smoothstep(-1.0, 0.9, L.y);
        float g = head * head * smoothstep(1.0, 0.85, L.y);
        float w = kind == 11 ? 0.35 + 0.65 * (L.y * 0.5 + 0.5) : 1.0;
        float prof = exp(-(L.x * L.x) / (w * w) * 4.0);
        float flick = kind == 11 ? 0.8 + 0.2 * sin(uTime * 90.0 + seed * 30.0 + L.y * 5.0) : 1.0;
        vec3 outer = kind == 11 ? vec3(1.0, 0.45, 0.08) : vec3(1.0, 0.22, 0.04);
        vec3 inner = kind == 11 ? vec3(1.0, 0.92, 0.7) : vec3(1.0, 0.72, 0.35);
        float core = exp(-L.x * L.x * 22.0 / (w * w)) * g;
        emit = fogLit(mix(outer, inner, core) * g * prof * flick * bright * (kind == 11 ? 14.0 : 30.0));
        h = 0.95;
        cover = g * prof;
        if (cover < 0.004) discard;
    } else if (kind == 9) {
        // ---- exhaust / muzzle glow ----
        float g = exp(-r * r * 4.0) * smoothstep(1.0, 0.8, r);
        float flick = 0.85 + 0.15 * sin(uTime * 60.0 + seed * 20.0);
        emit = fogLit(mix(vec3(1.0, 0.4, 0.1), vec3(1.0, 0.85, 0.6), g * g) * g * bright * 10.0 * flick * soft);
        h = 1.0;
        cover = g;
        if (cover < 0.004) discard;
    } else {
        // ---- scorch decal: dark irregular patch, embers while young ----
        vec2 qs = rot(L, ang) * 2.2 + seed * 91.0;
        float n = fbm(qs);
        float n2 = valueNoise(qs * 4.0);
        float d = length(L) * 1.1 + (n - 0.5) * 0.75 + (n2 - 0.5) * 0.15;
        float dark = smoothstep(1.0, 0.35, d) * mix(0.92, 0.6, smoothstep(20.0, 300.0, age));
        if (dark < 0.01) discard;
        float hot = exp(-age / 14.0);
        float e = smoothstep(0.55, 0.8, fbm(qs * 3.0 + vec2(age * 0.05, 0.0))) * smoothstep(0.8, 0.15, d);
        float flick = 0.6 + 0.4 * sin(uTime * 2.7 + hash21(floor(qs * 9.0)) * 30.0);
        emit = fogLit(bb(0.25 + 0.4 * e * flick) * e * hot * flick * 3.0);
        alpha = dark * 0.92;
        h = heat * (0.6 + 0.4 * smoothstep(0.8, 0.2, d));
        cover = dark * 0.95;
    }

    if (thermal) {
        vec4 c = finalColor(vec3(0.0), h, vWorld);
        float a = clamp(cover, 0.0, 1.0);
        FragColor = vec4(c.rgb * a, a);
        return;
    }
    vec4 c = finalColor(vec3(0.0), 0.0, vWorld);   // keeps the surface target written; colour is ours
    FragColor = vec4(body + emit, clamp(alpha, 0.0, 1.0));
}
)";

const char* kBodyVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
out vec3 vWorld;
out vec3 vN;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vN = uNormalMat * aNormal;
    gl_Position = uProj * uView * w;
}
)";

const char* kBodyFrag = R"(
in vec3 vWorld;
in vec3 vN;
layout(location = 0) out vec4 FragColor;
uniform vec3 uBodyColor;
uniform vec3 uBodyEmit;
uniform float uBodyHeat;
void main() {
    vec3 N = normalize(vN);
    vec3 V = normalize(uCamPos - vWorld);
    vec3 Lm = normalize(-uMoonDir);
    vec3 light = uAmbient + uSkyZenith * (0.7 + 0.5 * N.y) + uMoonColor * max(dot(N, Lm), 0.0);
    vec3 H = normalize(Lm + V);
    vec3 spec = uMoonColor * pow(max(dot(N, H), 0.0), 40.0) * 0.6;
    for (int i = 0; i < 8; ++i) {
        if (i >= uPracticalCount) break;
        vec3 Lp;
        float att = practicalAttenuation(i, vWorld, Lp);
        light += uPracticalColor[i].rgb * att * max(dot(N, Lp), 0.0);
    }
    vec3 lit = uBodyColor * light + spec + uBodyEmit;
    FragColor = finalColor(lit, uBodyHeat, vWorld);
}
)";

Mat4 basisFromDirection(const Vec3& dir) {
    Vec3 z = length(dir) > 1e-5f ? normalize(dir) : Vec3{0, 0, 1};
    Vec3 up = std::fabs(z.y) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    Vec3 x = normalize(cross(up, z));
    Vec3 y = cross(z, x);
    Mat4 b;
    b.at(0, 0) = x.x; b.at(1, 0) = x.y; b.at(2, 0) = x.z;
    b.at(0, 1) = y.x; b.at(1, 1) = y.y; b.at(2, 1) = y.z;
    b.at(0, 2) = z.x; b.at(1, 2) = z.y; b.at(2, 2) = z.z;
    return b;
}

float hashf(float x) {
    const float s = std::sin(x * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

void pushInstance(std::vector<float>& v, const Vec3& pos, float radius, const Vec3& vel, float brightness, float t,
                  float seed, float heat, int kind, float age, float life, float halfLen) {
    const float d[kFloatsPerInstance] = {pos.x, pos.y, pos.z, radius, vel.x, vel.y, vel.z, brightness,
                                         t, seed, heat, static_cast<float>(kind), age, life, halfLen, 0.0f};
    v.insert(v.end(), d, d + kFloatsPerInstance);
}

}  // namespace

EffectsRenderer::EffectsRenderer() = default;

EffectsRenderer::~EffectsRenderer() {
    auto kill = [](GlMesh& m) {
        if (m.ebo) glDeleteBuffers(1, &m.ebo);
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
    };
    kill(cylinder_);
    kill(cone_);
    kill(cube_);
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void EffectsRenderer::makeMesh(GlMesh& m, const MeshData& data) {
    m.count = static_cast<GLsizei>(data.indices.size());
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, data.vertices.size() * sizeof(Vertex), data.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices.size() * sizeof(uint32_t), data.indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

bool EffectsRenderer::init() {
    sprite_ = std::make_unique<Shader>();
    body_ = std::make_unique<Shader>();
    if (!sprite_->build("effects-sprite", glsl::withLighting(kSpriteVert), glsl::withLightingAndOutput(kSpriteFrag)))
        return false;
    if (!body_->build("effects-body", glsl::withLighting(kBodyVert), glsl::withLightingAndOutput(kBodyFrag)))
        return false;
    for (Shader* s : {sprite_.get(), body_.get()}) {
        s->use();
        s->set("uMoonShadow", 5);
        s->set("uSpotShadow", 6);
        s->set("uAO", 7);
        s->set("uGroundMap", 8);
        s->set("uHeatTrail", 9);
    }
    sprite_->use();
    sprite_->set("uSceneDepth", 10);
    sprite_->set("uHasSceneDepth", 0);

    const MeshData cube = createCube();
    indexCount_ = static_cast<GLsizei>(cube.indices.size());
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glGenBuffers(1, &instanceVbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, cube.vertices.size() * sizeof(Vertex), cube.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cube.indices.size() * sizeof(uint32_t), cube.indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(2 + i);
        glVertexAttribDivisor(2 + i, 1);
    }
    glBindVertexArray(0);

    makeMesh(cylinder_, createCylinder(20, 0.5f, 0.5f, true));
    makeMesh(cone_, createCylinder(20, 0.5f, 0.0f, true));
    makeMesh(cube_, cube);
    scratch_.reserve(kMaxInstances * kFloatsPerInstance);
    ready_ = true;
    return true;
}

void EffectsRenderer::drawRange(size_t first, size_t count) {
    if (count == 0) return;
    const size_t stride = kFloatsPerInstance * sizeof(float);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    for (int i = 0; i < 4; ++i)
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(stride),
                              reinterpret_cast<void*>(first * stride + static_cast<size_t>(i) * 16));
    glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count));
}

int EffectsRenderer::drawScorches(const std::vector<ScorchMark>& scorches) {
    scratch_.clear();
    // Newest 64 marks; the oldest ones are the most faded anyway.
    const size_t begin = scorches.size() > 64 ? scorches.size() - 64 : 0;
    for (size_t i = begin; i < scorches.size(); ++i) {
        const ScorchMark& s = scorches[i];
        const float seed = hashf(s.position.x * 3.1f + s.position.z * 7.7f);
        const float heat = 0.1f + 0.75f * std::pow(std::max(1.0f - s.age / 60.0f, 0.0f), 1.5f);
        pushInstance(scratch_, s.position, std::max(s.radius, 0.1f), {0, 0, 0}, 1.0f, 0.0f, seed, heat, kScorch, s.age,
                     1.0e9f, 0.0f);
    }
    const size_t n = scratch_.size() / kFloatsPerInstance;
    if (n == 0) return 0;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, scratch_.size() * sizeof(float), scratch_.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    drawRange(0, n);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    return 1;
}

void EffectsRenderer::drawPart(const GlMesh& m, const Mat4& basis, const Vec3& scaleV, const Vec3& offset,
                               const Vec3& colour, const Vec3& emit, float heat) {
    const Mat4 model = basis * translate(offset) * scale(scaleV);
    body_->set("uModel", model);
    body_->set("uNormalMat", normalMatrix(model));
    body_->set("uBodyColor", colour);
    body_->set("uBodyEmit", emit);
    body_->set("uBodyHeat", heat);
    glBindVertexArray(m.vao);
    glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
}

int EffectsRenderer::drawBodies(const std::vector<Projectile>& projectiles) {
    int calls = 0;
    for (const Projectile& p : projectiles) {
        if (!p.alive || p.kind == ProjectileKind::Tracer) continue;
        Vec3 dir = p.velocity;
        if (length(dir) < 1e-3f) dir = p.position - p.previous;
        const Mat4 basis = translate(p.position) * basisFromDirection(dir);
        if (p.kind == ProjectileKind::Missile) {
            // 1.55 m long, 0.18 m diameter: body, ogive nose, nozzle ring, four tail fins
            drawPart(cylinder_, basis, {0.18f, 0.18f, 1.15f}, {0, 0, 0.0f}, {0.07f, 0.075f, 0.065f}, {0, 0, 0}, 0.38f);
            drawPart(cone_, basis, {0.18f, 0.18f, 0.32f}, {0, 0, 0.73f}, {0.03f, 0.03f, 0.03f}, {0, 0, 0}, 0.4f);
            drawPart(cylinder_, basis, {0.15f, 0.15f, 0.1f}, {0, 0, -0.62f}, {0.02f, 0.02f, 0.02f}, {2.5f, 0.9f, 0.25f}, 1.0f);
            calls += 3;
            for (int k = 0; k < 4; ++k) {
                const Mat4 fin = basis * rotateZ(radians(90.0f * static_cast<float>(k)));
                drawPart(cube_, fin, {0.46f, 0.012f, 0.2f}, {0, 0, -0.42f}, {0.05f, 0.052f, 0.048f}, {0, 0, 0}, 0.36f);
                ++calls;
            }
        } else {
            // tank shell: short glowing slug with a blunt nose
            drawPart(cylinder_, basis, {0.13f, 0.13f, 0.45f}, {0, 0, 0}, {0.1f, 0.05f, 0.02f}, {6.0f, 2.2f, 0.6f}, 0.95f);
            drawPart(cone_, basis, {0.13f, 0.13f, 0.2f}, {0, 0, 0.32f}, {0.1f, 0.05f, 0.02f}, {8.0f, 3.0f, 0.8f}, 1.0f);
            calls += 2;
        }
    }
    glBindVertexArray(0);
    return calls;
}

int EffectsRenderer::drawSprites(const EffectsFrame& f, const Vec3& cam) {
    // ---- build the two groups ----
    std::vector<float> alphaGroup, addGroup;
    alphaGroup.reserve(4096 * kFloatsPerInstance);
    addGroup.reserve(4096 * kFloatsPerInstance);
    size_t budget = kMaxInstances;
    if (f.particles) {
        for (const EffectParticle& p : *f.particles) {
            if (budget == 0) break;
            if (p.life <= 0.0f || p.age >= p.life || p.age < 0.0f) continue;
            const float t = p.age / p.life;
            float radius = p.size + p.growth * p.age;
            if (radius <= 0.0f) continue;
            int kind = kSmoke;
            switch (p.type) {
                case EffectType::Fire: kind = kFire; break;
                case EffectType::Smoke: kind = kSmoke; break;
                case EffectType::Spark: kind = kSpark; break;
                case EffectType::Flash: kind = kFlash; break;
                case EffectType::Shockwave: kind = kShock; break;
                case EffectType::Trail: kind = kTrail; break;
                case EffectType::Dust: kind = kDust; break;
                case EffectType::Ember: kind = kEmber; break;
            }
            const bool smokeGroup = kind == kSmoke || kind == kTrail || kind == kDust;
            std::vector<float>& dst = smokeGroup ? alphaGroup : addGroup;
            Vec3 pos = p.position, vel = p.velocity;
            float halfLen = 0.0f;
            if (kind == kSpark || kind == kEmber) {
                const float speed = length(vel);
                halfLen = kind == kSpark ? std::clamp(speed * 0.04f, radius * 2.0f, 1.5f) : radius * 1.4f;
                if (speed > 1e-3f && kind == kSpark) pos = pos - vel * (halfLen / speed);   // head at the particle
            }
            pushInstance(dst, pos, radius, vel, p.brightness, t, p.seed, p.heat, kind, p.age, p.life, halfLen);
            --budget;
        }
    }
    if (f.projectiles) {
        for (const Projectile& p : *f.projectiles) {
            if (!p.alive) continue;
            Vec3 dir = p.velocity;
            if (length(dir) < 1e-3f) dir = p.position - p.previous;
            dir = length(dir) > 1e-5f ? normalize(dir) : Vec3{0, 0, 1};
            const float seed = hashf(static_cast<float>(p.owner) * 1.7f + p.age * 0.0f + 3.0f);
            if (p.kind == ProjectileKind::Tracer) {
                const float len = std::max(3.0f, length(p.position - p.previous));
                pushInstance(addGroup, p.position - dir * (len * 0.5f), 0.07f, dir, 1.0f, 0.0f, seed, 0.95f, kTracer,
                             p.age, 1.0e9f, len * 0.5f);
                pushInstance(addGroup, p.position, 0.18f, {0, 0, 0}, 0.5f, 0.0f, seed, 1.0f, kGlow, p.age, 1.0e9f, 0.0f);
            } else if (p.kind == ProjectileKind::Missile) {
                pushInstance(addGroup, p.position - dir * 0.75f, 0.22f, {0, 0, 0}, 1.0f, 0.0f, seed, 1.0f, kGlow, p.age,
                             1.0e9f, 0.0f);
                pushInstance(addGroup, p.position - dir * 1.75f, 0.13f, dir, 1.0f, 0.0f, seed, 1.0f, kFlame, p.age,
                             1.0e9f, 1.0f);
            } else {
                pushInstance(addGroup, p.position - dir * 1.0f, 0.1f, dir, 0.8f, 0.0f, seed, 0.95f, kTracer, p.age,
                             1.0e9f, 1.0f);
                pushInstance(addGroup, p.position, 0.3f, {0, 0, 0}, 0.6f, 0.0f, seed, 1.0f, kGlow, p.age, 1.0e9f, 0.0f);
            }
        }
    }

    // ---- sort back to front inside each group ----
    auto sortGroup = [&](std::vector<float>& g) {
        const size_t n = g.size() / kFloatsPerInstance;
        if (n < 2) return;
        order_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float* d = &g[i * kFloatsPerInstance];
            const float dx = d[0] - cam.x, dy = d[1] - cam.y, dz = d[2] - cam.z;
            order_[i] = {dx * dx + dy * dy + dz * dz, static_cast<unsigned>(i)};
        }
        std::sort(order_.begin(), order_.end(), [](const Sortable& a, const Sortable& b) { return a.key > b.key; });
        std::vector<float> sorted(g.size());
        for (size_t i = 0; i < n; ++i)
            std::memcpy(&sorted[i * kFloatsPerInstance], &g[order_[i].index * kFloatsPerInstance],
                        kFloatsPerInstance * sizeof(float));
        g.swap(sorted);
    };
    sortGroup(alphaGroup);
    sortGroup(addGroup);

    const size_t na = alphaGroup.size() / kFloatsPerInstance, nb = addGroup.size() / kFloatsPerInstance;
    if (na + nb == 0) return 0;
    scratch_.clear();
    scratch_.insert(scratch_.end(), alphaGroup.begin(), alphaGroup.end());
    scratch_.insert(scratch_.end(), addGroup.begin(), addGroup.end());

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, scratch_.size() * sizeof(float), scratch_.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    int calls = 0;
    if (na) { drawRange(0, na); ++calls; }
    if (nb) { drawRange(na, nb); ++calls; }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
    return calls;
}

}  // namespace ts
