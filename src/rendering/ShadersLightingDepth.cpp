// v9 M8 "lighting depth" GLSL: the analytic sky shared by the sky, the ambient / reflection terms and the aerial
// perspective; the AgX display transform; ground-truth ambient occlusion (GTAO); screen-space contact shadows.
// The constants come from the C++ mirrors (math/SkyModel.h, math/ToneMap.h) so the CPU tests check what the GPU runs.
#include <cstdio>
#include <string>

#include "math/ToneMap.h"
#include "rendering/ShaderSources.h"

namespace ts::glsl {

namespace {

std::string lit(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    std::string s = buf;
    if (s.find_first_of(".eE") == std::string::npos) s += ".0";
    return s;
}

std::string mat3Lit(const float* m) {
    std::string s = "mat3(";
    for (int i = 0; i < 9; ++i) s += lit(m[i]) + (i < 8 ? ", " : ")");
    return s;
}

}  // namespace

// Preetham sky (math/SkyModel.cpp skyRadiance) + its order-2 SH ambient (evalAmbient). The renderer fills the uniforms
// once per frame from the same SkyModel / Atmosphere::skySH the CPU computed.
std::string skyModelBlock() {
    return R"(
uniform vec3 uSkyPerez[5];   // Perez A..E; x: luminance Y, y: chromaticity x, z: chromaticity y
uniform vec3 uSkyZenithN;    // (Y_z, x_z, y_z) / F(0, theta_s)
uniform vec3 uSkySun;        // unit vector to the sun used by the model
uniform float uSkyScale;     // kcd/m^2 -> HDR radiance
uniform vec3 uSkySH[9];      // ambient (irradiance / pi) of sky + ground, real SH order 2
vec3 perezF3(float cosT, float gamma) {
    float cg = cos(gamma);
    return (1.0 + uSkyPerez[0] * exp(uSkyPerez[1] / max(cosT, 0.01))) * (1.0 + uSkyPerez[2] * exp(uSkyPerez[3] * gamma) + uSkyPerez[4] * cg * cg);
}
vec3 skyXyYToRgb(float x, float y, float Y) {
    float yy = max(y, 1e-4);
    float X = x / yy * Y, Z = (1.0 - x - y) / yy * Y;
    return max(vec3(3.2406 * X - 1.5372 * Y - 0.4986 * Z, -0.9689 * X + 1.8758 * Y + 0.0415 * Z, 0.0557 * X - 0.2040 * Y + 1.0570 * Z), vec3(0.0));
}
// Clear-sky radiance (no sun disc) towards dir; below the horizon the horizon value.
vec3 skyModelRadiance(vec3 dir) {
    dir = tsNormalize(dir);
    float gamma = acos(tsClamp(tsDot(dir, uSkySun), -1.0, 1.0));
    vec3 v = uSkyZenithN * perezF3(dir.y, gamma);
    return skyXyYToRgb(v.y, v.z, v.x) * uSkyScale;
}
// Ambient light (irradiance / pi) on a face with normal n: the SH projection of the same sky + the sunlit ground.
vec3 skyAmbientSH(vec3 n) {
    vec3 e = uSkySH[0] * 0.282095
           + uSkySH[1] * (0.488603 * n.y) + uSkySH[2] * (0.488603 * n.z) + uSkySH[3] * (0.488603 * n.x)
           + uSkySH[4] * (1.092548 * n.x * n.y) + uSkySH[5] * (1.092548 * n.y * n.z)
           + uSkySH[6] * (0.315392 * (3.0 * n.z * n.z - 1.0)) + uSkySH[7] * (1.092548 * n.x * n.z)
           + uSkySH[8] * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(e, vec3(0.0));
}
)";
}

// AgX "base contrast" (math/ToneMap.cpp agx): inset, log2 encoding, sigmoid, outset, EOTF 2.2.
std::string agxBlock() {
    using namespace tonemap;
    std::string s;
    s += "const mat3 kAgxInset = " + mat3Lit(kAgxInset) + ";\n";
    s += "const mat3 kAgxOutset = " + mat3Lit(kAgxOutset) + ";\n";
    s += "const float kAgxMinEv = " + lit(kAgxMinEv) + ", kAgxMaxEv = " + lit(kAgxMaxEv) + ";\n";
    s += "float agxSigmoid(float x) {\n    float s = " + lit(kAgxPoly[0]) + ";\n";
    for (int i = 1; i < 7; ++i) s += "    s = s * x + " + lit(kAgxPoly[i]) + ";\n";
    s += "    return s;\n}\n";
    s += R"(vec3 agxDisplay(vec3 c) {   // scene-linear -> display-linear 0..1
    c = kAgxInset * max(c, vec3(0.0));
    c = (tsClamp(log2(max(c, vec3(1e-10))), kAgxMinEv, kAgxMaxEv) - kAgxMinEv) / (kAgxMaxEv - kAgxMinEv);
    c = kAgxOutset * vec3(agxSigmoid(c.r), agxSigmoid(c.g), agxSigmoid(c.b));
    return pow(tsClamp(c, 0.0, 1.0), vec3(2.2));
}
)";
    return s;
}

// ---------------------------------------------------------------- GTAO (pre-pass resolution)
// Ground-truth ambient occlusion (Jimenez et al. 2016): per pixel a few slices through the view vector; in each slice
// the two horizon angles h0, h1 are found by marching the depth buffer both ways, and the cosine-weighted visible arc
// between them is integrated in closed form around the normal projected into the slice (angle n):
//     a = (cos n + 2 h sin n - cos(2 h - n)) / 4   for h = h0 and h1,   visibility = |N_proj| (a0 + a1)
// Slice angles and step offsets rotate per pixel (interleaved gradient noise) and per frame, so the bilateral blur and
// the TAA accumulate many more directions than one frame takes.
const char* kGtaoFrag = R"(
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform mat4 uProj, uInvProj;
uniform float uRadius;     // metres
uniform int uSlices, uSteps;
uniform float uFrame;      // frame index (temporal rotation)
uniform float uPower;
out float AO;

vec3 viewPos(vec2 uv) {
    float z = texture(uDepth, uv).r * 2.0 - 1.0;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, z, 1.0);
    return p.xyz / p.w;
}
float ign(vec2 p) { return fract(52.9829189 * fract(tsDot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) { AO = 1.0; return; }
    vec3 P = viewPos(vUV);
    vec3 N = tsNormalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
    vec3 V = tsNormalize(-P);
    vec2 res = vec2(textureSize(uDepth, 0));
    // Radius in pixels at this depth (projection scale), at most 1/8 of the frame height.
    float rPix = min(uRadius * uProj[1][1] * 0.5 * res.y / max(-P.z, 1e-3), res.y * 0.125);
    if (rPix < 1.0) { AO = 1.0; return; }
    float noiseA = fract(ign(gl_FragCoord.xy) + uFrame * 0.618034);
    float noiseB = fract(ign(gl_FragCoord.yx + 17.0) + uFrame * 0.754878);
    float vis = 0.0;
    for (int s = 0; s < 8; ++s) {
        if (s >= uSlices) break;
        float phi = (float(s) + noiseA) * 3.14159265 / float(uSlices);
        vec2 omega = vec2(cos(phi), sin(phi));
        vec3 dirV = vec3(omega, 0.0);
        vec3 orthoDir = dirV - tsDot(dirV, V) * V;
        vec3 axis = tsNormalize(tsCross(orthoDir, V));
        vec3 projN = N - axis * tsDot(N, axis);
        float projLen = tsLength(projN);
        float sgn = sign(tsDot(orthoDir, projN));
        float cosN = tsClamp(tsDot(projN, V) / max(projLen, 1e-4), 0.0, 1.0);
        float n = sgn * acos(cosN);
        float hc0 = cos(n + 1.5707963), hc1 = cos(n - 1.5707963);   // lowest horizons: the tangent plane
        float low0 = hc0, low1 = hc1;
        for (int j = 0; j < 16; ++j) {
            if (j >= uSteps) break;
            float t = (float(j) + noiseB) / float(uSteps);
            vec2 off = omega * max(t * t * rPix, 1.0 + float(j)) / res;
            vec3 S0 = viewPos(vUV + off) - P, S1 = viewPos(vUV - off) - P;
            float l0 = tsLength(S0), l1 = tsLength(S1);
            float w0 = 1.0 - tsSmoothstep(uRadius * 0.6, uRadius, l0), w1 = 1.0 - tsSmoothstep(uRadius * 0.6, uRadius, l1);
            hc0 = max(hc0, tsMix(low0, tsDot(S0 / max(l0, 1e-4), V), w0));
            hc1 = max(hc1, tsMix(low1, tsDot(S1 / max(l1, 1e-4), V), w1));
        }
        float h0 = n + tsClamp(-acos(tsClamp(hc1, -1.0, 1.0)) - n, -1.5707963, 1.5707963);
        float h1 = n + tsClamp(acos(tsClamp(hc0, -1.0, 1.0)) - n, -1.5707963, 1.5707963);
        float a0 = (cosN + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) * 0.25;
        float a1 = (cosN + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) * 0.25;
        vis += projLen * (a0 + a1);
    }
    vis = tsClamp(vis / float(max(uSlices, 1)), 0.0, 1.0);
    AO = pow(vis, uPower);
}
)";

// ---------------------------------------------------------------- Contact shadows (pre-pass resolution)
// Shadow maps cannot resolve the last few centimetres where an object meets the ground (texel size, depth bias). A short
// ray towards the sun is marched through the depth buffer instead: a sample that lies behind the visible surface by
// less than a thickness is an occluder, so feet, wheels, crates and wall bases get the dark line that grounds them.
const char* kContactFrag = R"(
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform mat4 uProj, uInvProj;
uniform vec3 uLightView;   // unit vector to the sun, view space
uniform float uLength;     // metres
uniform int uSteps;
uniform float uFrame;
uniform float uStrength;
out float Contact;

vec3 viewPos(vec2 uv) {
    float z = texture(uDepth, uv).r * 2.0 - 1.0;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, z, 1.0);
    return p.xyz / p.w;
}
float ign(vec2 p) { return fract(52.9829189 * fract(tsDot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    Contact = 1.0;
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) return;
    vec3 P = viewPos(vUV);
    vec3 N = tsNormalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
    if (tsDot(N, uLightView) <= 0.02) return;              // facing away: N.L and the shadow map already darken it
    float dist = -P.z;
    if (dist > 60.0) return;
    float len = uLength * (1.0 + 0.015 * dist);
    vec3 start = P + N * (0.006 + 0.0012 * dist);
    float jitter = fract(ign(gl_FragCoord.xy) + uFrame * 0.618034);
    float occ = 0.0;
    for (int i = 0; i < 32; ++i) {
        if (i >= uSteps) break;
        float t = (float(i) + jitter) / float(uSteps);
        vec3 Q = start + uLightView * (len * t);
        vec4 c = uProj * vec4(Q, 1.0);
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) break;
        float sceneDepth = -viewPos(uv).z, rayDepth = -Q.z;
        float delta = rayDepth - sceneDepth;              // > 0: the ray point is hidden behind the visible surface
        float thick = 0.12 + 0.01 * rayDepth;
        float bias = 0.004 + 0.0008 * rayDepth;
        float hit = tsSmoothstep(bias, 2.0 * bias, delta) * (1.0 - tsSmoothstep(0.6 * thick, thick, delta));
        occ = max(occ, hit * (1.0 - 0.5 * t));             // occluders near the start shade more
        if (occ > 0.95) break;
    }
    occ *= 1.0 - tsSmoothstep(35.0, 60.0, dist);
    Contact = 1.0 - occ * uStrength;
}
)";

}  // namespace ts::glsl
