#include "rendering/ShadersVegetation.h"

namespace ts::glsl::veg {

// ---------------------------------------------------------------- instance transform + hierarchical wind
// Instance: iA = world position (tree root), yaw (radians); iB = scale xz, scale y, random seed, LOD dither.
// Wind, three levels, each with its own frequency so the motion never looks like one rigid sway:
//   1) trunk sway: the whole tree bends like a cantilever, displacement ~ (height fraction)^2, slow gusts that travel
//      across the forest (phase from the world position along the wind);
//   2) branches: weight = distance from the trunk axis (COLOR_0.r), a faster swing with a per-card phase (COLOR_0.b);
//   3) leaf / needle flutter: weight = distance from the card's anchor (COLOR_0.g), fast, along the card normal.
const char* kWindCommon = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aWind;
layout(location = 4) in vec4 iA;
layout(location = 5) in vec4 iB;
uniform float uTime;
uniform vec4 uWindAmp;    // x trunk sway (tip displacement / height), y branch swing (m), z flutter (m), w native height (m)
uniform vec2 uWindDir;
uniform float uMinY;      // native lowest point of the variant (roots at y = 0)

mat3 yawMat(float a) { float c = cos(a), s = sin(a); return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c); }

vec3 vegPosition(out vec3 N, out float h01) {
    vec3 p = aPos;
    p.y -= uMinY;
    vec3 sc = vec3(iB.x, iB.y, iB.x);
    float H = uWindAmp.w * iB.y;
    h01 = tsClamp(p.y / max(uWindAmp.w, 1e-3), 0.0, 1.0);
    mat3 R = yawMat(iA.w);
    vec3 w = R * (p * sc);
    N = tsNormalize(R * (aNormal / sc));
    vec2 wd = uWindDir;
    float ph = tsDot(iA.xz, vec2(0.21, 0.13)) + iB.z * 6.2831;
    float gust = 0.6 + 0.4 * sin(uTime * 0.45 - tsDot(iA.xz, wd) * 0.09) * sin(uTime * 0.21 + ph);
    float sway = uWindAmp.x * H * gust * (0.75 + 0.25 * sin(uTime * 1.1 + ph)) * h01 * h01;
    vec3 off = vec3(wd.x, 0.0, wd.y) * sway;
    off.y -= 0.5 * sway * sway / max(H, 0.1);
    float bphase = aWind.b * 6.2831;
    float br = aWind.r * uWindAmp.y * iB.x * (0.4 + gust) * sin(uTime * (1.7 + 0.6 * aWind.b) + ph + bphase);
    off += vec3(wd.x, 0.5, wd.y) * br;
    float fl = aWind.g * uWindAmp.z * iB.x * (0.3 + gust) * sin(uTime * (6.0 + 3.0 * aWind.b) + bphase * 3.0 + tsDot(w, vec3(3.1)));
    off += N * fl;
    return iA.xyz + w + off;
}
)";

const char* kMeshVert = R"(
uniform mat4 uView, uProj;
out vec3 vWorld;
out vec3 vN;
out vec2 vUV;
out vec4 vWind;
out float vSeed;
out float vDither;
out float vH01;
void main() {
    vec3 N;
    float h01;
    vec3 P = vegPosition(N, h01);
    vWorld = P;
    vN = N;
    vUV = aUV;
    vWind = aWind;
    vSeed = iB.z;
    vDither = iB.w;
    vH01 = h01;
    gl_Position = uProj * uView * vec4(P, 1.0);
}
)";

// Shared fragment helpers: LOD dither, cheap foliage shadow, cotangent-frame normal mapping, foliage lighting.
const char* kFoliageLight = R"(
uniform int uA2C;           // 1: alpha-to-coverage (multisampled target)
uniform float uAlbedoScale; // calibration of the texture albedo against the scene's materials
uniform vec3 uTintA, uTintB;
uniform float uTransmission;

// Interleaved gradient noise (Jimenez 2014): screen-space dither for the LOD cross-fade (TAA resolves it).
float ign(vec2 p) { return fract(52.9829189 * fract(tsDot(p, vec2(0.06711056, 0.00583715)))); }
bool ditherKill(float d) {
    if (d == 0.0) return false;
    float n = ign(gl_FragCoord.xy);
    return d > 0.0 ? n >= d : n < -d;
}
// Alpha-to-coverage: the texture alpha of the needle / leaf cards becomes the fraction of the 4 MSAA samples covered.
// Needles are thinner than a texel: their anti-aliased alpha (0.2-0.7) is real partial coverage, which a binary 0.5
// alpha test would throw away (the crown looks bare). A soft ramp keeps it as a resolved haze of needles, and the box-
// filtered mips keep the average coverage at a distance; TAA smooths the coverage dither. Without MSAA: alpha test.
float cardAlpha(float a) {
    if (uA2C == 1) return tsClamp((a - 0.18) * 2.4, 0.0, 1.0);
    return a >= 0.35 ? 1.0 : 0.0;
}
// Key-light visibility for foliage: 4 Poisson taps of the cascade (the card's own alpha-tested depth is in the map).
float vegShadow(vec3 P) {
    if (uShadows == 0) return 1.0;
    float dist = tsLength(P - uCamPos);
    int layer = dist < uCascadeSplit ? 0 : 1;
    mat4 vp = layer == 0 ? uMoonVP0 : uMoonVP1;
    float texel = layer == 0 ? uMoonTexel.x : uMoonTexel.y;
    vec4 lp = vp * vec4(P + tsNormalize(-uMoonDir) * texel * 2.5, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    vec2 size = vec2(textureSize(uMoonShadow, 0).xy);
    mat2 rot = pixelRotation();
    float s = 0.0;
    for (int i = 0; i < 4; ++i) s += texture(uMoonShadow, vec4(p.xy + rot * kPoisson[i] * 1.6 / size, float(layer), p.z - 0.0008));
    return s * 0.25 * cloudShadowAt(P);
}
// Normal mapping without stored tangents: the cotangent frame from the screen derivatives of position and UV (Schueler).
vec3 perturbNormal(vec3 N, vec3 P, vec2 uv, vec3 tn) {
    vec3 dp1 = dFdx(P), dp2 = dFdy(P);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = tsCross(dp2, N), dp1perp = tsCross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float m = max(tsDot(T, T), tsDot(B, B));
    if (!(m > 1e-20)) return N;
    float k = inversesqrt(m);
    return tsNormalize(mat3(T * k, B * k, N) * tn);
}
vec3 instanceTint(float seed) {
    float s1 = fract(seed * 7.13), s2 = fract(seed * 13.7 + 0.3);
    return tsMix(uTintA, uTintB, s1) * (0.9 + 0.2 * s2);
}
// Two-sided foliage: wrapped diffuse on the lit side, transmission (thin leaves glow when the light is behind them),
// a little sheen; the same lamp / beacon / searchlight terms as the scene's foliage cards (Foliage.cpp).
vec3 foliageLighting(vec3 albedo, vec3 P, vec3 N, vec3 Ngeo, vec3 V, float ao, float sm, float thickness) {
    vec3 L = tsNormalize(-uMoonDir);
    float ndl = tsDot(N, L);
    float wrap = max((ndl + 0.35) / 1.35, 0.0);
    float back = max(-tsDot(Ngeo, L), 0.0);
    float through = pow(max(tsDot(-V, L), 0.0), 4.0);
    float glow = tsMix(1.1, 0.9, uDayWeight);   // a rim of light through the leaves, not a lamp (the moon is dim)
    vec3 transTint = vec3(0.7, 0.95, 0.5);
    vec3 key = uMoonColor * sm * (albedo * wrap + albedo * transTint * (0.45 * back + 0.9 * through) * glow * uTransmission * thickness);
    key += uMoonColor * sm * pow(max(tsDot(N, tsNormalize(L + V)), 0.0), 32.0) * 0.015;
    vec3 amb = uHdr == 1 ? hemiAmbient(N) : uAmbient;
    vec3 lit = albedo * amb * ao + key;
    vec3 d = uBeaconPos - P;
    float dd = tsLength(d);
    lit += albedo * uBeaconColor * tsMix(0.35, 1.0, abs(tsDot(N, d / dd))) / (1.0 + uBeaconAtt.x * dd + uBeaconAtt.y * dd * dd);
    for (int i = 0; i < TS_MAX_PRACTICALS; ++i) {
        if (i >= uPracticalCount) break;
        vec3 Lp;
        float att = practicalAttenuation(i, P, Lp);
        lit += albedo * uPracticalColor[i].rgb * att * tsMix(0.35, 1.0, abs(tsDot(N, Lp)));
    }
    d = uSpotPos - P;
    dd = tsLength(d);
    vec3 Ls = d / dd;
    float cone = tsClamp((tsDot(-Ls, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    vec3 spot = uSpotColor * cone * cone * tsMix(0.35, 1.0, abs(tsDot(N, Ls))) / (1.0 + uSpotAtt.x * dd + uSpotAtt.y * dd * dd);
    if (max(spot.r, spot.g) > 1e-4) lit += albedo * spot * spotShadow(P, N);
    return lit;
}
)";

const char* kMeshFrag = R"(
in vec3 vWorld;
in vec3 vN;
in vec2 vUV;
in vec4 vWind;
in float vSeed;
in float vDither;
in float vH01;
layout(location = 0) out vec4 FragColor;
uniform sampler2D uAlbedoTex, uNormalTex, uArmTex;
uniform int uFoliage;       // 1: alpha-tested card material (twigs, leaves, blades)
void main() {
    if (ditherKill(vDither)) discard;
    // +0.75 mip bias on the cards: needle detail finer than the TAA can hold flickers under the sub-pixel jitter.
    vec4 tex = texture(uAlbedoTex, vUV, uFoliage == 1 ? 0.75 : 0.0);
    float alpha = 1.0;
    if (uFoliage == 1) {
        alpha = cardAlpha(tex.a);
        if (alpha <= 0.0) discard;
    }
    vec3 Ng = tsNormalize(vN);
    if (!gl_FrontFacing) Ng = -Ng;
    float heat = uFoliage == 1 ? 0.13 + 0.03 * vH01 + 0.02 * (vSeed - 0.5) : surfaceHeat(vWorld, 0.0) + solarLoad(vWorld, Ng);
    if (uViewMode == 0 || uViewMode >= 3) {
        FragColor = finalColor(vec3(0.0), heat, vWorld);
        FragColor.a = alpha;
        return;
    }
    vec3 V = tsNormalize(uCamPos - vWorld);
    vec3 tn = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;
    tn.y = -tn.y;                                   // glTF UV v runs down the image; OpenGL normal maps have +Y up
    vec3 N = perturbNormal(Ng, vWorld, vUV, tsNormalize(tsMix(vec3(0.0, 0.0, 1.0), tn, uFoliage == 1 ? 0.6 : 1.0)));
    vec3 arm = texture(uArmTex, vUV).rgb;
    vec3 albedo = toLinear(tex.rgb) * uAlbedoScale * instanceTint(vSeed);
    if (uViewMode == 2) albedo = uBaseColor;
    float ao = vWind.a * tsMix(1.0, arm.r, 0.5) * ambientOcclusion();
    vec3 lit;
    if (uFoliage == 1) {
        lit = foliageLighting(albedo, vWorld, N, Ng, V, ao, vegShadow(vWorld), 1.0);
    } else {
        // bark / rock: GGX key light with the texture roughness, hemisphere + sky reflection ambient
        vec3 L = tsNormalize(-uMoonDir);
        float sm = max(tsDot(Ng, L), 0.0) > 0.0 ? moonShadow(vWorld, Ng) : 0.0;
        vec3 dk, sk;
        brdf(N, V, L, uMoonColor * sm, vec3(0.04), tsClamp(arm.g, 0.3, 1.0), dk, sk);
        vec3 amb = uHdr == 1 ? hemiAmbient(N) : uAmbient;
        vec3 R = tsReflect(-V, N);
        vec3 env = uHdr == 1 ? skyEnvironment(R, arm.g) * 0.04 * (1.0 - arm.g) : vec3(0.0);
        lit = albedo * (amb * ao + dk) + sk + env * ao;
        vec3 d = uBeaconPos - vWorld;
        float dd = tsLength(d);
        lit += albedo * uBeaconColor * max(tsDot(N, d / dd), 0.0) / (1.0 + uBeaconAtt.x * dd + uBeaconAtt.y * dd * dd);
        for (int i = 0; i < TS_MAX_PRACTICALS; ++i) {
            if (i >= uPracticalCount) break;
            vec3 Lp;
            float att = practicalAttenuation(i, vWorld, Lp);
            lit += albedo * uPracticalColor[i].rgb * att * max(tsDot(N, Lp), 0.0);
        }
        d = uSpotPos - vWorld;
        dd = tsLength(d);
        vec3 Ls = d / dd;
        float cone = tsClamp((tsDot(-Ls, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
        vec3 spot = uSpotColor * cone * cone * max(tsDot(N, Ls), 0.0) / (1.0 + uSpotAtt.x * dd + uSpotAtt.y * dd * dd);
        if (max(spot.r, spot.g) > 1e-4) lit += albedo * spot * spotShadow(vWorld, Ng);
    }
    FragColor = finalColor(lit, heat, vWorld);
    FragColor.a = alpha;
}
)";

// ---------------------------------------------------------------- shadow maps
const char* kDepthVert = R"(
uniform mat4 uLightVP;
out vec2 vUV;
void main() {
    vec3 N;
    float h01;
    vec3 P = vegPosition(N, h01);
    vUV = aUV;
    gl_Position = uLightVP * vec4(P, 1.0);
}
)";

const char* kDepthFrag = R"(
in vec2 vUV;
uniform sampler2D uAlbedoTex;
uniform int uFoliage;
void main() {
    if (uFoliage == 1 && texture(uAlbedoTex, vUV).a < 0.35) discard;
}
)";

// ---------------------------------------------------------------- pre-pass (SSAO / fog depth)
const char* kPrepassVert = R"(
uniform mat4 uView, uProj;
out vec2 vUV;
out vec3 vNormalView;
out float vDither;
void main() {
    vec3 N;
    float h01;
    vec3 P = vegPosition(N, h01);
    vUV = aUV;
    vNormalView = mat3(uView) * N;
    vDither = iB.w;
    gl_Position = uProj * uView * vec4(P, 1.0);
}
)";

const char* kPrepassFrag = R"(
in vec2 vUV;
in vec3 vNormalView;
in float vDither;
uniform sampler2D uAlbedoTex;
uniform int uFoliage;
out vec4 Normal;
float ign(vec2 p) { return fract(52.9829189 * fract(tsDot(p, vec2(0.06711056, 0.00583715)))); }
void main() {
    if (vDither != 0.0) {
        float n = ign(gl_FragCoord.xy);
        if (vDither > 0.0 ? n >= vDither : n < -vDither) discard;
    }
    if (uFoliage == 1 && texture(uAlbedoTex, vUV).a < 0.35) discard;
    vec3 n = tsNormalize(vNormalView);
    if (!gl_FrontFacing) n = -n;
    Normal = vec4(n * 0.5 + 0.5, 1.0);
}
)";

// ---------------------------------------------------------------- octahedral impostors
// One camera-facing quad per tree. The view direction (object space) picks the four nearest pictures of the
// hemi-octahedral atlas (bilinear weights); each picture's texture coordinates come from projecting the quad point
// onto that picture's own image plane, so the four blend without ghosting while the camera orbits.
const char* kImpostorVert = R"(
layout(location = 0) in vec2 aCorner;
layout(location = 4) in vec4 iA;
layout(location = 5) in vec4 iB;
uniform mat4 uView, uProj;
uniform vec3 uImpCentre;    // native bounding sphere centre (object space, roots at y = 0)
uniform float uImpRadius;
uniform float uImpGrid;
out vec3 vWorld;
out vec2 vTap0;
out vec2 vTap1;
out vec2 vTap2;
out vec2 vTap3;
flat out vec4 vW;
flat out vec4 vCellA;   // cells of taps 0, 1 (i, j, i, j)
flat out vec4 vCellB;   // cells of taps 2, 3
flat out float vYaw;
flat out float vSeed;
flat out float vDither;
out float vH01;

mat3 yawMat(float a) { float c = cos(a), s = sin(a); return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c); }
void impBasis(vec3 d, out vec3 right, out vec3 up) {
    vec3 hint = abs(d.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    right = tsNormalize(tsCross(hint, d));
    up = tsCross(d, right);
}
vec3 octDecode(vec2 uv) {
    float a = 2.0 * uv.x - 1.0, b = 2.0 * uv.y - 1.0;
    float x = 0.5 * (a + b), z = 0.5 * (a - b);
    float y = max(1.0 - abs(x) - abs(z), 0.0);
    return tsNormalize(vec3(x, y, z));
}
vec2 octEncode(vec3 d) {
    d.y = max(d.y, 0.0);
    d /= (abs(d.x) + d.y + abs(d.z));
    return vec2(0.5 * (d.x + d.z) + 0.5, 0.5 * (d.x - d.z) + 0.5);
}
// 'local': the quad point in the variant's own (unscaled, unrotated) space, so a picture maps onto the instance with its
// own non-uniform scale (width / height) and yaw.
vec2 tapUV(vec3 local, vec2 cell) {
    vec3 d = octDecode((cell + 0.5) / uImpGrid);
    vec3 right, up;
    impBasis(d, right, up);
    return vec2(tsDot(local, right), tsDot(local, up)) / (2.0 * uImpRadius) + 0.5;
}
void main() {
    mat3 R = yawMat(iA.w);
    float s = iB.y;
    vec3 C = iA.xyz + R * (uImpCentre * vec3(iB.x, iB.y, iB.x));
    float rad = uImpRadius * max(iB.x, iB.y);
    vec3 Vw = tsNormalize(uCamPos - C);
    vec3 right, up;
    impBasis(Vw, right, up);
    vec3 P = C + (right * aCorner.x + up * aCorner.y) * rad;
    vec3 Vo = tsNormalize((tsTranspose(R) * Vw) / vec3(iB.x, iB.y, iB.x));
    vec2 g = tsClamp(octEncode(Vo) * uImpGrid - 0.5, vec2(0.0), vec2(uImpGrid - 1.0));
    vec2 c0 = min(floor(g), vec2(uImpGrid - 1.0));
    vec2 c1 = min(c0 + 1.0, vec2(uImpGrid - 1.0));
    vec2 f = g - c0;
    vW = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    vCellA = vec4(c0.x, c0.y, c1.x, c0.y);
    vCellB = vec4(c0.x, c1.y, c1.x, c1.y);
    vec3 local = (tsTranspose(R) * (P - C)) / vec3(iB.x, iB.y, iB.x);
    vTap0 = tapUV(local, vec2(c0.x, c0.y));
    vTap1 = tapUV(local, vec2(c1.x, c0.y));
    vTap2 = tapUV(local, vec2(c0.x, c1.y));
    vTap3 = tapUV(local, vec2(c1.x, c1.y));
    vWorld = P;
    vYaw = iA.w;
    vSeed = iB.z;
    vDither = iB.w;
    vH01 = tsClamp((P.y - iA.y) / max(2.0 * uImpCentre.y * s, 0.1), 0.0, 1.0);
    gl_Position = uProj * uView * vec4(P, 1.0);
}
)";

// Shared by the impostor colour and pre-pass fragment shaders: the blended atlas sample.
#define TS_IMPOSTOR_SAMPLE R"(
uniform sampler2D uImpAlbedo, uImpNormal;
uniform float uImpGrid;
in vec3 vWorld;
in vec2 vTap0;
in vec2 vTap1;
in vec2 vTap2;
in vec2 vTap3;
flat in vec4 vW;
flat in vec4 vCellA;
flat in vec4 vCellB;
flat in float vYaw;
flat in float vSeed;
flat in float vDither;
in float vH01;
void impTap(vec2 uv, vec2 cell, float w, inout vec4 alb, inout vec4 nrm) {
    if (w <= 0.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return;
    vec2 at = (cell + tsClamp(uv, 0.002, 0.998)) / uImpGrid;
    alb += texture(uImpAlbedo, at) * w;
    nrm += texture(uImpNormal, at) * w;
}
void impSample(out vec4 alb, out vec4 nrm) {
    alb = vec4(0.0);
    nrm = vec4(0.0);
    impTap(vTap0, vCellA.xy, vW.x, alb, nrm);
    impTap(vTap1, vCellA.zw, vW.y, alb, nrm);
    impTap(vTap2, vCellB.xy, vW.z, alb, nrm);
    impTap(vTap3, vCellB.zw, vW.w, alb, nrm);
    // colour and normal were premultiplied by coverage in the atlas: un-premultiply
    if (alb.a > 1e-4) { alb.rgb /= alb.a; nrm /= alb.a; }
}
mat3 yawMat(float a) { float c = cos(a), s = sin(a); return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c); }
)"

const char* kImpostorFrag = TS_IMPOSTOR_SAMPLE R"(
layout(location = 0) out vec4 FragColor;
void main() {
    if (ditherKill(vDither)) discard;
    vec4 alb, nrm;
    impSample(alb, nrm);
    float alpha = uA2C == 1 ? tsClamp(alb.a * 1.15, 0.0, 1.0) : (alb.a >= 0.35 ? 1.0 : 0.0);   // the atlas holds coverage
    if (alpha <= 0.0) discard;
    float heat = 0.13 + 0.03 * vH01 + 0.02 * (vSeed - 0.5);
    if (uViewMode == 0 || uViewMode >= 3) {
        FragColor = finalColor(vec3(0.0), heat, vWorld);
        FragColor.a = alpha;
        return;
    }
    vec3 N = tsNormalize(yawMat(vYaw) * (nrm.rgb * 2.0 - 1.0));
    vec3 V = tsNormalize(uCamPos - vWorld);
    vec3 albedo = toLinear(alb.rgb) * uAlbedoScale * instanceTint(vSeed);
    float ao = tsClamp(nrm.a, 0.0, 1.0) * ambientOcclusion();
    vec3 lit = foliageLighting(albedo, vWorld, N, N, V, ao, vegShadow(vWorld), 0.8);
    FragColor = finalColor(lit, heat, vWorld);
    FragColor.a = alpha;
}
)";

const char* kImpostorPrepassFrag = R"(#version 330 core
uniform mat4 uView;
)" TS_IMPOSTOR_SAMPLE R"(
out vec4 Normal;
float ign(vec2 p) { return fract(52.9829189 * fract(tsDot(p, vec2(0.06711056, 0.00583715)))); }
void main() {
    if (vDither != 0.0) {
        float n = ign(gl_FragCoord.xy);
        if (vDither > 0.0 ? n >= vDither : n < -vDither) discard;
    }
    vec4 alb, nrm;
    impSample(alb, nrm);
    if (alb.a < 0.35) discard;
    vec3 N = tsNormalize(yawMat(vYaw) * (nrm.rgb * 2.0 - 1.0));
    Normal = vec4(tsNormalize(mat3(uView) * N) * 0.5 + 0.5, 1.0);
}
)";
#undef TS_IMPOSTOR_SAMPLE

// ---------------------------------------------------------------- impostor baking (object space, no wind)
const char* kBakeVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aWind;
uniform mat4 uViewProj;
uniform float uMinY;
out vec3 vN;
out vec2 vUV;
out float vAO;
void main() {
    vec3 p = aPos;
    p.y -= uMinY;
    vN = aNormal;
    vUV = aUV;
    vAO = aWind.a;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

const char* kBakeFrag = R"(#version 330 core
in vec3 vN;
in vec2 vUV;
in float vAO;
uniform sampler2D uAlbedoTex;
uniform sampler2D uArmTex;
uniform int uFoliage;
uniform vec3 uBakeDir;     // object-space direction towards the bake camera
layout(location = 0) out vec4 Albedo;
layout(location = 1) out vec4 Normal;
void main() {
    vec4 t = texture(uAlbedoTex, vUV);
    // Binary coverage at the 2x super-sampled bake resolution: a card texel counts where the mesh pass covers at least
    // half of its samples (cardAlpha 0.5). The down-sampled atlas then holds the true fraction of the texel the crown
    // covers. (Storing the soft card alpha of only the front-most fragment made the impostors look transparent.)
    if (uFoliage == 1 && t.a < 0.39) discard;
    vec3 n = tsNormalize(vN);
    if (tsDot(n, uBakeDir) < 0.0) n = -n;        // two-sided: the face the camera sees
    float ao = vAO * tsMix(1.0, texture(uArmTex, vUV).r, 0.5);
    Albedo = vec4(t.rgb, 1.0);
    Normal = vec4(n * 0.5 + 0.5, ao);
}
)";

}  // namespace ts::glsl::veg
