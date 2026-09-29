#include "rendering/ShaderSources.h"

namespace ts::glsl {

namespace {

// Thermal colour maps (same stops as rendering/ThermalPalette.h): 0 ironbow, 1 white-hot, 2 rainbow.
const char* kPalette = R"(
uniform int uPalette;
vec3 paletteStops(float t, vec3 c0, vec3 c1, vec3 c2, vec3 c3, vec3 c4, vec3 c5, vec3 c6) {
    t = clamp(t, 0.0, 1.0) * 6.0;
    if (t < 1.0) return mix(c0, c1, t);
    if (t < 2.0) return mix(c1, c2, t - 1.0);
    if (t < 3.0) return mix(c2, c3, t - 2.0);
    if (t < 4.0) return mix(c3, c4, t - 3.0);
    if (t < 5.0) return mix(c4, c5, t - 4.0);
    return mix(c5, c6, t - 5.0);
}
vec3 thermalPalette(float t) {
    if (uPalette == 1) return vec3(pow(clamp(t, 0.0, 1.0), 0.9));          // white-hot
    if (uPalette == 3) return vec3(1.0 - pow(clamp(t, 0.0, 1.0), 0.9));    // black-hot
    if (uPalette == 2)                                                     // rainbow
        return paletteStops(t, vec3(0.00, 0.00, 0.05), vec3(0.05, 0.10, 0.60), vec3(0.00, 0.70, 0.90),
                            vec3(0.10, 0.85, 0.20), vec3(1.00, 0.95, 0.10), vec3(1.00, 0.20, 0.05), vec3(1.0));
    return paletteStops(t, vec3(0.00, 0.00, 0.03), vec3(0.12, 0.02, 0.33), vec3(0.46, 0.03, 0.55),   // ironbow
                        vec3(0.80, 0.10, 0.34), vec3(0.98, 0.42, 0.06), vec3(1.00, 0.78, 0.16), vec3(1.00, 1.00, 0.88));
}
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i), b = hash21(i + vec2(1, 0)), c = hash21(i + vec2(0, 1)), d = hash21(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) { s += a * valueNoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s;
}
)";

// Lighting shared by the three shading pipelines. WHERE it is evaluated is what differs:
// per triangle (flat), per vertex (Gouraud) or per fragment (Phong). Each light's diffuse and specular
// terms are kept separate so that shadows and ambient occlusion can still be applied per fragment.
const char* kLighting = R"(
uniform vec3 uCamPos;
uniform vec3 uAmbient;
uniform vec3 uMoonDir;    uniform vec3 uMoonColor;                               // L0 directional
uniform vec3 uBeaconPos;  uniform vec3 uBeaconColor; uniform vec2 uBeaconAtt;    // L1 point
uniform vec3 uSpotPos;    uniform vec3 uSpotDir;     uniform vec3 uSpotColor;    // L2 spot
uniform vec2 uSpotCos;    uniform vec2 uSpotAtt;
uniform vec3 uBaseColor;  uniform float uHeat; uniform float uEmissive;
uniform float uSpecular;  uniform float uShininess; uniform float uHeatVariation;
uniform float uRoughness; uniform float uMetalness; uniform int uSurface;
uniform int uViewMode;    // 0 thermal, 1 visible, 2 primitive diagnostic, 3 depth, 4 raw heat (tracker)
uniform int uHdr;         // 1: linear HDR pipeline (visible view), 0: display-referred (v1 look)
uniform int uPbr;         // 1: GGX / Cook-Torrance, 0: classic Blinn-Phong
uniform vec3 uSkyZenith;  uniform vec3 uGroundBounce;
uniform float uWetness;   // 0 dry .. 1 soaked: up-facing surfaces get glossy (water film)
uniform float uRain;      // rain intensity (sky, fog, ripples)

const float PI = 3.14159265;

// Practical lights (lamps, fire, floodlight): up to 8, windowed inverse-square falloff (smooth cut-off at range).
uniform int uPracticalCount;
uniform vec4 uPracticalPos[8];    // xyz position, w range
uniform vec4 uPracticalColor[8];  // rgb linear radiance scale
uniform vec4 uPracticalDir[8];    // xyz spot axis, w cos(cone) (-2 = omni)
float practicalAttenuation(int i, vec3 P, out vec3 L) {
    vec3 d = uPracticalPos[i].xyz - P;
    float dist = max(length(d), 1e-3);
    L = d / dist;
    float x = dist / uPracticalPos[i].w;
    float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
    float att = window * window / (1.0 + dist * dist);
    if (uPracticalDir[i].w > -1.5) att *= smoothstep(uPracticalDir[i].w, uPracticalDir[i].w + 0.1, dot(-L, uPracticalDir[i].xyz));
    return att;
}

struct Terms {
    vec3 amb;            // ambient irradiance (hemisphere)
    vec3 d0; vec3 s0;    // moon: diffuse irradiance, specular radiance
    vec3 d1; vec3 s1;    // beacon
    vec3 d2; vec3 s2;    // searchlight
    vec3 sa;             // specular ambient (sky reflection)
    float irr;           // Lambert irradiance used by the heat model (unchanged from v1)
};

vec3 toLinear(vec3 c) { return uHdr == 1 ? pow(c, vec3(2.2)) : c; }
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void brdf(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 F0, float rough, out vec3 diff, out vec3 spec) {
    diff = vec3(0.0);
    spec = vec3(0.0);
    float nl = max(dot(N, L), 0.0);
    if (nl <= 0.0) return;
    vec3 H = normalize(L + V);
    if (uPbr == 0) {  // Blinn-Phong: k_d (N.L) + k_s (N.H)^n
        diff = radiance * nl;
        spec = radiance * uSpecular * pow(max(dot(N, H), 0.0), uShininess);
        return;
    }
    // Cook-Torrance: D (GGX) * G (Smith-Schlick) * F (Schlick) / (4 N.V N.L)
    float nv = max(dot(N, V), 1e-3), nh = max(dot(N, H), 0.0), vh = max(dot(V, H), 0.0);
    float a = rough * rough, a2 = a * a;
    float dd = nh * nh * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * dd * dd);
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float G = (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - vh, 5.0);
    spec = radiance * nl * PI * D * G * F / (4.0 * nv * nl);  // PI: light colours are given as irradiance
    diff = radiance * nl * (1.0 - F) * (1.0 - uMetalness);
}

Terms computeLight(vec3 P, vec3 N) {
    Terms t;
    vec3 V = normalize(uCamPos - P);
    vec3 albedo = toLinear(uBaseColor);
    vec3 F0 = mix(vec3(0.04), albedo, uMetalness);
    float rough = clamp(uRoughness, 0.05, 1.0);
    // A water film on up-facing surfaces: much smoother (sharp reflections of lamps), sides less so.
    float film = uHdr == 1 ? uWetness * clamp(N.y * 1.4 + 0.1, 0.0, 1.0) : 0.0;
    if (uSurface == 5) film *= 0.25;   // needles and leaves shed water: only a slight sheen
    rough = mix(rough, max(rough * 0.25, 0.07), film);
    if (uHdr == 1) {
        t.amb = mix(uGroundBounce, uSkyZenith, 0.5 + 0.5 * N.y);
        vec3 R = reflect(-V, N);
        float nv = max(dot(N, V), 0.0);
        vec3 Fr = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - nv, 5.0);
        t.sa = mix(uGroundBounce, uSkyZenith * 1.6, smoothstep(-0.2, 0.6, R.y)) * Fr * (1.0 - 0.8 * rough);
    } else {
        t.amb = uAmbient;
        t.sa = vec3(0.0);
    }

    vec3 L = normalize(-uMoonDir);
    brdf(N, V, L, uMoonColor, F0, rough, t.d0, t.s0);
    t.irr = luma(uMoonColor * max(dot(N, L), 0.0));

    vec3 d = uBeaconPos - P;
    float dist = max(length(d), 1e-4);
    L = d / dist;
    vec3 rad = uBeaconColor / (1.0 + uBeaconAtt.x * dist + uBeaconAtt.y * dist * dist);
    brdf(N, V, L, rad, F0, rough, t.d1, t.s1);
    t.irr += luma(rad * max(dot(N, L), 0.0));

    d = uSpotPos - P;
    dist = max(length(d), 1e-4);
    L = d / dist;
    float cone = clamp((dot(-L, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    rad = uSpotColor * cone * cone / (1.0 + uSpotAtt.x * dist + uSpotAtt.y * dist * dist);
    brdf(N, V, L, rad, F0, rough, t.d2, t.s2);
    t.irr += luma(rad * max(dot(N, L), 0.0));

    for (int i = 0; i < 8; ++i) {      // practical lights join the (unshadowed) local-light terms
        if (i >= uPracticalCount) break;
        float att = practicalAttenuation(i, P, L);
        if (att <= 0.0) continue;
        vec3 dd, ss;
        brdf(N, V, L, uPracticalColor[i].rgb * att, F0, rough, dd, ss);
        t.d1 += dd;
        t.s1 += ss;
    }
    return t;
}

// Apparent temperature: intrinsic heat + a little absorbed light (capped: a long-wave IR camera
// hardly responds to visible light) + ground texture.
float surfaceHeat(vec3 P, float irradiance) {
    float h = uHeat + 0.04 * min(irradiance, 1.5);
    h += uHeatVariation * ((valueNoise(P.xz * 0.9) - 0.5) + 0.5 * (valueNoise(P.xz * 3.7) - 0.5));
    // Rain: evaporation cools passive surfaces (engines and lamps stay hot).
    if (uHeat < 0.6) h -= 0.05 * uWetness;
    return h;
}
)";

// Fragment-only: shadows, ambient occlusion, procedural surfaces and the per-view-mode output.
const char* kOutput = R"(
uniform float uFogDensity; uniform float uFogged; uniform float uTime; uniform float uGrain;
uniform vec2 uResolution;  uniform vec2 uNearFar;  uniform float uWorldRadius;
uniform vec3 uFogColor;    uniform int uVolumetric; uniform float uEmissiveScale;
uniform int uShadows;                                  // 0 off, 1 moon, 2 moon + searchlight
uniform sampler2DArrayShadow uMoonShadow;
uniform mat4 uMoonVP0, uMoonVP1;
uniform vec2 uMoonTexel;   uniform float uCascadeSplit;
uniform sampler2DShadow uSpotShadow;
uniform mat4 uSpotVP;
uniform sampler2D uAO;     uniform int uUseAO;
uniform int uPcfTaps;      // 8 (high) or 16 (ultra)
uniform sampler2D uHeatTrail;                          // rover footprints (scene/HeatTrail)
uniform float uTrailExtent; uniform float uTrailHeat;
float trailHeat(vec3 P) { return uTrailHeat * texture(uHeatTrail, P.xz / (2.0 * uTrailExtent) + 0.5).r; }
uniform float uOpacity;    // translucent parts (rotor blur discs)
// Surface buffer (second render target): world normal and reflectivity, read by the screen-space reflection
// pass. Every shader that includes this block writes it (finalColor sets a neutral default).
layout(location = 1) out vec4 FragSurface;
vec3 gReflectNormal = vec3(0.0, 1.0, 0.0);   // set by the surface shading below
float gReflectivity = 0.0;
uniform vec3 uPartCentre;  uniform float uPartRadius;

const vec2 kPoisson[16] = vec2[](
    vec2(-0.326, -0.406), vec2(0.962, -0.195), vec2(-0.203, 0.621), vec2(0.473, -0.480),
    vec2(-0.840, -0.074), vec2(0.519, 0.767), vec2(0.185, -0.893), vec2(-0.696, 0.457),
    vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598),
    vec2(0.100, 0.330), vec2(-0.480, 0.150), vec2(0.300, -0.200), vec2(-0.100, -0.300));

mat2 pixelRotation() {
    float a = 6.2831853 * hash21(gl_FragCoord.xy);
    float c = cos(a), s = sin(a);
    return mat2(c, s, -s, c);
}

float cascadeShadow(int layer, mat4 vp, vec3 P, vec3 N, float texel) {
    vec3 L = normalize(-uMoonDir);
    float slope = 1.0 - max(dot(N, L), 0.0);
    vec4 lp = vp * vec4(P + N * texel * (1.0 + 2.0 * slope), 1.0);   // normal-offset bias
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    vec2 size = vec2(textureSize(uMoonShadow, 0).xy);
    float radius = layer == 0 ? 1.6 : 1.2;
    mat2 rot = pixelRotation();
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        if (i >= uPcfTaps) break;
        vec2 o = rot * kPoisson[i] * radius / size;
        sum += texture(uMoonShadow, vec4(p.xy + o, float(layer), p.z - 0.0004));
    }
    return sum / float(uPcfTaps);
}

float moonShadow(vec3 P, vec3 N) {
    if (uShadows == 0) return 1.0;
    float dist = length(P - uCamPos);
    if (dist < uCascadeSplit) {
        float s0 = cascadeShadow(0, uMoonVP0, P, N, uMoonTexel.x);
        float blend = smoothstep(uCascadeSplit * 0.8, uCascadeSplit, dist);
        return blend > 0.0 ? mix(s0, cascadeShadow(1, uMoonVP1, P, N, uMoonTexel.y), blend) : s0;
    }
    return cascadeShadow(1, uMoonVP1, P, N, uMoonTexel.y);
}

float spotShadow(vec3 P, vec3 N) {
    if (uShadows < 2) return 1.0;
    float dist = length(uSpotPos - P);
    vec4 lp = uSpotVP * vec4(P + N * 0.004 * dist, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;
    vec2 size = vec2(textureSize(uSpotShadow, 0));
    mat2 rot = pixelRotation();
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        if (i >= uPcfTaps) break;
        sum += texture(uSpotShadow, vec3(p.xy + rot * kPoisson[i] * 1.5 / size, p.z - 0.00015));
    }
    return sum / float(uPcfTaps);
}

float ambientOcclusion() {
    if (uUseAO == 0) return 1.0;
    return texture(uAO, gl_FragCoord.xy / uResolution).r;
}

// Procedural surface detail (no image textures): albedo variation from value noise.
vec3 surfaceAlbedo(vec3 P, vec3 N, vec3 base) {
    if (uHdr == 0 || uSurface == 0) return base;
    vec2 side = abs(N.y) > 0.7 ? P.xz : (abs(N.x) > abs(N.z) ? P.zy : P.xy);   // triplanar projection
    float n1 = fbm(side * 2.3), n2 = valueNoise(side * 11.0);
    if (uSurface == 1) {          // concrete: blotches, vertical rain streaks, darker near the ground
        float streak = valueNoise(vec2((P.x + P.z) * 6.0, P.y * 0.6));
        float grime = 1.0 - 0.35 * smoothstep(0.45, 0.0, P.y) * step(abs(N.y), 0.7);
        return base * (0.72 + 0.45 * n1) * (0.85 + 0.2 * n2) * mix(0.75, 1.0, streak) * grime;
    }
    if (uSurface == 2) {          // ground: dry grass / dirt / gravel blend, darker in hollows
        float patchy = fbm(P.xz * 0.35);
        vec3 grass = vec3(0.15, 0.19, 0.09), dirt = vec3(0.20, 0.18, 0.15), gravel = vec3(0.29, 0.29, 0.28);
        vec3 c = mix(dirt, grass, smoothstep(0.35, 0.6, patchy));
        c = mix(c, gravel, smoothstep(0.62, 0.72, fbm(P.xz * 1.7 + 3.1)) * 0.6);
        c *= 0.8 + 0.4 * n2;
        return c * (base / 0.35);
    }
    if (uSurface == 3) return base * (0.9 + 0.12 * valueNoise(vec2(dot(P, vec3(1.0)) * 90.0, 0.0)));  // brushed
    if (uSurface == 4) {          // paint: chips showing bare metal, dust towards the bottom
        float chip = smoothstep(0.78, 0.82, fbm(side * 9.0));
        vec3 c = mix(base, vec3(0.30, 0.30, 0.31), chip);
        return c * (0.85 + 0.25 * n1);
    }
    if (uSurface == 5) {          // foliage: hue variation between clumps, leaf clusters with dark gaps
        float hue = fbm(P.xz * 0.8 + P.y);
        vec3 q = P * 9.0;
        float leaves = valueNoise(q.xy + q.z * 0.7) * valueNoise(q.zy * 1.3 - q.x * 0.4);
        float gaps = smoothstep(0.05, 0.3, leaves);                 // deep holes between leaf clusters
        // 0.6: the solid crown is the shaded inner volume behind the instanced foliage cards (Foliage.cpp)
        return 0.6 * base * mix(vec3(0.75, 0.85, 0.7), vec3(1.25, 1.15, 0.75), hue) * (0.35 + 0.9 * gaps) * (0.8 + 0.4 * n2);
    }
    if (uSurface == 6) return base * (0.6 + 0.6 * valueNoise(vec2(atan(N.z, N.x) * 8.0, P.y * 1.5)) * (0.7 + 0.3 * n2));
    if (uSurface == 7) {          // rock: large-scale tone + lichen patches
        float lichen = smoothstep(0.6, 0.7, fbm(side * 1.3 + 7.0)) * step(0.3, N.y);
        return mix(base * (0.65 + 0.6 * n1), vec3(0.20, 0.23, 0.12), lichen * 0.6);
    }
    if (uSurface == 8) {          // wood: planks
        float plank = fract(side.y * 4.0);
        return base * (0.75 + 0.3 * valueNoise(vec2(side.x * 1.5, floor(side.y * 4.0) * 7.0))) * (0.8 + 0.2 * smoothstep(0.0, 0.08, plank));
    }
    if (uSurface == 9) return base * (0.8 + 0.3 * n1) * (0.9 + 0.2 * step(0.5, fract(side.x * 30.0 + side.y * 30.0)));  // woven
    if (uSurface == 10) return base * (0.85 + 0.3 * n2);
    return base;
}


// ---- Realistic terrain surface (Surface::Ground) ------------------------------------------------------
// Material layout from the ground data map; small-scale relief (clods, pebbles, tyre ruts) as a height
// field whose normal modulates the per-vertex (Gouraud) lighting; wet areas get their own glossy specular.
uniform sampler2D uGroundMap;
uniform float uGroundExtent;
uniform int uGroundDetail;

vec2 cellular(vec2 p) {   // x = distance to the nearest feature point, y = that cell's random id
    vec2 i = floor(p), f = fract(p);
    float best = 8.0, id = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 g = vec2(float(x), float(y));
            vec2 r = g + vec2(hash21(i + g), hash21(i + g + 17.3)) - f;
            float d = dot(r, r);
            if (d < best) { best = d; id = hash21(i + g + 5.1); }
        }
    }
    return vec2(sqrt(best), id);
}

float trackCentreX(float z) { return 1.4 * sin(0.2 * (z - 3.0)); }   // same centre line as objects/Terrain.cpp

float rutMask(vec2 p, float track) {
    if (track < 0.01) return 0.0;
    float off = abs(p.x - trackCentreX(p.y));                  // metres from the centre line
    float d = abs(off - 0.58);                                 // two wheel ruts 0.58 m either side
    return (1.0 - smoothstep(0.07, 0.19, d)) * smoothstep(0.3, 0.8, track);
}

float groundHeight(vec2 p, vec4 m) {
    float clods = fbm(p * 3.0) * 0.02 + valueNoise(p * 15.0) * 0.006;
    float peb = clamp(1.0 - cellular(p * 6.5).x * 1.5, 0.0, 1.0);
    float bed = smoothstep(0.35, 0.8, m.r);
    float rut = rutMask(p, m.g);
    float tread = rut * 0.0015 * sin(p.y * 48.0 + 3.0 * sin(p.x * 5.0));
    return clods * (1.0 - 0.7 * bed) + peb * peb * 0.03 * bed - rut * 0.035 + tread;
}

vec3 terrainShade(Terms t, vec3 P, vec3 N, float sm, float ss, float ao) {
    vec2 p = P.xz;
    vec4 m = texture(uGroundMap, p / (2.0 * uGroundExtent) + 0.5);
    float wet = m.r, forest = m.b, meadow = m.a, onTrack = smoothstep(0.1, 0.9, m.g), rut = rutMask(p, m.g);

    // Detail normal from the height field (central differences, 1.2 cm)
    const float e = 0.012;
    float h0 = groundHeight(p, m);
    float hx = groundHeight(p + vec2(e, 0.0), m), hz = groundHeight(p + vec2(0.0, e), m);
    vec3 Nd = normalize(N + vec3(-(hx - h0) / e, 0.0, -(hz - h0) / e));

    // Materials (linear albedo)
    vec2 cell = cellular(p * 6.5);
    float stoneMask = smoothstep(0.35, 0.8, wet) * smoothstep(0.25, 0.6, 1.0 - cell.x * 1.5);
    vec3 soil = mix(vec3(0.050, 0.041, 0.030), vec3(0.082, 0.068, 0.047), valueNoise(p * 0.9));
    // Turf: short grass seen from above -- dark green/olive with dry patches and fine blade streaks
    vec3 meadowCol = mix(vec3(0.040, 0.060, 0.020), vec3(0.110, 0.094, 0.048), smoothstep(0.4, 0.8, fbm(p * 0.45 + 3.0)));
    float streaks = valueNoise(vec2(p.x * 55.0 + 9.0 * valueNoise(p * 3.0), p.y * 18.0));
    meadowCol *= (0.65 + 0.7 * streaks) * (0.85 + 0.3 * valueNoise(p * 6.0));
    vec3 needles = mix(vec3(0.045, 0.032, 0.020), vec3(0.085, 0.058, 0.032), valueNoise(p * 9.0) * valueNoise(p * 2.3 + 1.7));
    vec3 trackCol = mix(vec3(0.050, 0.043, 0.034), vec3(0.078, 0.068, 0.053), fbm(p * 2.0));
    trackCol += vec3(0.035) * step(0.95, hash21(floor(p * 38.0))) * (1.0 - rut);   // gravel grains
    vec3 stoneCol = mix(vec3(0.11, 0.105, 0.10), vec3(0.24, 0.23, 0.215), cell.y);
    vec3 mud = vec3(0.035, 0.030, 0.024);

    vec3 albedo = mix(soil, meadowCol, meadow);
    albedo = mix(albedo, needles, forest);
    albedo = mix(albedo, trackCol * (1.0 - 0.2 * rut), onTrack);
    albedo = mix(albedo, mix(mud, stoneCol, stoneMask), smoothstep(0.3, 0.7, wet));

    float wetness = clamp(max(wet, rut * 0.55 * smoothstep(0.4, 0.7, valueNoise(p * 0.8))), 0.0, 1.0);
    wetness = max(wetness, 0.85 * uWetness);                              // soaked by rain
    // Puddles: in the creek and the ruts when dry; in rain they grow into every hollow and rut.
    float pond = max(wet * (1.0 - stoneMask) * valueNoise(p * 1.3 + 9.0) * 1.6,
                     rut * smoothstep(0.55 - 0.35 * uWetness, 0.75, valueNoise(p * 0.8)));
    pond = max(pond, uWetness * smoothstep(0.72, 0.9, valueNoise(p * 0.55 + 4.0) + 0.2 * onTrack) * (1.0 - forest));
    float puddle = smoothstep(0.55, 0.7, pond);
    // Rain ripples on standing water: expanding rings from random drop impacts in each 12 cm cell.
    if (uRain > 0.01 && puddle > 0.01) {
        vec2 cellId = floor(p * 8.0), f = fract(p * 8.0) - 0.5;
        float phase = fract(uTime * 1.3 + hash21(cellId));
        vec2 centre = vec2(hash21(cellId + 3.1), hash21(cellId + 7.7)) * 0.6 - 0.3;
        float r = length(f - centre), ring = sin((r - phase * 0.5) * 60.0) * (1.0 - phase) * smoothstep(0.5 * phase + 0.05, 0.0, abs(r - phase * 0.5));
        Nd = normalize(Nd + vec3((f - centre).x, 0.0, (f - centre).y) / max(r, 1e-3) * ring * 0.15 * uRain * puddle);
    }
    albedo *= mix(1.0, 0.5, wetness);                                               // wet soil is darker
    albedo = mix(albedo, vec3(0.01), puddle);
    float cavity = mix(1.0, 0.55, stoneMask * (1.0 - smoothstep(0.0, 0.5, 1.0 - cell.x * 1.5))) * (1.0 - 0.25 * rut);

    // Per-vertex (Gouraud) irradiance, re-aimed by the detail normal
    vec3 Lm = normalize(-uMoonDir);
    float kMoon = clamp((max(dot(Nd, Lm), 0.0) + 0.03) / (max(dot(N, Lm), 0.0) + 0.03), 0.2, 2.5);
    vec3 Ls = normalize(uSpotPos - P);
    float kSpot = clamp((max(dot(Nd, Ls), 0.0) + 0.03) / (max(dot(N, Ls), 0.0) + 0.03), 0.2, 2.5);
    vec3 diffuse = t.amb * ao * cavity + t.d0 * sm * kMoon + t.d1 + t.d2 * ss * kSpot;

    // Specular: dry ground is almost matte; wet soil and puddles are glossy and mirror the night sky
    vec3 V = normalize(uCamPos - P);
    float rough = mix(0.9, 0.3, wetness);
    rough = mix(rough, 0.05, puddle);
    vec3 dd, sMoon, sSpot;
    brdf(Nd, V, Lm, uMoonColor, vec3(0.03), rough, dd, sMoon);
    float dist = length(uSpotPos - P);
    float cone = clamp((dot(-Ls, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
    brdf(Nd, V, Ls, uSpotColor * cone * cone / (1.0 + uSpotAtt.x * dist + uSpotAtt.y * dist * dist), vec3(0.03), rough, dd, sSpot);
    vec3 R = reflect(-V, Nd);
    vec3 sky = mix(vec3(0.020, 0.026, 0.042), vec3(0.004, 0.006, 0.013), clamp(R.y * 1.5, 0.0, 1.0));
    sky += uMoonColor * 6.0 * pow(max(dot(R, Lm), 0.0), 600.0) * sm * (1.0 - uRain);  // moon glint (not when overcast)
    float F = 0.02 + 0.98 * pow(1.0 - max(dot(Nd, V), 0.0), 5.0);
    vec3 reflection = 0.45 * sky * F * max(wetness, puddle) * (1.0 - rough) * (1.0 - 0.5 * uWetness);  // overcast: darker sky in the water
    // Wet ground mirrors the lamps: per-fragment GGX for the practical lights with the wet roughness.
    vec3 lampGlints = vec3(0.0);
    if (wetness > 0.3) {
        for (int i = 0; i < 8; ++i) {
            if (i >= uPracticalCount) break;
            vec3 Lp;
            float att = practicalAttenuation(i, P, Lp);
            vec3 dl, sl;
            brdf(Nd, V, Lp, uPracticalColor[i].rgb * att * 3.0, vec3(0.03), rough, dl, sl);
            lampGlints += sl;
        }
    }
    vec3 specular = (sMoon * sm + sSpot * ss) * mix(0.25, 1.0, wetness) + t.s1 * 0.5 + reflection + lampGlints * wetness;
    // Screen-space reflections: standing water mirrors the scene, wet soil only faintly.
    gReflectNormal = Nd;
    gReflectivity = max(puddle, 0.35 * wetness * (1.0 - rough));
    return albedo * diffuse + specular;
}

vec3 compose(Terms t, vec3 P, vec3 N, float sm, float ss, float ao) {
    vec3 albedo = surfaceAlbedo(P, N, toLinear(uBaseColor));
    // Wet porous materials (concrete, wood, bark, sand, canvas...) darken; metal, glass and paint do not.
    bool porous = uSurface == 1 || uSurface == 6 || uSurface == 7 || uSurface == 8 || uSurface == 9 || uSurface == 5;
    if (porous && uHdr == 1) albedo *= 1.0 - 0.45 * uWetness;
    vec3 diffuse = t.amb * ao * (1.0 - uMetalness) + t.d0 * sm + t.d1 + t.d2 * ss;
    vec3 specular = t.s0 * sm + t.s1 + t.s2 * ss + t.sa * ao;
    // Wet tops (water film) and polished metal reflect the scene (screen-space reflection weight).
    float film = uWetness * clamp(N.y * 1.4 + 0.1, 0.0, 1.0) * (uSurface == 5 ? 0.0 : 1.0);
    float gloss = 1.0 - clamp(uRoughness, 0.0, 1.0);
    gReflectNormal = N;
    gReflectivity = max(0.45 * film, uMetalness * gloss * gloss * 0.6);
    return albedo * diffuse + specular + albedo * uEmissive * uEmissiveScale;
}

float fogAmount(vec3 P) {
    if (uFogged < 0.5) return 0.0;
    float dist = length(P - uCamPos);
    float fog = 1.0 - exp(-pow(uFogDensity * dist, 2.0));
    float rim = smoothstep(uWorldRadius * 3.5, uWorldRadius * 5.0, length(P.xz));   // far backdrop dissolves
    return max(fog, rim);
}

vec4 finalColor(vec3 lit, float heat, vec3 P) {
    FragSurface = vec4(0.5, 1.0, 0.5, 0.0);
    float fade = fogAmount(P);
    if (uViewMode == 3) {  // Z-buffer visualisation: linearised depth, near = white
        float z = gl_FragCoord.z * 2.0 - 1.0;
        float lin = 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
        float g = 1.0 - clamp(lin / 60.0, 0.0, 1.0);
        return vec4(vec3(g * g), 1.0);
    }
    if (uViewMode == 0 || uViewMode == 4) {
        // Apparent temperature (0..1) as a scalar: the tracker reads it directly (raw heat), the thermal
        // view passes it through the sensor model (gain control, detail enhancement, palette, noise).
        float h = mix(heat, 0.08, fade);
        return vec4(h, h, h, 1.0);
    }
    if (uViewMode == 2) return vec4(lit, 1.0);
    if (uHdr == 1) {        // linear HDR radiance; tone mapping happens in the composite pass
        if (uVolumetric == 1) return vec4(lit, 1.0);
        return vec4(mix(lit, uFogColor, fade), 1.0);
    }
    vec3 c = vec3(1.0) - exp(-1.25 * lit);  // v1: exposure tone mapping in the object shader
    return vec4(mix(c, vec3(0.010, 0.013, 0.024), fade), 1.0);
}

float partAlpha(vec3 P) {
    if (uOpacity >= 1.0) return 1.0;
    // Rotor blur: denser towards the blade tips (they sweep more area), faint radial streaks.
    float r = clamp(length(P - uPartCentre) / max(uPartRadius, 1e-4), 0.0, 1.0);
    vec3 d = P - uPartCentre;
    float streak = 0.75 + 0.25 * sin(atan(d.z, d.x) * 2.0 + uTime * 40.0);
    return uOpacity * smoothstep(1.0, 0.9, r) * mix(0.35, 1.0, r) * streak;
}

vec4 shadeFragment(Terms t, vec3 P, vec3 N) {
    float heat = surfaceHeat(P, t.irr);
    if (uSurface == 2) heat += trailHeat(P);   // ground keeps the rover's warm tracks for a while
    if (uViewMode == 0 || uViewMode >= 3) return finalColor(vec3(0.0), heat, P);
    // Shadow maps are only sampled where that light actually contributes (facing it / inside the cone).
    float sm = max(max(t.d0.r, t.d0.g), t.d0.b) + max(max(t.s0.r, t.s0.g), t.s0.b) > 1e-4 ? moonShadow(P, N) : 1.0;
    float ss = max(max(t.d2.r, t.d2.g), t.d2.b) + max(max(t.s2.r, t.s2.g), t.s2.b) > 1e-5 ? spotShadow(P, N) : 1.0;
    float ao = ambientOcclusion();
    vec3 lit = (uSurface == 2 && uHdr == 1 && uGroundDetail == 1) ? terrainShade(t, P, N, sm, ss, ao)
                                                                   : compose(t, P, N, sm, ss, ao);
    vec4 c = finalColor(lit, heat, P);
    c.a = partAlpha(P);
    if (uHdr == 1) FragSurface = vec4(normalize(gReflectNormal) * 0.5 + 0.5, clamp(gReflectivity, 0.0, 1.0));
    return c;
}
)";

}  // namespace

std::string withPalette(const char* body) { return std::string("#version 330 core\n") + kPalette + body; }
std::string withLighting(const char* body) { return withPalette(kLighting) + body; }
std::string withLightingAndOutput(const char* body) { return withPalette(kLighting) + kOutput + body; }

// Terms travel from the lighting stage to the fragment stage as these varyings (qualifier q, prefix p).
#define TS_TERM_VARYINGS(q, dir, p) \
    q dir vec3 p##Amb; q dir vec3 p##D0; q dir vec3 p##S0; q dir vec3 p##D1; q dir vec3 p##S1; \
    q dir vec3 p##D2; q dir vec3 p##S2; q dir vec3 p##Sa; q dir float p##Irr;
#define TS_STR2(x) #x
#define TS_STR(x) TS_STR2(x)

// ---------------------------------------------------------------- Flat
const char* kFlatVert = R"(
layout(location = 0) in vec3 aPos;
uniform mat4 uModel, uView, uProj;
out vec3 gWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    gWorld = w.xyz;
    gl_Position = uProj * uView * w;
}
)";

// One normal per triangle, from its own edges; lighting evaluated once at the centroid.
const char* kFlatGeom = "layout(triangles) in;\nlayout(triangle_strip, max_vertices = 3) out;\nin vec3 gWorld[];\n"
    TS_STR(TS_TERM_VARYINGS(flat, out, f)) R"(
flat out vec3 fNormal;
out vec3 fWorld;
void main() {
    vec3 a = gWorld[0], b = gWorld[1], c = gWorld[2];
    vec3 cr = cross(b - a, c - a);
    vec3 N = length(cr) > 1e-12 ? normalize(cr) : vec3(0.0, 1.0, 0.0);   // degenerate (zero-area) triangle
    Terms t = computeLight((a + b + c) / 3.0, N);
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        fWorld = gWorld[i];
        fNormal = N;
        fAmb = t.amb; fD0 = t.d0; fS0 = t.s0; fD1 = t.d1; fS1 = t.s1; fD2 = t.d2; fS2 = t.s2; fSa = t.sa; fIrr = t.irr;
        EmitVertex();
    }
    EndPrimitive();
}
)";

const char* kFlatFrag = TS_STR(TS_TERM_VARYINGS(flat, in, f)) R"(
flat in vec3 fNormal;
in vec3 fWorld;
layout(location = 0) out vec4 FragColor;
void main() {
    Terms t = Terms(fAmb, fD0, fS0, fD1, fS1, fD2, fS2, fSa, fIrr);
    FragColor = shadeFragment(t, fWorld, fNormal);
}
)";

// ---------------------------------------------------------------- Gouraud
const char* kGouraudVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
)" TS_STR(TS_TERM_VARYINGS(, out, v)) R"(
out vec3 vWorld;
out vec3 vNormal;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vec3 N = normalize(uNormalMat * aNormal);
    Terms t = computeLight(w.xyz, N);    // lighting per VERTEX
    vAmb = t.amb; vD0 = t.d0; vS0 = t.s0; vD1 = t.d1; vS1 = t.s1; vD2 = t.d2; vS2 = t.s2; vSa = t.sa; vIrr = t.irr;
    vWorld = w.xyz;
    vNormal = N;
    gl_Position = uProj * uView * w;
}
)";

const char* kGouraudFrag = TS_STR(TS_TERM_VARYINGS(, in, v)) R"(
in vec3 vWorld;
in vec3 vNormal;
layout(location = 0) out vec4 FragColor;
void main() {   // lighting terms are only interpolated; shadows/AO (visibility) are looked up per fragment
    Terms t = Terms(vAmb, vD0, vS0, vD1, vS1, vD2, vS2, vSa, vIrr);
    FragColor = shadeFragment(t, vWorld, normalize(vNormal));
}
)";

// ---------------------------------------------------------------- Phong
const char* kPhongVert = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
out vec3 vWorld;
out vec3 vNormal;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = uNormalMat * aNormal;      // the NORMAL is interpolated
    gl_Position = uProj * uView * w;
}
)";

const char* kPhongFrag = R"(
in vec3 vWorld;
in vec3 vNormal;
layout(location = 0) out vec4 FragColor;
void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    FragColor = shadeFragment(computeLight(vWorld, N), vWorld, N);   // lighting per FRAGMENT
}
)";

// ---------------------------------------------------------------- Depth only (shadow maps)
const char* kDepthVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uLightVP, uModel;
void main() { gl_Position = uLightVP * uModel * vec4(aPos, 1.0); }
)";

const char* kDepthFrag = R"(#version 330 core
void main() {}
)";

// ---------------------------------------------------------------- Pre-pass: depth + view-space normal (SSAO, fog)
const char* kPrepassVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
out vec3 vNormalView;
void main() {
    vNormalView = mat3(uView) * (uNormalMat * aNormal);
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
)";

const char* kPrepassFrag = R"(#version 330 core
in vec3 vNormalView;
out vec4 Normal;
void main() {
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing) n = -n;
    Normal = vec4(n * 0.5 + 0.5, 1.0);
}
)";

// ---------------------------------------------------------------- Light cone (low quality / thermal cue)
const char* kBeamVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel, uView, uProj;
uniform mat3 uNormalMat;
out float vAxial;
out vec3 vWorld;
out vec3 vNormal;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vAxial = aPos.z + 0.5;               // 0 at the lens, 1 on the ground
    vWorld = w.xyz;
    vNormal = uNormalMat * aNormal;
    gl_Position = uProj * uView * w;
}
)";

const char* kBeamFrag = R"(#version 330 core
in float vAxial;
in vec3 vWorld;
in vec3 vNormal;
uniform vec3 uCamPos;
uniform vec3 uBeamColor;
uniform float uBeamIntensity;
layout(location = 0) out vec4 FragColor;
void main() {
    float t = clamp(vAxial, 0.0, 1.0);
    vec3 V = normalize(uCamPos - vWorld);
    float facing = abs(dot(normalize(vNormal), V));
    float a = uBeamIntensity * pow(1.0 - t, 1.4) * (0.2 + 0.8 * facing * facing) * smoothstep(0.0, 0.05, t);
    FragColor = vec4(uBeamColor * a, 1.0);   // blended additively
}
)";

// ---------------------------------------------------------------- Lines / points
const char* kLineVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
uniform int uUseVertexColor;
uniform vec3 uColor;
out vec3 vColor;
void main() {
    vColor = uUseVertexColor == 1 ? aColor : uColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kLineFrag = R"(#version 330 core
in vec3 vColor;
uniform float uAlpha;
layout(location = 0) out vec4 FragColor;
void main() { FragColor = vec4(vColor, uAlpha); }
)";

// ---------------------------------------------------------------- HUD
const char* kHudVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
out vec4 vColor;
out vec2 vUV;
void main() {
    vColor = aColor;
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kHudFrag = R"(
in vec4 vColor;
in vec2 vUV;
uniform sampler2D uTex;
uniform int uMode;   // 0 flat colour, 1 text (atlas alpha), 2 raw heat image through the thermal palette
layout(location = 0) out vec4 FragColor;
void main() {
    if (uMode == 1) {
        FragColor = vec4(vColor.rgb, vColor.a * texture(uTex, vUV).r);
    } else if (uMode == 2) {
        float h = texture(uTex, vUV).r;
        FragColor = vec4(thermalPalette((h - 0.10) / 0.80), 1.0);
    } else {
        FragColor = vColor;
    }
}
)";

}  // namespace ts::glsl
