// GLSL of the v3 post-processing chain (after the HDR scene is resolved):
//   combine (volumetric fog + screen-space reflections) -> TAA -> statistics (auto-exposure / thermal AGC)
//   -> lens flare + anamorphic streak -> depth of field -> final composite (motion blur, grading, filmic
//   curve, sRGB) or the thermal sensor model (AGC, detail enhancement, palette, sensor noise).
#include "rendering/ShaderSources.h"

namespace ts::glsl {

namespace {

// Shared helpers: depth linearisation and world position from the depth buffer.
#define TS_DEPTH_HELPERS                                                                                  \
    "uniform vec2 uNearFar;\n"                                                                            \
    "float linearDepth(float d) {\n"                                                                      \
    "    float z = d * 2.0 - 1.0;\n"                                                                      \
    "    return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));\n" \
    "}\n"

}  // namespace

// ---------------------------------------------------------------- Combine: fog + screen-space reflections
const char* kCombineFrag = TS_DEPTH_HELPERS R"(
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uFog;
uniform sampler2D uSurface;   // rgb world normal * 0.5 + 0.5, a reflectivity
uniform sampler2D uDepth;     // resolved full-resolution depth
uniform int uMode;            // 0 visible HDR, 1 thermal heat, 2 pass-through
uniform int uUseFog;
uniform int uSsr;
uniform int uSsrSteps;
uniform mat4 uViewProj, uInvViewProj;   // jittered (matches the depth buffer)
uniform vec3 uCamPos;
uniform float uTime;
// v9 M8 aerial perspective: air + regional haze between the camera and every surface (0 at night: uAerialDensity)
uniform float uAerialDensity;  // extinction at height 0, 1/m
uniform float uAerialHeight;   // scale height H of the exponential density, m
uniform float uAerialMie;      // strength of the forward (Mie) glow around the sun
uniform vec3 uSunColor;        // sun radiance (DayCycle Atmosphere::sunColor)
uniform vec3 uHazeRef;         // mean horizon radiance of the sky model (Atmosphere::fogColor by day)
layout(location = 0) out vec4 Color;

// Optical depth of sigma(h) = s0 exp(-h / H) along the segment camera -> P is closed-form:
//     tau = s0 exp(-h_cam / H) |P - cam| (1 - exp(-k)) / k,   k = (h_P - h_cam) / H
// transmittance T = exp(-tau); the in-scattered light is the sky model's radiance towards the horizon in that direction
// (the light the same air scatters into the sky) plus a Henyey-Greenstein forward lobe around the sun:
//     L = L_surface T + (L_sky + sun HG(cos, 0.76) k_mie) (1 - T)
vec3 aerialPerspective(vec3 c, vec3 P) {
    vec3 ray = P - uCamPos;
    float dist = length(ray);
    if (uAerialDensity <= 0.0 || dist < 1e-3) return c;
    vec3 dir = ray / dist;
    float k = ray.y / uAerialHeight;
    float tau = uAerialDensity * exp(-max(uCamPos.y, 0.0) / uAerialHeight) * dist * (abs(k) > 1e-3 ? (1.0 - exp(-k)) / k : 1.0);
    float T = exp(-tau);
    vec3 sky = skyModelRadiance(vec3(dir.x, max(dir.y, 0.04), dir.z));
    // The Perez fit over-brightens the horizon right below a low sun: cap the in-scatter at 1.6x the mean horizon.
    const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
    sky *= min(1.0, 1.6 * dot(uHazeRef, lw) / max(dot(sky, lw), 1e-4));
    float cs = dot(dir, uSkySun), g = 0.76;
    float hg = (1.0 - g * g) / (12.566371 * pow(1.0 + g * g - 2.0 * g * cs, 1.5));
    return c * T + (sky + uSunColor * hg * uAerialMie) * (1.0 - T);
}

vec3 worldAt(vec2 uv, float d) {
    vec4 p = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

// Screen-space reflection: march the reflected ray in world space with growing steps, project every sample
// and compare its depth with the depth buffer; a crossing within a thickness is refined by bisection.
vec3 reflection(vec2 uv, float d, vec4 surf) {
    vec3 P = worldAt(uv, d);
    vec3 N = normalize(surf.rgb * 2.0 - 1.0);
    vec3 V = normalize(P - uCamPos);
    vec3 R = reflect(V, N);
    if (R.y < -0.2) return vec3(0.0);
    float jitter = hash21(gl_FragCoord.xy + fract(uTime * 3.7) * 61.0);
    float t = 0.04 * (1.0 + jitter), prevT = 0.0;
    float dist0 = length(P - uCamPos);
    for (int i = 0; i < 96; ++i) {
        if (i >= uSsrSteps) break;
        vec3 Q = P + R * t;
        vec4 c = uViewProj * vec4(Q, 1.0);
        if (c.w <= 0.05) break;
        vec2 q = c.xy / c.w * 0.5 + 0.5;
        if (any(lessThan(q, vec2(0.0))) || any(greaterThan(q, vec2(1.0)))) break;
        float sceneZ = linearDepth(texture(uDepth, q).r);
        float rayZ = c.w;
        float thickness = 0.15 + 0.04 * t + 0.01 * dist0;
        if (rayZ > sceneZ + 0.01 && rayZ - sceneZ < thickness && sceneZ < uNearFar.y * 0.9) {
            // Bisection between the last miss and this hit
            float a = prevT, b = t;
            for (int k = 0; k < 6; ++k) {
                float m = 0.5 * (a + b);
                vec4 cm = uViewProj * vec4(P + R * m, 1.0);
                vec2 qm = cm.xy / cm.w * 0.5 + 0.5;
                if (cm.w > linearDepth(texture(uDepth, qm).r)) b = m; else a = m;
            }
            vec4 cb = uViewProj * vec4(P + R * b, 1.0);
            q = cb.xy / cb.w * 0.5 + 0.5;
            vec2 edge = smoothstep(vec2(0.0), vec2(0.07), q) * smoothstep(vec2(1.0), vec2(0.93), q);
            float fade = edge.x * edge.y * (1.0 - smoothstep(0.6, 1.0, float(i) / float(uSsrSteps)));
            float F = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, -V), 0.0, 1.0), 5.0);
            return texture(uScene, q).rgb * fade * mix(0.35, 1.0, F);
        }
        prevT = t;
        t = t * 1.09 + 0.03;
    }
    return vec3(0.0);
}

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    if (uMode != 0) { Color = vec4(c, 1.0); return; }
    if (uSsr == 1) {
        vec4 surf = texture(uSurface, vUV);
        float d = texture(uDepth, vUV).r;
        if (surf.a > 0.02 && d < 1.0) c += reflection(vUV, d, surf) * surf.a;
    }
    if (uUseFog == 1) {
        vec4 f = texture(uFog, vUV);
        c = c * f.a + f.rgb;
    }
    float dc = texture(uDepth, vUV).r;
    if (dc < 1.0) c = aerialPerspective(c, worldAt(vUV, dc));   // the sky already contains its own air
    Color = vec4(c, 1.0);
}
)";

// ---------------------------------------------------------------- Temporal anti-aliasing
// History reprojected with the depth buffer (camera motion), sampled with a Catmull-Rom filter, clipped to
// the current 3x3 neighbourhood's colour distribution in YCoCg (variance clipping), blended with luminance
// weights (no bright-pixel flicker).
const char* kTaaFrag = TS_DEPTH_HELPERS R"(
in vec2 vUV;
uniform sampler2D uCurrent;
uniform sampler2D uHistory;
uniform sampler2D uDepth;
uniform mat4 uReproject;     // previous (unjittered) view-projection * inverse(current jittered view-projection)
uniform vec2 uJitterUV;      // this frame's jitter: a static point is seen at vUV + uJitterUV
uniform float uFeedback;     // history weight (0.9)
uniform int uReset;
uniform int uHeat;           // 1: thermal heat image (scalar)
layout(location = 0) out vec4 Color;

vec3 toYCoCg(vec3 c) { return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25))); }
vec3 fromYCoCg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
vec3 tonemapW(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }       // compress HDR before filtering
vec3 untonemapW(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }

vec3 catmullRom(sampler2D tex, vec2 uv) {
    vec2 size = vec2(textureSize(tex, 0));
    vec2 pos = uv * size;
    vec2 centre = floor(pos - 0.5) + 0.5;
    vec2 f = pos - centre;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 tc12 = (centre + w2 / w12) / size;
    vec2 tc0 = (centre - 1.0) / size, tc3 = (centre + 2.0) / size;
    vec3 r = texture(tex, vec2(tc12.x, tc0.y)).rgb * (w12.x * w0.y) +
             texture(tex, vec2(tc0.x, tc12.y)).rgb * (w0.x * w12.y) +
             texture(tex, tc12).rgb * (w12.x * w12.y) +
             texture(tex, vec2(tc3.x, tc12.y)).rgb * (w3.x * w12.y) +
             texture(tex, vec2(tc12.x, tc3.y)).rgb * (w12.x * w3.y);
    float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / wsum, vec3(0.0));
}

void main() {
    vec3 cur = texture(uCurrent, vUV).rgb;
    if (uReset == 1) { Color = vec4(cur, 1.0); return; }
    vec2 texel = 1.0 / vec2(textureSize(uCurrent, 0));
    // Neighbourhood statistics + closest depth (dilated velocity keeps object edges from ghosting)
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    float closest = 1.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = vUV + vec2(x, y) * texel;
            vec3 s = toYCoCg(tonemapW(texture(uCurrent, uv).rgb));
            m1 += s;
            m2 += s * s;
            float d = texture(uDepth, uv).r;
            closest = min(closest, d);
        }
    }
    vec3 mean = m1 / 9.0;
    vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    vec4 prev = uReproject * vec4(vUV * 2.0 - 1.0, closest * 2.0 - 1.0, 1.0);
    vec2 prevUV = prev.xy / prev.w * 0.5 + 0.5 - uJitterUV;
    if (any(lessThan(prevUV, vec2(0.0))) || any(greaterThan(prevUV, vec2(1.0)))) { Color = vec4(cur, 1.0); return; }
    vec3 hist = toYCoCg(tonemapW(catmullRom(uHistory, prevUV)));
    // Clip the history towards the neighbourhood mean (variance box, gamma 1.1)
    vec3 boxMin = mean - 1.1 * sigma, boxMax = mean + 1.1 * sigma;
    vec3 centre = 0.5 * (boxMax + boxMin), extent = 0.5 * (boxMax - boxMin) + 1e-5;
    vec3 v = hist - centre;
    vec3 a = abs(v / extent);
    float ma = max(a.x, max(a.y, a.z));
    if (ma > 1.0) hist = centre + v / ma;
    vec3 c = toYCoCg(tonemapW(cur));
    // Faster response when the camera moves a lot (less smearing), slower when still (more AA)
    float motion = length((prevUV - vUV) * vec2(textureSize(uCurrent, 0)));
    float feedback = mix(uFeedback, 0.75, clamp(motion / 30.0, 0.0, 1.0));
    vec3 outc = max(untonemapW(max(fromYCoCg(mix(c, hist, feedback)), vec3(0.0))), vec3(0.0));
    if (any(isnan(outc)) || any(isinf(outc))) outc = cur;   // never let one bad pixel into the history
    Color = vec4(outc, 1.0);
}
)";

// ---------------------------------------------------------------- Statistics reduction
// uMode 0: first pass from the image into 64x64 -- per texel: min, max, mean, mean of squares of
//          log2 luminance (visible) or heat (thermal), from a 4x4 grid of bilinear taps.
// uMode 1: 4x4 texel reduction of such statistics.
const char* kReduceFrag = R"(#version 330 core
uniform sampler2D uSrc;
uniform int uMode;
uniform int uHeat;
uniform float uLetterbox;
layout(location = 0) out vec4 Stats;
void main() {
    ivec2 o = ivec2(gl_FragCoord.xy);
    float mn = 1e9, mx = -1e9, sum = 0.0, sum2 = 0.0, n = 0.0;
    if (uMode == 0) {
        vec2 cell = 1.0 / vec2(64.0);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                vec2 uv = (vec2(o) + (vec2(x, y) + 0.5) / 4.0) * cell;
                uv.y = mix(uLetterbox, 1.0 - uLetterbox, uv.y);        // meter the picture, not the black bars
                vec3 c = texture(uSrc, uv).rgb;
                if (any(isnan(c)) || any(isinf(c))) continue;
                float v = uHeat == 1 ? c.r : log2(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 0.0) + 1e-5);
                // Centre-weighted metering for exposure (the frame edges count half)
                float w = uHeat == 1 ? 1.0 : mix(0.5, 1.0, smoothstep(0.7, 0.2, length(uv - 0.5)));
                mn = min(mn, v); mx = max(mx, v); sum += v * w; sum2 += v * v * w; n += w;
            }
        }
        if (n == 0.0) { Stats = vec4(0.0); return; }
        Stats = vec4(mn, mx, sum / n, sum2 / n);
        return;
    }
    vec4 acc = vec4(1e9, -1e9, 0.0, 0.0);
    ivec2 size = textureSize(uSrc, 0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            ivec2 p = o * 4 + ivec2(x, y);
            if (p.x >= size.x || p.y >= size.y) continue;
            vec4 s = texelFetch(uSrc, p, 0);
            acc.x = min(acc.x, s.x); acc.y = max(acc.y, s.y); acc.z += s.z; acc.w += s.w; n += 1.0;
        }
    }
    Stats = vec4(acc.xy, acc.zw / max(n, 1.0));
}
)";

// Temporal adaptation of the 1x1 statistics (eye / sensor gain adapting over ~a second).
const char* kAdaptFrag = R"(#version 330 core
uniform sampler2D uCurrent;
uniform sampler2D uPrevious;
uniform float uRate;       // 1 - exp(-dt * speed); 1 = reset
layout(location = 0) out vec4 Stats;
void main() {
    vec4 c = texelFetch(uCurrent, ivec2(0), 0);
    vec4 p = texelFetch(uPrevious, ivec2(0), 0);
    Stats = mix(p, c, clamp(uRate, 0.0, 1.0));
}
)";

// ---------------------------------------------------------------- Lens flare (Chapman's screen-space ghosts)
const char* kFlareFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;      // blurred bright image (bloom chain)
uniform float uThreshold;
layout(location = 0) out vec4 Color;
vec3 bright(vec2 uv) { return max(texture(uSrc, uv).rgb - uThreshold, vec3(0.0)); }
vec3 chroma(vec2 uv, vec2 dir, float k) {    // lens dispersion: red and blue ghosts slightly apart
    return vec3(bright(uv + dir * k).r, bright(uv).g, bright(uv - dir * k).b);
}
void main() {
    vec2 uv = vec2(1.0) - vUV;                            // ghosts appear mirrored through the centre
    vec2 ghostVec = (vec2(0.5) - uv) * 0.34;
    vec2 dir = normalize(ghostVec + 1e-6);
    vec3 result = vec3(0.0);
    const vec3 tints[5] = vec3[](vec3(1.0, 0.8, 0.6), vec3(0.6, 0.8, 1.0), vec3(0.8, 1.0, 0.7),
                                 vec3(1.0, 0.6, 0.9), vec3(0.7, 0.9, 1.0));
    for (int i = 0; i < 5; ++i) {
        vec2 o = fract(uv + ghostVec * float(i));
        float w = pow(max(1.0 - length(vec2(0.5) - o) / 0.7071, 0.0), 10.0);
        result += chroma(o, dir, 0.004 * float(i + 1)) * w * tints[i];
    }
    vec2 haloVec = dir * 0.45;                            // halo ring around the centre
    float hw = pow(max(1.0 - length(vec2(0.5) - fract(uv + haloVec)) / 0.7071, 0.0), 6.0);
    result += chroma(uv + haloVec, dir, 0.01) * hw * vec3(0.9, 0.95, 1.0) * 0.6;
    Color = vec4(result, 1.0);
}
)";

// Anamorphic streak: a wide horizontal blur of the brightest light sources.
const char* kStreakFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform float uThreshold;
layout(location = 0) out vec4 Color;
void main() {
    float texel = 1.0 / float(textureSize(uSrc, 0).x);
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    for (int i = -24; i <= 24; ++i) {
        float w = exp(-abs(float(i)) / 7.0);
        vec3 c = max(texture(uSrc, vUV + vec2(float(i) * texel * 2.5, 0.0)).rgb - uThreshold, vec3(0.0));
        sum += c * w;
        wsum += w;
    }
    Color = vec4(sum / wsum * vec3(0.55, 0.75, 1.25), 1.0);
}
)";

// ---------------------------------------------------------------- Depth of field (half resolution)
// Scatter-as-gather bokeh: 64 golden-angle taps over the largest circle of confusion; a tap contributes if
// its own blur circle reaches this pixel (background taps are limited to this pixel's CoC so a sharp
// foreground does not bleed out).
const char* kDofFrag = TS_DEPTH_HELPERS R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform sampler2D uDepth;
uniform float uFocus;        // metres (< 0: autofocus on the centre of the frame)
uniform float uAperture;     // CoC (pixels at this resolution) of an object at infinity
uniform float uMaxCoc;       // pixels at this resolution
layout(location = 0) out vec4 Color;

float focusDistance() {
    if (uFocus > 0.0) return uFocus;
    float f = 0.0;
    for (int i = 0; i < 5; ++i) f += linearDepth(texture(uDepth, vec2(0.5) + 0.02 * vec2(i % 3 - 1, i / 3)).r);
    return f / 5.0;
}
float coc(float z, float focus) { return clamp(uAperture * abs(z - focus) / max(z, 1e-3), 0.0, uMaxCoc); }

void main() {
    float focus = focusDistance();
    vec2 texel = 1.0 / vec2(textureSize(uSrc, 0)) * 2.0;    // uSrc is full resolution; offsets in half-res pixels
    float zc = linearDepth(texture(uDepth, vUV).r);
    float cc = coc(zc, focus);
    vec3 sum = texture(uSrc, vUV).rgb;
    float wsum = 1.0;
    const float golden = 2.39996323;
    for (int i = 1; i < 64; ++i) {
        float r = sqrt(float(i) / 63.0) * uMaxCoc;
        float a = float(i) * golden;
        vec2 uv = vUV + vec2(cos(a), sin(a)) * r * texel;
        float z = linearDepth(texture(uDepth, uv).r);
        float ct = coc(z, focus);
        if (z > zc) ct = min(ct, cc);                        // background behind a sharp pixel stays behind
        float w = clamp(ct - r + 1.0, 0.0, 1.0);
        vec3 s = texture(uSrc, uv).rgb;
        sum += s * w;
        wsum += w;
    }
    Color = vec4(sum / wsum, cc / uMaxCoc);
}
)";

const char* kDofCombineFrag = TS_DEPTH_HELPERS R"(
in vec2 vUV;
uniform sampler2D uSharp;
uniform sampler2D uBlur;
uniform sampler2D uDepth;
uniform float uFocus;
uniform float uAperture;     // full-resolution pixels
layout(location = 0) out vec4 Color;
void main() {
    float focus = uFocus;
    if (focus <= 0.0) {
        focus = 0.0;
        for (int i = 0; i < 5; ++i) focus += linearDepth(texture(uDepth, vec2(0.5) + 0.02 * vec2(i % 3 - 1, i / 3)).r);
        focus /= 5.0;
    }
    float z = linearDepth(texture(uDepth, vUV).r);
    float c = uAperture * abs(z - focus) / max(z, 1e-3);
    vec4 blur = texture(uBlur, vUV);
    float k = clamp(max(c, blur.a * uAperture * 0.5) - 0.7, 0.0, 1.0);
    Color = vec4(mix(texture(uSharp, vUV).rgb, blur.rgb, k), 1.0);
}
)";

// ---------------------------------------------------------------- Final composite
const char* kCompositeFrag = TS_DEPTH_HELPERS R"(
in vec2 vUV;
uniform sampler2D uScene;     // TAA / DOF output (HDR radiance, or heat in the thermal view)
uniform sampler2D uBloom;
uniform sampler2D uFlare;
uniform sampler2D uStreak;
uniform sampler2D uStats;     // 1x1 adapted statistics
uniform sampler2D uDepth;
uniform int uMode;            // 0 cinematic HDR (visible), 1 thermal sensor, 2 pass-through
uniform float uBloomStrength;
uniform float uFlareStrength;
uniform float uExposure;
uniform int uAutoExposure;
uniform float uKeyLog2;       // log2 luminance the auto-exposure maps to the base exposure
uniform float uEvRange;       // +- stops the auto-exposure may move
uniform float uPurkinje;      // v6: scotopic shift weight (1 night, 0 day)
uniform float uDayGrade;      // v6: weight of the neutral daylight grade
uniform float uDayContrast;   // v9 M8: log-space day contrast around mid grey (1.10 with ACES; more with AgX, whose
                              // base-contrast sigmoid is ~2/3 as steep at mid grey)
uniform float uDaySaturation; // extra day saturation (0.10 ACES)
uniform float uTime;
uniform float uGrain;
uniform float uVignette;
uniform float uFade;          // dip to black between cinematic shots
uniform float uLetterbox;     // fraction of the height covered by each black bar
uniform float uMotionBlur;    // shutter fraction (0 = off)
uniform mat4 uReproject;      // previous view-projection * inverse(current view-projection)
// Thermal sensor
uniform int uToneMap;         // v9 M8: 0 AgX (default), 1 ACES (RenderSettings::toneMap, TS_TONEMAP=aces)
uniform float uAgxWeight;     // weight of AgX against the ACES curve (see Renderer::endFrame)
uniform float uAgxExposure, uAcesExposure;
uniform float uShutter;       // NUC shutter closed (0..1)
uniform float uSensorNoise;
layout(location = 0) out vec4 FragColor;

// ACES filmic (Stephen Hill's fit of the RRT + ODT, sRGB primaries)
vec3 acesFitted(vec3 v) {
    const mat3 inM = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 outM = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    v = inM * v;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(outM * (a / b), 0.0, 1.0);
}
vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

vec3 sceneSample(vec2 uv, vec2 ca) {   // lateral chromatic aberration grows towards the frame edge
    return vec3(texture(uScene, uv + ca).r, texture(uScene, uv).g, texture(uScene, uv - ca).b);
}

vec4 finish(vec3 c) {
    if (vUV.y < uLetterbox || vUV.y > 1.0 - uLetterbox) return vec4(0.0, 0.0, 0.0, 1.0);
    return vec4(c * uFade, 1.0);
}

vec3 thermalSensor(vec2 uv) {
    // Heat shimmer: warm air above hot parts refracts the long-wave image a little
    float hot = smoothstep(0.45, 0.9, texture(uScene, uv - vec2(0.0, 0.01)).r);
    uv += (vec2(valueNoise(uv * 60.0 + vec2(0.0, uTime * 4.0)), valueNoise(uv * 60.0 - uTime * 3.0)) - 0.5) * 0.003 * hot;
    vec2 px = 1.0 / vec2(textureSize(uScene, 0));
    // Optics (germanium lens MTF): slight blur
    float h = texture(uScene, uv).r * 0.5;
    h += (texture(uScene, uv + vec2(px.x, 0.0)).r + texture(uScene, uv - vec2(px.x, 0.0)).r +
          texture(uScene, uv + vec2(0.0, px.y)).r + texture(uScene, uv - vec2(0.0, px.y)).r) * 0.125;
    // Digital detail enhancement: boost local contrast against a wide local mean
    float local = 0.0;
    for (int i = 0; i < 12; ++i) {
        float a = float(i) * 0.5236;
        local += texture(uScene, uv + vec2(cos(a), sin(a)) * px * 9.0).r;
    }
    local /= 12.0;
    h = local + (h - local) * 1.5;
    // Automatic gain control from the adapted frame statistics: the background sits in the lower part of
    // the range (mean - 1.5 sigma), hot targets saturate at the top; a minimum span keeps small natural
    // temperature differences from being stretched into noise (like a plateau-equalised FLIR core).
    vec4 st = texelFetch(uStats, ivec2(0), 0);
    float sd = sqrt(max(st.w - st.z * st.z, 0.0));
    float lo = max(st.x, st.z - 1.5 * sd);
    float hi = min(st.y, st.z + 4.5 * sd);
    float span = max(hi - lo, 0.30);
    float t = (h - lo) / span;
    // Sensor: temporal noise, fixed-pattern column/row offsets, a few dead pixels, narcissus vignette
    vec2 fc = floor(gl_FragCoord.xy / max(1.0, float(textureSize(uScene, 0).y) / 480.0));   // ~480-line sensor
    t += (hash21(fc + fract(uTime * 7.31) * 311.0) - 0.5) * uSensorNoise;
    t += (hash21(vec2(fc.x, 3.0)) - 0.5) * 0.018 + (hash21(vec2(7.0, fc.y)) - 0.5) * 0.008;
    vec2 q = vUV - 0.5;
    t -= 0.10 * dot(q, q);
    t = mix(t, 0.45, uShutter);
    if (hash21(fc * 1.37 + 0.5) > 0.99996) t = 1.0;
    return thermalPalette(clamp(t, 0.0, 1.0));
}

void main() {
    if (uMode == 2) { FragColor = finish(texture(uScene, vUV).rgb); return; }
    vec2 q = vUV - 0.5;
    float grain = hash21(gl_FragCoord.xy + fract(uTime * 13.7) * 517.0) - 0.5;
    if (uMode == 1) { FragColor = finish(thermalSensor(vUV)); return; }

    vec2 ca = q * dot(q, q) * 0.012;
    vec3 c;
    if (uMotionBlur > 0.0) {   // camera motion blur: 12 taps along the per-pixel screen velocity
        float d = texture(uDepth, vUV).r;
        vec4 prev = uReproject * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
        vec2 vel = (vUV - (prev.xy / prev.w * 0.5 + 0.5)) * uMotionBlur;
        float len = length(vel);
        if (len > 0.06) vel *= 0.06 / len;
        c = vec3(0.0);
        float jitter = hash21(gl_FragCoord.xy + uTime);
        for (int i = 0; i < 12; ++i) {
            float t = (float(i) + jitter) / 12.0 - 0.5;
            c += sceneSample(vUV + vel * t, ca);
        }
        c /= 12.0;
    } else {
        c = sceneSample(vUV, ca);
    }
    c += texture(uBloom, vUV).rgb * uBloomStrength;
    c += (texture(uFlare, vUV).rgb + texture(uStreak, vUV).rgb) * uFlareStrength;

    float exposure = uExposure;
    if (uAutoExposure == 1) {
        float meanLog = texelFetch(uStats, ivec2(0), 0).z;
        exposure *= exp2(clamp(uKeyLog2 - meanLog, -uEvRange, uEvRange));
    }
    c *= exposure;
    // Night vision (Purkinje shift): in dim light the eye's rods take over -- less colour, bluer.
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float scotopic = (1.0 - smoothstep(0.004, 0.08, l)) * uPurkinje;
    c = mix(c, l * vec3(0.62, 0.78, 1.15), scotopic * 0.55);
    // Grading: cool moonlit shadows, warm practical lights, a touch of saturation in the mids
    vec3 graded = mix(c * vec3(0.90, 0.98, 1.12), c * vec3(1.10, 1.0, 0.86), smoothstep(0.08, 1.6, l));
    // Daylight: almost neutral, slightly cool shade and warm sunlit highlights
    if (uDayGrade > 0.0)
        graded = mix(graded, c * mix(vec3(0.97, 1.0, 1.04), vec3(1.04, 1.0, 0.95), smoothstep(0.05, 1.2, l)), uDayGrade);
    c = graded;
    // v8 daylight: a little more contrast around mid grey (log space) and richer colour, so a clear day reads crisp
    // instead of milky; the night grade is untouched (uDayGrade = 0).
    if (uDayGrade > 0.0) {
        vec3 lc = log2(max(c, vec3(1e-6)));
        const float mid = -2.4739;   // log2(0.18)
        c = mix(c, exp2(mid + (lc - mid) * uDayContrast), uDayGrade);
    }
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = max(mix(vec3(lum), c, 1.12 + uDaySaturation * uDayGrade), vec3(0.0));
    // Display transform: AgX (log encoding + sigmoid; saturated highlights desaturate towards white instead of clipping
    // one channel) or the v3-v8 ACES fit.
    vec3 aces = acesFitted(c * uAcesExposure);
    c = uToneMap == 1 || uAgxWeight <= 0.0 ? aces : mix(aces, agxDisplay(c * uAgxExposure), uAgxWeight);
    c = linearToSrgb(c);
    c *= 1.0 - uVignette * dot(q, q) * 1.5;
    c += grain * uGrain * (1.0 - 0.6 * c);                  // film grain, stronger in the shadows
    FragColor = finish(clamp(c, 0.0, 1.0));
}
)";

}  // namespace ts::glsl
