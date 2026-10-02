#include "rendering/Particles.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "objects/Terrain.h"
#include "primitives/Cube.h"
#include "rendering/ShaderSources.h"

namespace ts {

namespace {

const char* kParticleVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 iP;       // fireflies: x, ground y, z, seed (0..1); motes: seed in 1..2, xyz = box coords 0..1
uniform mat4 uView, uProj;
uniform float uTime;
uniform vec3 uBox;
out vec2 vLocal;                       // -1 .. 1 across the sprite
out vec3 vWorld;
flat out vec3 vCentre;
out float vGlow;                       // firefly emission (0 for motes)
flat out float vKind;                  // 0 firefly, 1 mote
void main() {
    bool mote = iP.w >= 1.0;
    float seed = fract(iP.w);
    vec3 c;
    float radius;
    float glow = 0.0;
    if (!mote) {
        // Wander: sum of incommensurate sines = smooth closed-ish path around the home point
        float t = uTime;
        float a = 6.2831853 * seed;
        vec3 w = vec3(sin(t * 0.31 + a) + 0.6 * sin(t * 0.83 + 3.0 * a),
                      0.5 * sin(t * 0.47 + 5.0 * a) + 0.3 * sin(t * 1.10 + a),
                      cos(t * 0.27 + 2.0 * a) + 0.6 * sin(t * 0.71 + 7.0 * a));
        float hover = 0.3 + 2.2 * fract(seed * 7.13);
        c = vec3(iP.x, iP.y + hover, iP.z) + vec3(w.x * 0.9, w.y * 0.45, w.z * 0.9);
        c.y = max(c.y, iP.y + 0.25);
        // Blink: period 1-3 s, lit for ~30 % of the cycle with a soft attack and a longer decay
        float period = 1.0 + 2.0 * fract(seed * 13.7);
        float ph = fract(t / period + fract(seed * 91.3));
        float on = ph < 0.3 ? sin(3.14159265 * ph / 0.3) : 0.0;
        glow = on * on * (2.0 + 6.0 * fract(seed * 5.37));       // HDR 2 .. 8
        radius = 0.035;
    } else {
        // Motes: drifting slowly inside the box that wraps around the camera
        float s2 = seed * 43.7;
        vec3 p = iP.xyz * uBox + vec3(0.03, -0.02, 0.02) * uTime * 3.0
               + vec3(sin(uTime * 0.4 + s2), sin(uTime * 0.31 + 2.0 * s2), cos(uTime * 0.37 + 3.0 * s2)) * 0.25;
        vec3 origin = uCamPos - 0.5 * uBox;
        c = mod(p - (origin - floor(origin / uBox) * uBox), uBox) + origin;
        radius = 0.006 + 0.008 * fract(s2);
    }
    // Keep far points at least ~1.1 px wide so they never vanish or shimmer
    float dist = length(c - uCamPos);
    radius = max(radius, dist * (mote ? 0.0010 : 0.0016));
    vec3 fwd = normalize(c - uCamPos);
    vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 upv = cross(right, fwd);
    vLocal = aPos.xy * 2.0;                 // the cube's front and back face coincide: each point is drawn twice (0.5 weight below)
    vec3 wp = c + (right * aPos.x + upv * aPos.y) * 2.0 * radius;
    vWorld = wp;
    vCentre = c;
    vGlow = glow;
    vKind = mote ? 1.0 : 0.0;
    gl_Position = uProj * uView * vec4(wp, 1.0);
}
)";

const char* kParticleFrag = R"(
in vec2 vLocal;
in vec3 vWorld;
flat in vec3 vCentre;
in float vGlow;
flat in float vKind;
layout(location = 0) out vec4 FragColor;
void main() {
    if (uViewMode != 1) discard;                       // visible view only
    float r = length(vLocal);
    if (r >= 1.0) discard;
    float dist = length(vCentre - uCamPos);
    if (vKind < 0.5) {
        // Firefly: hot core and soft halo, warm yellow-green, emissive HDR
        float core = exp(-r * r * 9.0);
        float halo = exp(-r * 3.0) * 0.25;
        vec3 col = mix(vec3(0.75, 1.0, 0.28), vec3(1.0, 0.95, 0.55), core);
        float fade = 1.0 - smoothstep(30.0, 45.0, dist);
        FragColor = vec4(col * (core + halo) * vGlow * fade * 0.5, 1.0);
        return;
    }
    // Mote: scatters light from the moon, the searchlight cone and practical lights (forward-peaked)
    vec3 V = normalize(vCentre - uCamPos);
    vec3 light = uSkyZenith * 0.2 + uMoonColor * 0.03 * (0.3 + pow(max(dot(V, -uMoonDir), 0.0), 8.0));
    vec3 toSpot = vCentre - uSpotPos;
    float ds = max(length(toSpot), 1e-3);
    float cone = clamp((dot(toSpot / ds, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    light += uSpotColor * cone * cone / (1.0 + uSpotAtt.x * ds + uSpotAtt.y * ds * ds) * 0.12 * (0.4 + 2.0 * pow(max(dot(V, toSpot / ds), 0.0), 6.0));
    for (int i = 0; i < TS_MAX_PRACTICALS; ++i) {
        if (i >= uPracticalCount) break;
        vec3 Lp;
        float att = practicalAttenuation(i, vCentre, Lp);
        light += uPracticalColor[i].rgb * att * 0.15 * (0.4 + 2.0 * pow(max(dot(V, -Lp), 0.0), 6.0));
    }
    float sprite = exp(-r * r * 4.0);
    float nearFade = smoothstep(0.4, 1.5, dist) * (1.0 - smoothstep(9.0, 16.0, dist));
    float tw = 0.6 + 0.4 * sin(uTime * 2.0 + dot(vCentre, vec3(12.9, 7.3, 4.1)));   // tumbling glints
    FragColor = vec4(light * sprite * nearFade * tw * (1.0 - 0.7 * uRain) * 0.5, 1.0);
}
)";

struct Instance {
    float x, y, z, w;
};

}  // namespace

ParticleField::~ParticleField() {
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool ParticleField::init() {
    shader_ = std::make_unique<Shader>();
    if (!shader_->build("particles", glsl::withLighting(kParticleVert), glsl::withLightingAndOutput(kParticleFrag)))
        return false;
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, cube.indices.size() * sizeof(uint32_t), cube.indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), reinterpret_cast<void*>(0));
    glVertexAttribDivisor(2, 1);
    glBindVertexArray(0);
    shader_->use();
    shader_->set("uMoonShadow", 5);
    shader_->set("uSpotShadow", 6);
    shader_->set("uAO", 7);
    shader_->set("uGroundMap", 8);
    shader_->set("uHeatTrail", 9);
    return true;
}

void ParticleField::setCount(int n) {
    if (n == count_) return;
    count_ = std::max(n, 0);
    const int fireflies = count_ * 6 / 10;
    std::vector<Instance> all;
    all.reserve(static_cast<size_t>(count_));
    uint32_t s = 0xF1EF11E5u;
    auto next = [&s] {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / static_cast<float>(0x1000000);
    };
    // Fireflies: rejection-sample the compound; favour meadow (away from the bunker pad, the track and the
    // creek bed) and the forest edge ring near the fence.
    int guard = 0;
    while (static_cast<int>(all.size()) < fireflies && guard++ < fireflies * 60) {
        const float ang = 6.2831853f * next();
        const float r = 3.5f + (kFenceRadius - 4.0f) * std::sqrt(next());
        const float x = r * std::cos(ang), z = r * std::sin(ang);
        if (std::fabs(x - trackCentreX(z)) < kTrackHalfWidth * 1.4f && z > 0.0f) continue;
        if (std::fabs(z - creekCentreZ(x)) < kCreekHalfWidth * 1.3f) continue;
        const float edge = std::clamp((r - 11.0f) / 6.0f, 0.0f, 1.0f);   // denser toward the tree line
        if (next() > 0.35f + 0.65f * edge) continue;
        all.push_back({x, terrainHeight(x, z), z, next() * 0.999f});
    }
    // Motes: seed 1..2 in w, position inside the unit box
    while (static_cast<int>(all.size()) < count_) all.push_back({next(), next(), next(), 1.0f + next() * 0.999f});
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, all.size() * sizeof(Instance), all.data(), GL_STATIC_DRAW);
}

void ParticleField::drawInstances() {
    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, count_);
    glBindVertexArray(0);
}

}  // namespace ts
