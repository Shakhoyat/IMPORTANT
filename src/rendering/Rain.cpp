#include "rendering/Rain.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "primitives/Cube.h"
#include "rendering/ShaderSources.h"

namespace ts {

namespace {

const char* kRainVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec4 iDrop;   // x, z in [0,1) of the box, phase, size
uniform mat4 uView, uProj;
uniform float uTime;
uniform vec3 uBox;                    // box size around the camera (x, y, z)
uniform vec3 uWind;                   // horizontal drift, m/s
uniform float uFallSpeed;             // m/s
out float vT;                         // 0 .. 1 along the streak (head -> tail)
out vec3 vWorld;
void main() {
    vec3 fall = normalize(vec3(uWind.x, -uFallSpeed, uWind.z));
    float speed = length(vec3(uWind.x, -uFallSpeed, uWind.z));
    // Position inside the wrapping box: every drop falls forever, re-entering at the top.
    vec3 origin = uCamPos - 0.5 * uBox;
    float travel = uTime * speed + iDrop.z * uBox.y;
    vec3 p = vec3(iDrop.x * uBox.x, uBox.y - mod(travel, uBox.y), iDrop.y * uBox.z);
    p.xz += (uWind.xz / uFallSpeed) * (uBox.y - p.y);            // wind drift over the fall
    p = mod(p - (origin - floor(origin / uBox) * uBox), uBox) + origin;  // wrap around the camera
    // Streak: the unit cube stretched along the fall direction (length = distance fallen in ~1/40 s)
    vec3 side = normalize(cross(fall, vec3(0.0, 0.0, 1.0)));
    vec3 side2 = cross(side, fall);
    float len = speed / 40.0 * iDrop.w;
    float width = 0.0035 * iDrop.w;
    vec3 local = side * aPos.x * width + side2 * aPos.z * width - fall * aPos.y * len;
    vT = aPos.y + 0.5;
    vWorld = p + local;
    gl_Position = uProj * uView * vec4(vWorld, 1.0);
}
)";

const char* kRainFrag = R"(
in float vT;
in vec3 vWorld;
uniform float uRainAmount;            // 0 .. 1
layout(location = 0) out vec4 FragColor;
void main() {
    vec3 V = normalize(vWorld - uCamPos);                          // eye -> drop
    // Forward scattering: a drop between the eye and a light looks much brighter (refraction / glint).
    vec3 light = uSkyZenith * 0.6 + uMoonColor * 0.08 * (0.3 + pow(max(dot(V, -uMoonDir), 0.0), 8.0));
    vec3 toSpot = vWorld - uSpotPos;
    float ds = length(toSpot);
    float cone = clamp((dot(toSpot / ds, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    light += uSpotColor * cone * cone / (1.0 + uSpotAtt.x * ds + uSpotAtt.y * ds * ds) * 0.25 * (0.4 + 2.0 * pow(max(dot(V, toSpot / ds), 0.0), 6.0));
    for (int i = 0; i < 8; ++i) {
        if (i >= uPracticalCount) break;
        vec3 Lp;
        float att = practicalAttenuation(i, vWorld, Lp);
        light += uPracticalColor[i].rgb * att * 0.3 * (0.4 + 2.0 * pow(max(dot(V, -Lp), 0.0), 6.0));
    }
    float fade = smoothstep(0.0, 0.3, vT) * smoothstep(1.0, 0.6, vT);   // soft ends
    float dist = length(vWorld - uCamPos);
    float nearFade = smoothstep(0.3, 1.2, dist) * (1.0 - smoothstep(14.0, 22.0, dist));
    FragColor = vec4(light * fade * nearFade * uRainAmount, 1.0);    // additive
}
)";

struct Drop {
    float x, z, phase, size;
};

}  // namespace

RainField::~RainField() {
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool RainField::init() {
    shader_ = std::make_unique<Shader>();
    if (!shader_->build("rain", glsl::withLighting(kRainVert), glsl::withLightingAndOutput(kRainFrag))) return false;
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
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Drop), reinterpret_cast<void*>(0));
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

void RainField::setCount(int drops) {
    if (drops == count_) return;
    count_ = drops;
    std::vector<Drop> all(static_cast<size_t>(std::max(drops, 0)));
    uint32_t s = 0xC0FFEEu;
    auto next = [&s] {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / static_cast<float>(0x1000000);
    };
    for (Drop& d : all) d = {next(), next(), next(), 0.7f + 0.6f * next()};
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, all.size() * sizeof(Drop), all.data(), GL_STATIC_DRAW);
}

void RainField::draw() {
    if (count_ <= 0) return;
    glBindVertexArray(vao_);
    glDrawElementsInstanced(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr, count_);
    glBindVertexArray(0);
}

}  // namespace ts
