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
// v6 time of day (DayCycle -> Atmosphere)
uniform float uDay;           // 0 night .. 1 day: weight of daySky()
uniform float uNight;         // stars, Milky Way
uniform vec3 uSunDir;         // direction sunlight travels
uniform vec3 uSunColor;       // disc / halo radiance (0 with the sun below the horizon)
uniform float uTwilight;      // warm horizon band around sunrise / sunset
layout(location = 0) out vec4 FragColor;

vec3 stars(vec3 dir, float scale, float threshold, float gain) {
    vec2 uv = vec2(atan(dir.z, dir.x), asin(tsClamp(dir.y, -1.0, 1.0))) * scale;
    vec2 cell = floor(uv);
    float h = hash21(cell);
    if (h < threshold) return vec3(0.0);
    vec2 centre = vec2(hash21(cell + 3.7), hash21(cell + 9.1)) * 0.6 + 0.2;
    float d = tsLength(fract(uv) - centre);
    float twinkle = 0.65 + 0.35 * sin(uTime * (2.0 + 6.0 * hash21(cell + 1.3)) + 40.0 * h);
    vec3 tint = tsMix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.86, 0.7), hash21(cell + 5.5));
    return tint * tsSmoothstep(0.09, 0.0, d) * (h - threshold) / (1.0 - threshold) * gain * twinkle;
}

// Five-octave fbm for the cumulus layer (the shared fbm has four).
float fbm5(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { s += a * valueNoise(p); p = p * 2.03 + vec2(17.1, 9.7); a *= 0.5; }
    return s;
}

// Cumulus density at a sky direction (plane projection 1 km up, drifting with the wind).
float cumulusDensity(vec2 cp, float cover) {
    float n = fbm5(cp) * 0.8 + 0.3 * valueNoise(cp * 6.0 + 3.0);
    return tsSmoothstep(0.55 - cover, 0.80 - cover * 0.6, n);
}

vec3 nightSky(vec3 dir) {
    float up = dir.y;
    vec3 toMoon = tsNormalize(-uMoonDir);
    float cosMoon = tsClamp(tsDot(dir, toMoon), -1.0, 1.0);
    float theta = acos(cosMoon);                                     // angular distance to the moon centre
    vec3 moonCool = vec3(0.78, 0.86, 1.0);                           // cool white moonlight tint
    float clear = 1.0 - 0.9 * uRain;

    // Atmosphere: deep-blue zenith, faint airglow band above the horizon, moonlit haze on the moon side
    vec3 zenith = vec3(0.0022, 0.0034, 0.0075), horizon = vec3(0.020, 0.027, 0.042);
    vec3 c = tsMix(horizon, zenith, pow(tsSmoothstep(-0.02, 0.75, up), 0.6));
    c += vec3(0.0015, 0.0048, 0.0085) * exp(-max(up, 0.0) * 7.0) * tsSmoothstep(-0.1, 0.02, up);   // airglow
    vec2 azim = tsNormalize(vec2(toMoon.x, toMoon.z) + 1e-5);
    float sideAz = max(tsDot(tsNormalize(dir.xz + 1e-5), azim), 0.0);
    c += moonCool * vec3(0.012, 0.014, 0.019) * pow(sideAz, 3.0) * exp(-max(up, 0.0) * 5.0) * clear;  // horizon glow to the moon
    c += moonCool * 0.018 * pow(max(cosMoon, 0.0), 6.0) * clear;                                    // wide moonlit haze

    // Moon halo: tight glow + wide aureole (the disc itself is the emissive "Moon" sphere)
    float halo = 0.55 * exp(-theta * 55.0) + 0.05 * exp(-theta * 14.0) + 0.012 * exp(-theta * 4.0);
    c += moonCool * halo * (0.25 + 0.75 * clear);

    vec3 cloudLight = vec3(0.0);
    float cloudAlpha = 0.0;
    if (up > -0.02) {
        float horizonFade = tsSmoothstep(0.0, 0.2, up);
        // Milky Way: tilted band, warm core / blue arms, dark dust lanes
        vec3 axis = tsNormalize(vec3(0.35, 0.25, -0.9));
        float band = exp(-pow(tsDot(dir, axis) / 0.22, 2.0));
        vec3 gu = tsNormalize(tsCross(axis, vec3(0.0, 1.0, 0.0)));                         // galactic frame: no pole singularity on the band
        vec2 bp = vec2(atan(tsDot(dir, tsCross(axis, gu)), tsDot(dir, gu)) * 3.0, tsDot(dir, axis) * 5.0);
        float dust = fbm(bp * 2.0) * (0.55 + 0.45 * fbm(bp * 6.0 + 4.0));
        float lane = tsSmoothstep(0.42, 0.62, fbm(bp * 3.0 + 11.0)) * band;                  // dark lanes cut the band
        vec3 mwCol = tsMix(vec3(0.020, 0.024, 0.036), vec3(0.030, 0.026, 0.022), tsSmoothstep(0.35, 0.7, fbm(bp + 20.0)));
        c += mwCol * band * dust * (1.0 - 0.75 * lane) * horizonFade * clear * uNight;

        // Clouds. Cumulus (lower, broken) and cirrus (high, thin), drifting; denser in rain.
        float cover = 0.06 + 0.22 * uRain;
        vec2 wind = vec2(uTime * 0.010, uTime * 0.004);
        vec2 cp = dir.xz / (up + 0.10) * 0.9 + wind;
        float dens = cumulusDensity(cp, cover);
        vec2 mo = tsNormalize(toMoon.xz + 1e-5) * 0.12 / (1.0 + 3.0 * max(toMoon.y, 0.0));
        float densM = cumulusDensity(cp + mo, cover);                                      // toward the moon
        float edge = tsClamp((dens - densM) * 3.0 + 0.35 * (1.0 - dens), 0.0, 1.0);          // lit rims, dark bases
        float fwd = pow(max(cosMoon, 0.0), 10.0);                                          // silver lining
        float moonUp = tsSmoothstep(-0.05, 0.3, toMoon.y);
        vec3 cumCol = vec3(0.0035, 0.0045, 0.0075) + moonCool * (0.020 * edge * (0.4 + moonUp) + 0.14 * fwd * (0.3 + edge) * moonUp)
                      + vec3(0.004, 0.005, 0.007) * (1.0 - dens);
        cumCol *= 1.0 - 0.3 * uRain;
        float cumA = dens * tsSmoothstep(0.02, 0.22, up) * (0.9 + 0.1 * uRain);

        vec2 rp = vec2(dir.x, dir.z) / (up + 0.25) * vec2(0.6, 2.2) + wind * 1.8 + 5.0;
        float cirrus = tsSmoothstep(0.50 - 0.2 * uRain, 0.85, fbm(rp) * 0.9 + 0.25 * fbm(rp * 3.7));
        float cirA = cirrus * 0.35 * tsSmoothstep(0.03, 0.3, up);
        vec3 cirCol = vec3(0.006, 0.0075, 0.012) + moonCool * (0.03 * pow(max(cosMoon, 0.0), 8.0) * moonUp + 0.004);

        cloudAlpha = 1.0 - (1.0 - cumA) * (1.0 - cirA);
        cloudLight = (cumCol * cumA + cirCol * cirA * (1.0 - cumA)) / max(cloudAlpha, 1e-4);

        // Stars fade near the horizon, near the moon and behind clouds
        float vis = tsSmoothstep(0.02, 0.22, up) * (1.0 - tsSmoothstep(0.94, 0.995, cosMoon)) * (1.0 - uRain) * (1.0 - cloudAlpha);
        c += (stars(dir, 170.0, 0.975, 0.9) + stars(dir, 60.0, 0.992, 3.5) + band * stars(dir, 320.0, 0.94, 0.25)) * vis * uNight;
        c = tsMix(c, cloudLight, cloudAlpha);
    }
    c = tsMix(c, uFogColor, tsSmoothstep(0.08, -0.05, up));   // horizon haze meets the fog colour
    c += (hash21(gl_FragCoord.xy) - 0.5) * 0.0008;         // dither: no banding in the dark gradient
    return max(c, vec3(0.0));
}

// Day sky (v9 M8): the analytic Preetham sky (skyModelRadiance, the same model the ambient light and the aerial
// perspective use), a limb-darkened sun disc with a tight Mie aureole, the twilight band, and the drifting cumulus layer
// (cloudDensity, the same field that casts the cloud shadows on the ground), lit by the sun and by the sky model.
vec3 daySky(vec3 dir) {
    vec3 toSun = tsNormalize(-uSunDir);
    float up = dir.y;
    float cosS = tsDot(dir, toSun);
    float below = tsMix(0.03, 1.0, tsSmoothstep(-0.10, 0.02, toSun.y));     // the sun below the horizon: the day sky fades
    vec3 c = skyModelRadiance(vec3(dir.x, max(up, 0.0), dir.z)) * below * (1.0 - 0.4 * uRain);
    c += uSunColor * 0.30 * pow(max(cosS, 0.0), 160.0) * (1.0 - 0.8 * uRain);   // aureole (Mie forward lobe)
    vec2 sideXZ = tsNormalize(dir.xz + 1e-5), sunXZ = tsNormalize(toSun.xz + 1e-5);
    float side = pow(max(tsDot(sideXZ, sunXZ), 0.0), 2.0);
    c += uTwilight * exp(-max(up, 0.0) * 5.0) * (vec3(0.90, 0.38, 0.12) * (0.25 + 0.75 * side) + vec3(0.10, 0.06, 0.14)) * 0.5;
    // Sun disc, 0.27 deg radius, limb darkening I(mu) = 1 - 0.6 (1 - mu) (mu = cosine at the solar surface)
    float r = acos(tsClamp(cosS, -1.0, 1.0)) / 0.0047124;
    if (r < 1.2) {
        float mu = sqrt(max(1.0 - r * r, 0.0));
        c += uSunColor * 40.0 * (1.0 - 0.6 * (1.0 - mu)) * tsSmoothstep(1.08, 0.92, r) * (1.0 - uRain);
    }
    if (up > 0.0) {
        // Cumulus layer: the shared cloudDensity field plus two zero-mean detail octaves that erode the edges, faded
        // out where the pixel footprint would alias them.
        float t = (kCloudAltitude - uCamPos.y) / max(up, 0.03);
        vec2 q = uCamPos.xz + dir.xz * t;
        vec2 cp = (q + kCloudWind * uTime) * kCloudScale;
        float fw = tsLength(fwidth(cp));
        float detail = 1.0 - tsSmoothstep(0.012, 0.05, fw);
        float n = fbm(cp) + detail * (0.11 * (valueNoise(cp * 8.3 + 5.1) - 0.5) + 0.05 * (valueNoise(cp * 17.9 + 2.7) - 0.5));
        float soft = 0.015 + 1.5 * fw;
        float d = tsSmoothstep(kCloudLo - soft, kCloudHi + soft, n);
        // Light reaching this point of the layer: optical depth of three samples towards the sun (Beer-Lambert),
        // "powder" darkening of thick cores, Henyey-Greenstein forward lobe for the silver linings; the sky light
        // from above and the ground light from below come from the sky model's SH ambient.
        float od = 0.0;
        for (int k = 1; k <= 3; ++k) od += cloudDensity(q + sunXZ * (9.0 * float(k * k)), uTime) * (1.0 / float(k));
        float sunT = exp(-0.9 * od - 0.6 * d);
        float powder = 1.0 - 0.55 * exp(-4.0 * d);
        float g = 0.62, hg = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosS, 1.5);
        float thin = (1.0 - d) * (1.0 - d);
        vec3 skyTop = skyAmbientSH(vec3(0.0, 1.0, 0.0)), skyBottom = skyAmbientSH(vec3(0.0, -1.0, 0.0));
        vec3 cloud = 0.92 * tsMix(skyBottom, skyTop * 1.15, 0.7 + 0.25 * (1.0 - d)) * below
                   + uSunColor * (0.30 * sunT * powder + 0.075 * hg * thin * (0.35 + 0.65 * sunT));
        // aerial perspective: distant cloud fades into the sky behind it
        cloud = tsMix(cloud, skyModelRadiance(vec3(dir.x, max(up, 0.0), dir.z)) * below, 1.0 - exp(-t / 5200.0));
        float alpha = tsSmoothstep(0.0, 0.5, d) * (0.6 + 0.35 * d) * tsSmoothstep(0.015, 0.14, up);
        c = tsMix(c, cloud * (1.0 - 0.3 * uRain), alpha);
    }
    c = tsMix(c, uFogColor, tsSmoothstep(0.06, -0.04, up));                    // horizon haze meets the fog colour
    return max(c, vec3(0.0));
}

void main() {
    vec4 p = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = tsNormalize(p.xyz / p.w - uCamPos);
    float up = tsClamp(dir.y, -1.0, 1.0);
    vec2 q = gl_FragCoord.xy / uResolution - 0.5;
    if (uViewMode == 0) {   // cold sky: apparent temperature rises towards the (warmer, denser) horizon air
        float h = 0.03 + 0.06 * (1.0 - tsSmoothstep(-0.05, 0.6, up)) + 0.02 * uRain;
        FragColor = vec4(h, h, h, 1.0);
    } else if (uViewMode == 1 && uHdr == 1) {
        vec3 c = nightSky(dir);
        if (uDay > 0.0) c = tsMix(c, daySky(dir), uDay);
        FragColor = vec4(c, 1.0);
    } else if (uViewMode == 1) {
        vec3 c = tsMix(vec3(0.030, 0.038, 0.065), vec3(0.004, 0.006, 0.016), tsSmoothstep(0.0, 0.7, up));
        c += stars(dir, 110.0, 0.985, 1.0) * tsStep(0.08, up) * uNight;
        if (uDay > 0.0) c = tsMix(c, vec3(1.0) - exp(-1.25 * daySky(dir)), uDay);   // v1 look: tone-mapped in place
        FragColor = vec4(c, 1.0);
    } else if (uViewMode == 2) {
        FragColor = vec4(tsMix(vec3(0.075, 0.08, 0.09), vec3(0.03, 0.035, 0.045), tsSmoothstep(-0.2, 0.8, up)), 1.0);
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
    vec3 N = tsNormalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
    // Random tangent frame per pixel (rotates the kernel; the blur pass removes the noise)
    vec3 r = tsNormalize(vec3(hash21(gl_FragCoord.xy) * 2.0 - 1.0, hash21(gl_FragCoord.yx + 7.0) * 2.0 - 1.0, 0.0));
    vec3 t = r - N * tsDot(r, N);
    if (tsDot(t, t) < 1e-6) t = abs(N.x) < 0.9 ? tsCross(N, vec3(1.0, 0.0, 0.0)) : tsCross(N, vec3(0.0, 1.0, 0.0));
    vec3 T = tsNormalize(t);   // random vector (almost) parallel to N: use a fixed perpendicular instead
    mat3 TBN = mat3(T, tsCross(N, T), N);
    float occ = 0.0;
    for (int i = 0; i < uSamples; ++i) {
        float fi = float(i);
        vec3 s = vec3(hash21(vec2(fi, 1.3)) * 2.0 - 1.0, hash21(vec2(fi, 7.1)) * 2.0 - 1.0, hash21(vec2(fi, 3.9)));
        s = tsNormalize(s) * tsMix(0.1, 1.0, pow((fi + 1.0) / float(uSamples), 2.0));
        vec3 S = P + TBN * s * uRadius;
        vec4 c = uProj * vec4(S, 1.0);
        vec2 uv = c.xy / c.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) continue;
        float sceneZ = viewPos(uv).z;
        float range = tsSmoothstep(0.0, 1.0, uRadius / abs(P.z - sceneZ));
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
uniform vec3 uSpot2Pos;  uniform vec3 uSpot2Dir;    uniform vec3 uSpot2Color;   // UAV 2 (unshadowed, same cone)
uniform sampler2DArrayShadow uMoonShadow;
uniform mat4 uMoonVP1;
uniform sampler2DShadow uSpotShadow;
uniform mat4 uSpotVP;
uniform int uSpotShadowOn;
uniform int uMoonShafts;
uniform float uHaloScale;      // v8: lamp halos in the mist (1 night .. ~0.1 in daylight: sunlit haze swamps them)
out vec4 Fog;

// Practical lights (lamps, fire, floodlight, explosion flashes): up to 16.
#define TS_MAX_PRACTICALS 16
uniform int uPracticalCount;
uniform vec4 uPracticalPos[16];    // xyz position, w range
uniform vec4 uPracticalColor[16];  // rgb linear radiance scale, w 1 = indoors
uniform vec4 uPracticalDir[16];    // xyz spot axis, w cos(cone) (-2 = omni)
// Indoor volumes (underground bunker): no sky light, no moon / beacon / searchlight, light from lamps on the
// same side of the walls only, thin fog, no rain. Boxes come from the scene's WalkSpace (max 8).
uniform int uIndoorCount;
uniform vec3 uIndoorMin[8];
uniform vec3 uIndoorMax[8];
float indoorMask(vec3 P) {
    float m = 0.0;
    for (int i = 0; i < 8; ++i) {
        if (i >= uIndoorCount) break;
        vec3 a = uIndoorMin[i] - P, b = P - uIndoorMax[i];
        float outside = max(max(max(a.x, b.x), max(a.y, b.y)), max(a.z, b.z));
        m = max(m, 1.0 - tsSmoothstep(-0.2, 0.2, outside));
    }
    return m;
}

float practicalAttenuation(int i, vec3 P, out vec3 L) {
    vec3 d = uPracticalPos[i].xyz - P;
    float dist = max(tsLength(d), 1e-3);
    L = d / dist;
    float x = dist / uPracticalPos[i].w;
    float window = tsClamp(1.0 - x * x * x * x, 0.0, 1.0);
    float att = window * window / (1.0 + dist * dist);
    if (uPracticalDir[i].w > -1.5) att *= tsSmoothstep(uPracticalDir[i].w, uPracticalDir[i].w + 0.1, tsDot(-L, uPracticalDir[i].xyz));
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
    return uDensity * exp(-max(p.y, 0.0) * uHeightFalloff) * (0.45 + 1.1 * n) * (1.0 - 0.75 * indoorMask(p));
}

// [t0, t1] where the ray o + t d (0 <= t <= tMax) is inside the searchlight's outer cone.
vec2 coneInterval(vec3 o, vec3 d, float tMax, vec3 sp, vec3 sd) {
    float c2 = uSpotCos.y * uSpotCos.y;
    vec3 co = o - sp;
    float dv = tsDot(d, sd), cv = tsDot(co, sd);
    float a = dv * dv - c2, b = 2.0 * (dv * cv - tsDot(d, co) * c2), c = cv * cv - tsDot(co, co) * c2;
    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0 || abs(a) < 1e-6) return vec2(0.0);
    float sq = sqrt(disc);
    float t0 = (-b - sq) / (2.0 * a), t1 = (-b + sq) / (2.0 * a);
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
    if (a < 0.0) {             // f > 0 between the roots: one bounded piece, keep it if in front of the lamp
        if (tsDot(o + d * (0.5 * (t0 + t1)) - sp, sd) < 0.0) return vec2(0.0);
        return vec2(tsClamp(t0, 0.0, tMax), tsClamp(t1, 0.0, tMax));
    }
    // a > 0: f > 0 outside the roots; the front nappe is the piece the ray moves into along the axis
    if (dv > 0.0) return vec2(tsClamp(t1, 0.0, tMax), tMax);
    return vec2(0.0, tsClamp(t0, 0.0, tMax));
}

void main() {
    float d = texture(uDepth, vUV).r;
    vec4 wp = uInvViewProj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 end = wp.xyz / wp.w;
    vec3 ray = end - uCamPos;
    float len = min(tsLength(ray), uMaxDistance);
    vec3 dir = tsNormalize(ray);
    float stepLen = len / float(uSteps);
    float jitter = hash21(gl_FragCoord.xy + fract(uTime) * 97.0);

    float cMoon = tsDot(dir, -uMoonDir);
    float phMoon = phaseHG(cMoon, 0.55) * 0.7 + 0.3 / (4.0 * 3.14159265);
    vec3 inscatter = vec3(0.0);
    float trans = 1.0;
    float sigmaSum = 0.0;
    vec3 halo = vec3(0.0);
    for (int i = 0; i < 64; ++i) {
        if (i >= uSteps) break;
        vec3 p = uCamPos + dir * (stepLen * (float(i) + jitter));
        float sigma = density(p);
        float inside = indoorMask(p);
        vec3 light = (uAmbientFog + uMoonColor * moonVisibility(p) * phMoon) * (1.0 - inside);
        vec3 toBeacon = uBeaconPos - p;
        float db = tsLength(toBeacon);
        light += uBeaconColor / (1.0 + uBeaconAtt.x * db + uBeaconAtt.y * db * db) / (4.0 * 3.14159265) * (1.0 - inside);
        // Halos of the lamps, fire, flashes in the mist. v9: evaluated on every other step and held for the next
        // (a halo changes slowly along the ray; the per-pixel jitter + TAA hide the hold); none by day (faded out).
        if ((i & 1) == 0) {
            halo = vec3(0.0);
            for (int k = 0; k < TS_MAX_PRACTICALS; ++k) {
                if (k >= uPracticalCount || uHaloScale < 0.15) break;
                if (abs(uPracticalColor[k].w - inside) > 0.5) continue;
                vec3 Lk;
                halo += uPracticalColor[k].rgb * practicalAttenuation(k, p, Lk) * (0.12 * uHaloScale);
            }
        }
        light += halo;
        float ext = exp(-sigma * stepLen);
        inscatter += trans * light * (1.0 - ext);   // energy-conserving single scattering step
        trans *= ext;
        sigmaSum += sigma;
    }
    // Searchlight shafts (UAV 1 shadowed, UAV 2 unshadowed): march only the part of the ray inside each light cone
    // (analytic ray/cone test), so a 2 m wide beam gets 24 samples instead of one or two.
    float meanSigma = sigmaSum / float(max(uSteps, 1));
    for (int s = 0; s < 2; ++s) {
        vec3 sp = s == 0 ? uSpotPos : uSpot2Pos, sd = s == 0 ? uSpotDir : uSpot2Dir, sc = s == 0 ? uSpotColor : uSpot2Color;
        // A viewer at the lamp (the UAV's own gimbal / first-person camera) looks down the whole shaft: fade it out
        // within 4 m of the lamp so the beam does not white out its own camera.
        sc *= tsSmoothstep(0.6, 4.0, tsLength(uCamPos - sp));
        if (tsDot(sc, sc) <= 0.0) continue;
        vec2 seg = coneInterval(uCamPos, dir, len, sp, sd);
        if (seg.y <= seg.x) continue;
        const int kShaftSteps = 24;
        float ds = (seg.y - seg.x) / float(kShaftSteps);
        for (int i = 0; i < kShaftSteps; ++i) {
            float t = seg.x + ds * (float(i) + jitter);
            vec3 p = uCamPos + dir * t;
            vec3 toSpot = sp - p;
            float dl = tsLength(toSpot);
            vec3 Ls = toSpot / max(dl, 1e-3);
            float cone = tsClamp((tsDot(-Ls, sd) - uSpotCos.y) / max(uSpotCos.x - uSpotCos.y, 1e-4), 0.0, 1.0);
            float att = cone * cone / (1.0 + uSpotAtt.x * dl + uSpotAtt.y * dl * dl);
            float sigma = density(p);
            float vis = s == 0 ? spotVisibility(p) : 1.0;
            inscatter += exp(-meanSigma * t) * sc * att * vis * phaseHG(tsDot(dir, -Ls), 0.35) * sigma * ds;
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
        float soft = tsClamp(br - uThreshold + knee, 0.0, 2.0 * knee);
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

}  // namespace ts::glsl
