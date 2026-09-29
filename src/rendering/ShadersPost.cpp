// GLSL for the sky and the post-processing chain:
// SSAO -> bilateral blur, volumetric fog + light shafts, bloom (prefilter, down, up), composite (tone mapping).
#include "rendering/ShaderSources.h"

namespace ts::glsl {

// Full-screen triangle; vUV in [0, 1].
const char* kFullscreenVert = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// ---------------------------------------------------------------- Sky
const char* kSkyVert = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
)";

const char* kSkyFrag = R"(
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform int uViewMode;
uniform int uHdr;
uniform float uTime;
uniform vec2 uResolution;
uniform vec3 uMoonDir;       // direction the moonlight travels
uniform vec3 uFogColor;
uniform float uRain;          // overcast: clouds close, stars and moon glow fade
out vec4 FragColor;

vec3 stars(vec3 dir, float scale, float threshold, float gain) {
    vec2 uv = vec2(atan(dir.z, dir.x), asin(clamp(dir.y, -1.0, 1.0))) * scale;
    vec2 cell = floor(uv);
    float h = hash21(cell);
    if (h < threshold) return vec3(0.0);
    vec2 centre = vec2(hash21(cell + 3.7), hash21(cell + 9.1)) * 0.6 + 0.2;
    float d = length(fract(uv) - centre);
    float twinkle = 0.65 + 0.35 * sin(uTime * (2.0 + 6.0 * hash21(cell + 1.3)) + 40.0 * h);
    vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.86, 0.7), hash21(cell + 5.5));
    return tint * smoothstep(0.09, 0.0, d) * (h - threshold) / (1.0 - threshold) * gain * twinkle;
}

vec3 nightSky(vec3 dir) {
    float up = dir.y;
    vec3 toMoon = normalize(-uMoonDir);
    float cosMoon = dot(dir, toMoon);
    vec3 zenith = vec3(0.0020, 0.0032, 0.0085), horizon = vec3(0.018, 0.024, 0.040);
    vec3 c = mix(horizon, zenith, pow(smoothstep(-0.02, 0.75, up), 0.6));
    c += vec3(0.020, 0.024, 0.034) * pow(max(cosMoon, 0.0), 6.0);             // moonlit haze
    c += vec3(0.10, 0.11, 0.13) * pow(max(cosMoon, 0.0), 180.0);             // halo
    if (up > -0.02) {
        // Milky Way: a noisy band along a tilted great circle
        vec3 axis = normalize(vec3(0.35, 0.25, -0.9));
        float band = exp(-pow(dot(dir, axis) / 0.22, 2.0));
        vec2 bp = vec2(atan(dir.z, dir.x) * 3.0, dir.y * 5.0);
        float dust = fbm(bp * 2.0) * (0.55 + 0.45 * fbm(bp * 6.0 + 4.0));
        c += vec3(0.020, 0.021, 0.028) * band * dust * smoothstep(0.0, 0.3, up);
        float vis = smoothstep(0.02, 0.2, up) * (1.0 - smoothstep(0.96, 0.995, cosMoon)) * (1.0 - uRain);
        c += (stars(dir, 170.0, 0.975, 0.9) + stars(dir, 60.0, 0.992, 3.5) + band * stars(dir, 320.0, 0.94, 0.25)) * vis;
        // Thin clouds on a plane 1 km up, drifting, silver-lined towards the moon
        vec2 cp = dir.xz / max(up, 0.05) * 1.2 + vec2(uTime * 0.012, uTime * 0.004);
        float cloud = smoothstep(0.52 - 0.45 * uRain, 0.85 - 0.3 * uRain, fbm(cp) * 0.75 + fbm(cp * 3.1) * 0.35);
        vec3 cloudCol = vec3(0.010, 0.012, 0.018) + vec3(0.10, 0.10, 0.11) * pow(max(cosMoon, 0.0), 24.0);
        c = mix(c, cloudCol * (1.0 - 0.5 * uRain), cloud * smoothstep(0.03, 0.25, up) * (0.85 + 0.15 * uRain));
    }
    return mix(c, uFogColor, smoothstep(0.08, -0.05, up));   // horizon haze meets the fog colour
}

void main() {
    vec4 p = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - uCamPos);
    float up = clamp(dir.y, -1.0, 1.0);
    vec2 q = gl_FragCoord.xy / uResolution - 0.5;
    if (uViewMode == 0) {
        float grain = (hash21(gl_FragCoord.xy + fract(uTime * 7.31) * 311.0) - 0.5) * 0.03;
        vec3 c = thermalPalette(0.05 + 0.07 * (1.0 - smoothstep(-0.05, 0.6, up)) + grain);
        FragColor = vec4(c * (1.0 - 0.6 * dot(q, q)), 1.0);
    } else if (uViewMode == 1 && uHdr == 1) {
        FragColor = vec4(nightSky(dir), 1.0);
    } else if (uViewMode == 1) {
        vec3 c = mix(vec3(0.030, 0.038, 0.065), vec3(0.004, 0.006, 0.016), smoothstep(0.0, 0.7, up));
        c += stars(dir, 110.0, 0.985, 1.0) * step(0.08, up);
        FragColor = vec4(c, 1.0);
    } else if (uViewMode == 2) {
        FragColor = vec4(mix(vec3(0.075, 0.08, 0.09), vec3(0.03, 0.035, 0.045), smoothstep(-0.2, 0.8, up)), 1.0);
    } else if (uViewMode == 4) {
        FragColor = vec4(0.05, 0.05, 0.05, 1.0);
    } else {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
)";

// ---------------------------------------------------------------- SSAO (half resolution)
const char* kSsaoFrag = R"(
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uNormal;
uniform mat4 uProj, uInvProj;
uniform float uRadius;
uniform int uSamples;
out float AO;

vec3 viewPos(vec2 uv) {
    float z = texture(uDepth, uv).r * 2.0 - 1.0;
    vec4 p = uInvProj * vec4(uv * 2.0 - 1.0, z, 1.0);
    return p.xyz / p.w;
}

void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) { AO = 1.0; return; }
    vec3 P = viewPos(vUV);
    vec3 N = normalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
    // Random tangent frame per pixel (rotates the kernel; the blur pass removes the noise)
    vec3 r = normalize(vec3(hash21(gl_FragCoord.xy) * 2.0 - 1.0, hash21(gl_FragCoord.yx + 7.0) * 2.0 - 1.0, 0.0));
    vec3 t = r - N * dot(r, N);
    if (dot(t, t) < 1e-6) t = abs(N.x) < 0.9 ? cross(N, vec3(1.0, 0.0, 0.0)) : cross(N, vec3(0.0, 1.0, 0.0));
    vec3 T = normalize(t);   // random vector (almost) parallel to N: use a fixed perpendicular instead
    mat3 TBN = mat3(T, cross(N, T), N);
    float occ = 0.0;
    for (int i = 0; i < uSamples; ++i) {
        float fi = float(i);
        vec3 s = vec3(hash21(vec2(fi, 1.3)) * 2.0 - 1.0, hash21(vec2(fi, 7.1)) * 2.0 - 1.0, hash21(vec2(fi, 3.9)));
        s = normalize(s) * mix(0.1, 1.0, pow((fi + 1.0) / float(uSamples), 2.0));
        vec3 S = P + TBN * s * uRadius;
        vec4 c = uProj * vec4(S, 1.0);
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) continue;
        float sceneZ = viewPos(uv).z;
        float range = smoothstep(0.0, 1.0, uRadius / abs(P.z - sceneZ));
        occ += (sceneZ >= S.z + 0.02 ? 1.0 : 0.0) * range;
    }
    AO = pow(1.0 - occ / float(uSamples), 1.6);
}
)";

// Depth-aware 5x5 blur of the AO buffer (keeps object edges sharp).
const char* kBlurFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform sampler2D uDepth;
uniform vec2 uNearFar;
out float AO;
float linearDepth(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSrc, 0));
    float centre = linearDepth(texture(uDepth, vUV).r);
    float sum = 0.0, wsum = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUV + vec2(x, y) * texel;
            float w = exp(-abs(linearDepth(texture(uDepth, uv).r) - centre) * 8.0 / max(centre * 0.05, 0.05));
            sum += texture(uSrc, uv).r * w;
            wsum += w;
        }
    }
    AO = sum / max(wsum, 1e-4);
}
)";

// ---------------------------------------------------------------- Volumetric fog and light shafts (half resolution)
const char* kFogFrag = R"(
in vec2 vUV;
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform float uTime;
uniform int uSteps;
uniform float uMaxDistance;
uniform float uDensity;        // extinction at ground level, 1/m
uniform float uHeightFalloff;  // 1/m
uniform vec3 uAmbientFog;      // sky light scattered by the fog
uniform vec3 uMoonDir;   uniform vec3 uMoonColor;
uniform vec3 uBeaconPos; uniform vec3 uBeaconColor; uniform vec2 uBeaconAtt;
uniform vec3 uSpotPos;   uniform vec3 uSpotDir;     uniform vec3 uSpotColor;
uniform vec2 uSpotCos;   uniform vec2 uSpotAtt;
uniform sampler2DArrayShadow uMoonShadow;
uniform mat4 uMoonVP1;
uniform sampler2DShadow uSpotShadow;
uniform mat4 uSpotVP;
uniform int uSpotShadowOn;
uniform int uMoonShafts;
out vec4 Fog;

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
   // rgb = in-scattered light, a = transmittance

float phaseHG(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * g * c, 1.5));
}

float moonVisibility(vec3 p) {
    if (uMoonShafts == 0) return 1.0;
    vec4 lp = uMoonVP1 * vec4(p, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s, vec3(0.0))) || any(greaterThan(s, vec3(1.0)))) return 1.0;
    return texture(uMoonShadow, vec4(s.xy, 1.0, s.z - 0.001));
}

float spotVisibility(vec3 p) {
    if (uSpotShadowOn == 0) return 1.0;
    vec4 lp = uSpotVP * vec4(p, 1.0);
    if (lp.w <= 0.0) return 0.0;
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s, vec3(0.0))) || any(greaterThan(s, vec3(1.0)))) return 0.0;
    return texture(uSpotShadow, vec3(s.xy, s.z - 0.0005));
}

float density(vec3 p) {
    float n = valueNoise(p.xz * 0.18 + vec2(p.y * 0.25 + uTime * 0.05, uTime * 0.02));
    return uDensity * exp(-max(p.y, 0.0) * uHeightFalloff) * (0.45 + 1.1 * n);
}

// [t0, t1] where the ray o + t d (0 <= t <= tMax) is inside the searchlight's outer cone.
vec2 coneInterval(vec3 o, vec3 d, float tMax) {
    float c2 = uSpotCos.y * uSpotCos.y;
    vec3 co = o - uSpotPos;
    float dv = dot(d, uSpotDir), cv = dot(co, uSpotDir);
    float a = dv * dv - c2, b = 2.0 * (dv * cv - dot(d, co) * c2), c = cv * cv - dot(co, co) * c2;
    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0 || abs(a) < 1e-6) return vec2(0.0);
    float sq = sqrt(disc);
    float t0 = (-b - sq) / (2.0 * a), t1 = (-b + sq) / (2.0 * a);
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
    if (a < 0.0) {             // f > 0 between the roots: one bounded piece, keep it if in front of the lamp
        if (dot(o + d * (0.5 * (t0 + t1)) - uSpotPos, uSpotDir) < 0.0) return vec2(0.0);
        return vec2(clamp(t0, 0.0, tMax), clamp(t1, 0.0, tMax));
    }
    // a > 0: f > 0 outside the roots; the front nappe is the piece the ray moves into along the axis
    if (dv > 0.0) return vec2(clamp(t1, 0.0, tMax), tMax);
    return vec2(0.0, clamp(t0, 0.0, tMax));
}

void main() {
    float d = texture(uDepth, vUV).r;
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 end = wp.xyz / wp.w;
    vec3 ray = end - uCamPos;
    float len = min(length(ray), uMaxDistance);
    vec3 dir = normalize(ray);
    float stepLen = len / float(uSteps);
    float jitter = hash21(gl_FragCoord.xy + fract(uTime) * 97.0);

    float cMoon = dot(dir, -uMoonDir);
    float phMoon = phaseHG(cMoon, 0.55) * 0.7 + 0.3 / (4.0 * 3.14159265);
    vec3 inscatter = vec3(0.0);
    float trans = 1.0;
    float sigmaSum = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= uSteps) break;
        vec3 p = uCamPos + dir * (stepLen * (float(i) + jitter));
        float sigma = density(p);
        vec3 light = uAmbientFog + uMoonColor * moonVisibility(p) * phMoon;
        vec3 toBeacon = uBeaconPos - p;
        float db = length(toBeacon);
        light += uBeaconColor / (1.0 + uBeaconAtt.x * db + uBeaconAtt.y * db * db) / (4.0 * 3.14159265);
        for (int k = 0; k < 8; ++k) {   // halos of the lamps, fire and floodlight in the mist
            if (k >= uPracticalCount) break;
            vec3 Lk;
            light += uPracticalColor[k].rgb * practicalAttenuation(k, p, Lk) * 0.12;
        }
        float ext = exp(-sigma * stepLen);
        inscatter += trans * light * (1.0 - ext);   // energy-conserving single scattering step
        trans *= ext;
        sigmaSum += sigma;
    }
    // Searchlight shaft: march only the part of the ray inside the light cone (analytic ray/cone test),
    // so a 2 m wide beam gets 24 samples instead of one or two.
    vec2 seg = coneInterval(uCamPos, dir, len);
    if (seg.y > seg.x) {
        float meanSigma = sigmaSum / float(max(uSteps, 1));
        const int kShaftSteps = 24;
        float ds = (seg.y - seg.x) / float(kShaftSteps);
        for (int i = 0; i < kShaftSteps; ++i) {
            float t = seg.x + ds * (float(i) + jitter);
            vec3 p = uCamPos + dir * t;
            vec3 toSpot = uSpotPos - p;
            float dl = length(toSpot);
            vec3 Ls = toSpot / max(dl, 1e-3);
            float cone = clamp((dot(-Ls, uSpotDir) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
            float att = cone * cone / (1.0 + uSpotAtt.x * dl + uSpotAtt.y * dl * dl);
            float sigma = density(p);
            inscatter += exp(-meanSigma * t) * uSpotColor * att * spotVisibility(p) * phaseHG(dot(dir, -Ls), 0.35) * sigma * ds;
        }
    }
    Fog = vec4(inscatter, trans);
}
)";

// ---------------------------------------------------------------- Bloom
// Prefilter: soft-knee threshold + 13-tap downsample (Jimenez, "Next generation post processing in CoD:AW").
const char* kBloomDownFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform int uPrefilter;
uniform float uThreshold;
out vec4 Color;
vec3 tap(vec2 o) { return texture(uSrc, vUV + o).rgb; }
void main() {
    vec2 t = 1.0 / vec2(textureSize(uSrc, 0));
    vec3 a = tap(t * vec2(-2, 2)), b = tap(t * vec2(0, 2)), c = tap(t * vec2(2, 2));
    vec3 d = tap(t * vec2(-2, 0)), e = tap(vec2(0)), f = tap(t * vec2(2, 0));
    vec3 g = tap(t * vec2(-2, -2)), h = tap(t * vec2(0, -2)), i = tap(t * vec2(2, -2));
    vec3 j = tap(t * vec2(-1, 1)), k = tap(t * vec2(1, 1)), l = tap(t * vec2(-1, -1)), m = tap(t * vec2(1, -1));
    vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    if (uPrefilter == 1) {
        if (any(isnan(col)) || any(isinf(col))) col = vec3(0.0);   // one bad pixel must not bloom
        float br = max(col.r, max(col.g, col.b));
        float knee = uThreshold * 0.5;
        float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee + 1e-5);
        col *= max(soft, br - uThreshold) / max(br, 1e-5);
        col = min(col, vec3(40.0));   // firefly clamp
    }
    Color = vec4(col, 1.0);
}
)";

// 3x3 tent upsample, blended additively onto the next larger mip.
const char* kBloomUpFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSrc;
uniform float uRadius;
out vec4 Color;
void main() {
    vec2 t = uRadius / vec2(textureSize(uSrc, 0));
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += (texture(uSrc, vUV + vec2(-t.x, 0)).rgb + texture(uSrc, vUV + vec2(t.x, 0)).rgb +
          texture(uSrc, vUV + vec2(0, -t.y)).rgb + texture(uSrc, vUV + vec2(0, t.y)).rgb) * 2.0;
    s += texture(uSrc, vUV + vec2(-t.x, -t.y)).rgb + texture(uSrc, vUV + vec2(t.x, -t.y)).rgb +
         texture(uSrc, vUV + vec2(-t.x, t.y)).rgb + texture(uSrc, vUV + vec2(t.x, t.y)).rgb;
    Color = vec4(s / 16.0, 1.0);
}
)";

// ---------------------------------------------------------------- Composite
const char* kCompositeFrag = R"(
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uFog;
uniform int uMode;            // 0 cinematic HDR (visible), 1 thermal sensor, 2 pass-through
uniform int uUseFog;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uTime;
uniform float uGrain;
uniform float uVignette;
uniform float uFade;          // dip to black between cinematic shots
uniform float uLetterbox;     // fraction of the height covered by each black bar
out vec4 FragColor;

// ACES filmic curve (Narkowicz 2015 fit)
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

vec4 finish(vec3 c) {
    if (vUV.y < uLetterbox || vUV.y > 1.0 - uLetterbox) return vec4(0.0, 0.0, 0.0, 1.0);
    return vec4(c * uFade, 1.0);
}

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    if (uMode == 2) { FragColor = finish(c); return; }
    vec2 q = vUV - 0.5;
    float grain = hash21(gl_FragCoord.xy + fract(uTime * 13.7) * 517.0) - 0.5;
    if (uMode == 1) {   // thermal sensor: hot areas bloom on the detector, slight lens fringing
        vec2 ca = q * 0.004;
        c = vec3(texture(uScene, vUV + ca).r, c.g, texture(uScene, vUV - ca).b);
        c += texture(uBloom, vUV).rgb * uBloomStrength * 0.6;
        FragColor = finish(c + grain * uGrain * 0.5);
        return;
    }
    if (uUseFog == 1) {
        vec4 f = texture(uFog, vUV);
        c = c * f.a + f.rgb;
    }
    c += texture(uBloom, vUV).rgb * uBloomStrength;
    c *= uExposure;
    // Colour grading before the curve: cool moonlit shadows, warm practical lights
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(c * vec3(0.92, 0.98, 1.12), c * vec3(1.08, 1.0, 0.9), smoothstep(0.05, 1.5, l));
    c = aces(c);
    c = pow(c, vec3(1.0 / 2.2));                            // linear -> sRGB (approx.)
    c *= 1.0 - uVignette * dot(q, q) * 1.6;
    c += grain * uGrain;
    FragColor = finish(clamp(c, 0.0, 1.0));
}
)";

}  // namespace ts::glsl
