// v7 WP-Q: instanced variants of the scene-part programs (flat / Gouraud / Phong, depth-only, pre-pass).
// Same lighting code as ShaderSources.cpp; the per-object parameters (TS_PER_OBJECT) become globals that each stage
// loads from the per-instance buffer texture (rendering/Instancing.h) before it shades, so the maths is unchanged.
//
// Per-instance layout of the lit programs, 11 RGBA32F texels (Renderer::packInstance):
//   0..3  model matrix columns
//   4..6  normal matrix columns (xyz); w = heat, emissive, specular
//   7     base colour, shininess
//   8     roughness, metalness, surface, heat variation
//   9     part centre, part radius
//   10    fogged, opacity
// Depth-only: 4 texels (model). Pre-pass: 7 texels (model, normal matrix).
#include "rendering/ShaderSources.h"

namespace ts::glsl {

namespace {

#define TS_TERM_VARYINGS(q, dir, p) \
    q dir vec3 p##Amb; q dir vec3 p##D0; q dir vec3 p##S0; q dir vec3 p##D1; q dir vec3 p##S1; \
    q dir vec3 p##D2; q dir vec3 p##S2; q dir vec3 p##Sa; q dir float p##Irr;
#define TS_STR2(x) #x
#define TS_STR(x) TS_STR2(x)

const char* kFetch = R"(
uniform samplerBuffer uInstances;
uniform int uInstanceBase;
vec4 tsInst(int i, int k) { return texelFetch(uInstances, i * 11 + k); }
mat4 tsModel(int i) { return mat4(tsInst(i, 0), tsInst(i, 1), tsInst(i, 2), tsInst(i, 3)); }
mat3 tsNormalMat(int i) { return mat3(tsInst(i, 4).xyz, tsInst(i, 5).xyz, tsInst(i, 6).xyz); }
void tsLoadLight(int i) {
    vec4 a = tsInst(i, 4), b = tsInst(i, 5), c = tsInst(i, 6), d = tsInst(i, 7), e = tsInst(i, 8);
    uHeat = a.w; uEmissive = b.w; uSpecular = c.w;
    uBaseColor = d.xyz; uShininess = d.w;
    uRoughness = e.x; uMetalness = e.y; uSurface = int(floor(e.z + 0.5)); uHeatVariation = e.w;
}
)";

const char* kFetchOutput = R"(
void tsLoadOutput(int i) {
    vec4 f = tsInst(i, 9), g = tsInst(i, 10);
    uPartCentre = f.xyz; uPartRadius = f.w;
    uFogged = g.x; uOpacity = g.y;
}
)";

// ---------------------------------------------------------------- Flat
const char* kFlatVertI = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uView, uProj;
out vec3 gWorld;
flat out int gInstance;
void main() {
    int i = uInstanceBase + gl_InstanceID;
    vec4 w = tsModel(i) * vec4(aPos, 1.0);
    gWorld = w.xyz;
    gInstance = i;
    gl_Position = uProj * uView * w;
}
)";

const char* kFlatGeomI = "layout(triangles) in;\nlayout(triangle_strip, max_vertices = 3) out;\nin vec3 gWorld[];\n"
    "flat in int gInstance[];\nflat out int fInstance;\n"
    TS_STR(TS_TERM_VARYINGS(flat, out, f)) R"(
flat out vec3 fNormal;
out vec3 fWorld;
void main() {
    tsLoadLight(gInstance[0]);
    vec3 a = gWorld[0], b = gWorld[1], c = gWorld[2];
    vec3 cr = tsCross(b - a, c - a);
    vec3 N = tsLength(cr) > 1e-12 ? tsNormalize(cr) : vec3(0.0, 1.0, 0.0);   // degenerate (zero-area) triangle
    Terms t = computeLight((a + b + c) / 3.0, N);
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        fWorld = gWorld[i];
        fNormal = N;
        fInstance = gInstance[0];
        fAmb = t.amb; fD0 = t.d0; fS0 = t.s0; fD1 = t.d1; fS1 = t.s1; fD2 = t.d2; fS2 = t.s2; fSa = t.sa; fIrr = t.irr;
        EmitVertex();
    }
    EndPrimitive();
}
)";

const char* kFlatFragI = TS_STR(TS_TERM_VARYINGS(flat, in, f)) R"(
flat in vec3 fNormal;
in vec3 fWorld;
flat in int fInstance;
layout(location = 0) out vec4 FragColor;
void main() {
    tsLoadLight(fInstance);
    tsLoadOutput(fInstance);
    Terms t = Terms(fAmb, fD0, fS0, fD1, fS1, fD2, fS2, fSa, fIrr);
    FragColor = shadeFragment(t, fWorld, fNormal);
}
)";

// ---------------------------------------------------------------- Gouraud
const char* kGouraudVertI = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uView, uProj;
)" TS_STR(TS_TERM_VARYINGS(, out, v)) R"(
out vec3 vWorld;
out vec3 vNormal;
flat out int vInstance;
void main() {
    int i = uInstanceBase + gl_InstanceID;
    tsLoadLight(i);
    vec4 w = tsModel(i) * vec4(aPos, 1.0);
    vec3 N = tsNormalize(tsNormalMat(i) * aNormal);
    Terms t = computeLight(w.xyz, N);    // lighting per VERTEX
    vAmb = t.amb; vD0 = t.d0; vS0 = t.s0; vD1 = t.d1; vS1 = t.s1; vD2 = t.d2; vS2 = t.s2; vSa = t.sa; vIrr = t.irr;
    vWorld = w.xyz;
    vNormal = N;
    vInstance = i;
    gl_Position = uProj * uView * w;
}
)";

const char* kGouraudFragI = TS_STR(TS_TERM_VARYINGS(, in, v)) R"(
in vec3 vWorld;
in vec3 vNormal;
flat in int vInstance;
layout(location = 0) out vec4 FragColor;
void main() {
    tsLoadLight(vInstance);
    tsLoadOutput(vInstance);
    Terms t = Terms(vAmb, vD0, vS0, vD1, vS1, vD2, vS2, vSa, vIrr);
    FragColor = shadeFragment(t, vWorld, tsNormalize(vNormal));
}
)";

// ---------------------------------------------------------------- Phong
const char* kPhongVertI = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uView, uProj;
out vec3 vWorld;
out vec3 vNormal;
flat out int vInstance;
void main() {
    int i = uInstanceBase + gl_InstanceID;
    vec4 w = tsModel(i) * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = tsNormalMat(i) * aNormal;
    vInstance = i;
    gl_Position = uProj * uView * w;
}
)";

const char* kPhongFragI = R"(
in vec3 vWorld;
in vec3 vNormal;
flat in int vInstance;
layout(location = 0) out vec4 FragColor;
void main() {
    tsLoadLight(vInstance);
    tsLoadOutput(vInstance);
    vec3 N = tsNormalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 dnx = dFdx(N), dny = dFdy(N);
    gSpecAA = uHdr == 1 ? min(0.5 * (tsDot(dnx, dnx) + tsDot(dny, dny)), 0.18) : 0.0;   // v8 specular AA
    FragColor = shadeFragment(computeLight(vWorld, N), vWorld, N);
}
)";

// Inserts the instancing switches right after the #version line of an assembled source.
std::string instanced(const std::string& src) {
    const size_t eol = src.find('\n');
    return src.substr(0, eol + 1) + "#define TS_PER_OBJECT\n#define TS_INSTANCED 1\n" + src.substr(eol + 1);
}

}  // namespace

const char* kDepthVertI = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform samplerBuffer uInstances;
uniform int uInstanceBase;
uniform mat4 uLightVP;
void main() {
    int b = (uInstanceBase + gl_InstanceID) * 4;
    mat4 M = mat4(texelFetch(uInstances, b), texelFetch(uInstances, b + 1), texelFetch(uInstances, b + 2),
                  texelFetch(uInstances, b + 3));
    gl_Position = uLightVP * M * vec4(aPos, 1.0);
}
)";

const char* kPrepassVertI = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform samplerBuffer uInstances;
uniform int uInstanceBase;
uniform mat4 uView, uProj;
out vec3 vNormalView;
void main() {
    int b = (uInstanceBase + gl_InstanceID) * 7;
    mat4 M = mat4(texelFetch(uInstances, b), texelFetch(uInstances, b + 1), texelFetch(uInstances, b + 2),
                  texelFetch(uInstances, b + 3));
    mat3 N = mat3(texelFetch(uInstances, b + 4).xyz, texelFetch(uInstances, b + 5).xyz, texelFetch(uInstances, b + 6).xyz);
    vNormalView = mat3(uView) * (N * aNormal);
    gl_Position = uProj * uView * M * vec4(aPos, 1.0);
}
)";

InstancedSources instancedSources(ShadingKind kind) {
    const std::string fetch = kFetch, out = std::string(kFetch) + kFetchOutput;
    InstancedSources s;
    switch (kind) {
        case ShadingKind::Flat:
            s.vert = instanced(withLighting((fetch + kFlatVertI).c_str()));
            s.geom = instanced(withLighting((fetch + kFlatGeomI).c_str()));
            s.frag = instanced(withLightingAndOutput((out + kFlatFragI).c_str()));
            break;
        case ShadingKind::Gouraud:
            s.vert = instanced(withLighting((fetch + kGouraudVertI).c_str()));
            s.frag = instanced(withLightingAndOutput((out + kGouraudFragI).c_str()));
            break;
        case ShadingKind::Phong:
            s.vert = instanced(withLighting((fetch + kPhongVertI).c_str()));
            s.frag = instanced(withLightingAndOutput((out + kPhongFragI).c_str()));
            break;
    }
    return s;
}

}  // namespace ts::glsl
