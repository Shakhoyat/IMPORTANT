// v9 M8 (lighting depth: PCSS, cascade blending, AgX / ACES, analytic sky + SH ambient): T380-T389. CPU only.
#include <algorithm>
#include <cmath>
#include <string>

#include "TestKit.h"
#include "math/ShadowMath.h"
#include "math/SkyModel.h"
#include "math/ToneMap.h"
#include "scene/DayCycle.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

float lum(const Vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
float maxc(const Vec3& c) { return std::max(c.x, std::max(c.y, c.z)); }
float hueDiff(float a, float b) {
    const float d = std::fabs(a - b);
    return std::min(d, 360.0f - d);
}
Vec3 fromElevAz(float elevDeg, float azDeg) {
    const float e = radians(elevDeg), a = radians(azDeg);
    return {std::sin(a) * std::cos(e), std::sin(e), std::cos(a) * std::cos(e)};
}

}  // namespace

TS_TEST_GROUP(V9M8) {
    // ---- T380: PCSS penumbra width grows with the blocker distance (contact hardening) ------------------------------
    {
        // Area light, Fernando's similar triangles: receiver 10 m from the light, blockers moving towards the light.
        const float lightSize = 0.5f;
        float prev = -1.0f;
        bool grows = true;
        for (float dB = 9.99f; dB >= 2.0f; dB -= 0.25f) {
            const float w = pcssPenumbraWidth(10.0f, dB, lightSize);
            grows = grows && w > prev;
            prev = w;
        }
        const float touching = pcssPenumbraWidth(10.0f, 10.0f, lightSize);
        // Directional (sun): linear in the gap, 2 tan(0.8 deg) per metre.
        const float w01 = pcssPenumbraDirectional(50.1f, 50.0f, kSunSourceDeg), w1 = pcssPenumbraDirectional(51.0f, 50.0f, kSunSourceDeg);
        const float w5 = pcssPenumbraDirectional(55.0f, 50.0f, kSunSourceDeg), w10 = pcssPenumbraDirectional(60.0f, 50.0f, kSunSourceDeg);
        const float perMetre = 2.0f * std::tan(0.5f * radians(kSunSourceDeg));
        const float k0 = pcssKernelTexels(pcssPenumbraDirectional(50.0f, 50.0f, kSunSourceDeg), 0.005f, 1.6f, 40.0f);
        const float k10 = pcssKernelTexels(w10, 0.005f, 1.6f, 40.0f);
        record("T380", "PCSS: penumbra width grows with the blocker-receiver distance (area light and sun at infinity)",
               "w = (dR - dB) wL / dB strictly grows as the blocker moves away from the receiver, 0 at contact; sun: w(gap) linear, "
               "2 tan(0.8 deg) = 2.79 cm per metre; PCF kernel 1.6 texels at contact, wider (clamped 40) at 10 m",
               fmt("area: strictly growing %d, touching %.4f, w(dB=2) %.3f m; sun gap 0.1/1/5/10 m -> %.4f/%.4f/%.4f/%.4f m; kernel %.1f -> %.1f texels",
                   grows ? 1 : 0, touching, prev, w01, w1, w5, w10, k0, k10),
               grows && touching == 0.0f && w01 < w1 && w1 < w5 && w5 < w10 && std::fabs(w1 - perMetre) < 1e-5f &&
                   std::fabs(w10 / w1 - 10.0f) < 1e-3f && std::fabs(k0 - 1.6f) < 1e-6f && k10 > k0);
    }

    // ---- T381: cascade blend weight: 0 inside, 1 at the split, continuous and monotonic -------------------------------
    {
        const float split = 14.0f;
        float maxJump = 0.0f, prev = cascadeBlendWeight(0.0f, split, kCascadeBlendBand);
        bool mono = true;
        for (float d = 0.001f; d <= split + 2.0f; d += 0.001f) {
            const float w = cascadeBlendWeight(d, split, kCascadeBlendBand);
            maxJump = std::max(maxJump, std::fabs(w - prev));
            mono = mono && w >= prev - 1e-7f;
            prev = w;
        }
        const float inside = cascadeBlendWeight(split * 0.79f, split, kCascadeBlendBand);
        const float mid = cascadeBlendWeight(split * 0.9f, split, kCascadeBlendBand);
        const float at = cascadeBlendWeight(split, split, kCascadeBlendBand);
        // C1 at the ends: the slope vanishes at both ends of the band (smoothstep)
        const float e = 1e-3f;
        const float slopeLo = (cascadeBlendWeight(split * 0.8f + e, split, kCascadeBlendBand) - cascadeBlendWeight(split * 0.8f, split, kCascadeBlendBand)) / e;
        const float slopeHi = (cascadeBlendWeight(split, split, kCascadeBlendBand) - cascadeBlendWeight(split - e, split, kCascadeBlendBand)) / e;
        record("T381", "Cascade blending: the near / world cascade weight is continuous across the split band",
               "0 before 80 % of the split, 0.5 at 90 %, 1 at the split; max step over 1 mm < 0.001, monotonic, slope -> 0 at both ends",
               fmt("w(0.79 s) %.3f, w(0.9 s) %.3f, w(s) %.3f, max 1 mm step %.5f, monotonic %d, end slopes %.4f / %.4f per m", inside, mid, at, maxJump,
                   mono ? 1 : 0, slopeLo, slopeHi),
               inside == 0.0f && std::fabs(mid - 0.5f) < 1e-4f && at == 1.0f && maxJump < 0.001f && mono && slopeLo < 0.01f && slopeHi < 0.01f);
    }

    // ---- T382: AgX curve: monotonic over 20 stops, mid grey like the ACES setting, never above 1 -----------------------
    {
        bool mono = true, bounded = true;
        float prev = -1.0f;
        for (float ev = -14.0f; ev <= 6.0f; ev += 0.05f) {
            const float v = tonemap::display(Vec3{1, 1, 1} * (0.18f * std::exp2(ev)), tonemap::Operator::AgX).x;
            mono = mono && v >= prev - 1e-6f;
            bounded = bounded && v >= 0.0f && v <= 1.0f;
            prev = v;
        }
        float sprev = -1.0f;
        bool sigMono = true;
        for (float x = 0.0f; x <= 1.0f; x += 0.01f) {
            const float s = tonemap::agxSigmoid(x);
            sigMono = sigMono && s >= sprev - 1e-6f;
            sprev = s;
        }
        const float greyAgx = tonemap::display({0.18f, 0.18f, 0.18f}, tonemap::Operator::AgX).x;
        const float greyAces = tonemap::display({0.18f, 0.18f, 0.18f}, tonemap::Operator::Aces).x;
        const Vec3 g = tonemap::display({0.18f, 0.18f, 0.18f}, tonemap::Operator::AgX);
        const bool neutral = std::fabs(g.x - g.y) < 2e-3f && std::fabs(g.y - g.z) < 2e-3f;
        record("T382", "AgX: monotonic display response, mid grey maps like the ACES setting, neutral greys stay neutral",
               "display(0.18 * 2^ev) non-decreasing for ev -14..+6 and within 0..1; sigmoid monotonic on 0..1; 0.18 -> within 0.05 of ACES "
               "(sRGB 0.40..0.60); grey stays grey",
               fmt("monotonic %d, bounded %d, sigmoid monotonic %d, 0.18 -> AgX %.3f / ACES %.3f, grey rgb (%.3f %.3f %.3f)", mono ? 1 : 0,
                   bounded ? 1 : 0, sigMono ? 1 : 0, greyAgx, greyAces, g.x, g.y, g.z),
               mono && bounded && sigMono && std::fabs(greyAgx - greyAces) < 0.05f && greyAgx > 0.40f && greyAgx < 0.60f && neutral);
    }

    // ---- T383: saturated highlights: ACES clips a channel and skews the hue, AgX does not ------------------------------
    {
        // A saturated orange (fire, sodium lamp) and blue (sky glint, LED) pushed up in exposure.
        const Vec3 orange{1.0f, 0.25f, 0.02f}, blue{0.03f, 0.08f, 1.0f};
        int acesClipped = 0, agxClipped = 0;
        float acesHue = 0.0f, agxHue = 0.0f;
        for (const Vec3& c : {orange, blue}) {
            const float h0 = tonemap::hueDeg(tonemap::display(c * 0.1f, tonemap::Operator::Aces));
            const float a0 = tonemap::hueDeg(tonemap::display(c * 0.1f, tonemap::Operator::AgX));
            for (float k : {0.5f, 1.0f, 2.0f, 4.0f}) {
                const Vec3 ac = tonemap::display(c * k, tonemap::Operator::Aces), ag = tonemap::display(c * k, tonemap::Operator::AgX);
                if (maxc(ac) >= 0.9999f) ++acesClipped;
                if (maxc(ag) >= 0.9999f) ++agxClipped;
                acesHue = std::max(acesHue, hueDiff(tonemap::hueDeg(ac), h0));
                agxHue = std::max(agxHue, hueDiff(tonemap::hueDeg(ag), a0));
            }
        }
        // Path to white: a very bright saturated light desaturates towards white instead of staying a flat primary.
        const Vec3 hot = tonemap::display(orange * 64.0f, tonemap::Operator::AgX);
        const float sat = (maxc(hot) - std::min(hot.x, std::min(hot.y, hot.z))) / std::max(maxc(hot), 1e-4f);
        record("T383", "AgX keeps saturated highlights unclipped and their hue steadier than ACES",
               "orange / blue at exposures 0.5..4: ACES saturates a channel at 1.0, AgX none; AgX hue drift < ACES hue drift; orange x64 -> "
               "near white (saturation < 0.35)",
               fmt("clipped channels ACES %d / AgX %d of 8, max hue drift ACES %.1f deg / AgX %.1f deg, orange x64 AgX (%.3f %.3f %.3f) sat %.2f",
                   acesClipped, agxClipped, acesHue, agxHue, hot.x, hot.y, hot.z, sat),
               acesClipped > 0 && agxClipped == 0 && agxHue < acesHue && sat < 0.35f);
    }

    // ---- T384: tone-map weight: AgX by day, the v8 ACES curve at night, continuous through twilight --------------------
    {
        const LightRig night = DayCycle::rigAt(23.0f), noon = DayCycle::rigAt(10.5f);
        float prev = -1.0f, maxJump = 0.0f;
        bool mono = true;
        for (float d = 0.0f; d <= 1.0001f; d += 0.001f) {
            const float w = tonemap::agxDayWeight(d);
            if (prev >= 0.0f) maxJump = std::max(maxJump, w - prev);
            mono = mono && w >= prev;
            prev = w;
        }
        const float wn = tonemap::agxDayWeight(night.atmo.day), wd = tonemap::agxDayWeight(noon.atmo.day);
        const float cn = tonemap::dayContrast(wn), cd = tonemap::dayContrast(wd);
        record("T384", "Display transform weight: pure ACES at night (v8 parity), pure AgX by day, smooth in between",
               "23:00 weight 0 and contrast 1.10 (v8), 10:30 weight 1 and AgX contrast; monotonic, max step 0.002 per 0.001 of daylight",
               fmt("night day %.2f -> AgX %.2f contrast %.2f; 10:30 day %.2f -> AgX %.2f contrast %.2f; monotonic %d, max step %.4f", night.atmo.day, wn,
                   cn, noon.atmo.day, wd, cd, mono ? 1 : 0, maxJump),
               wn == 0.0f && wd == 1.0f && cn == tonemap::kAcesDayContrast && cd == tonemap::kAgxDayContrast && mono && maxJump < 0.002f);
    }

    // ---- T385: analytic sky: by day the zenith is darker and bluer than the horizon ------------------------------------
    {
        bool ok = true;
        std::string obs;
        for (float elev : {20.0f, 35.0f, 50.0f}) {
            const SkyModel m = preethamSky(fromElevAz(elev, 100.0f));
            const Vec3 zen = skyRadiance(m, {0, 1, 0});
            // horizon at 90 deg azimuth from the sun (away from the circumsolar glow) and the azimuth-averaged band
            const Vec3 hor = skyRadiance(m, fromElevAz(3.0f, 190.0f)), band = horizonRadiance(m);
            const bool darker = lum(zen) < lum(hor) && lum(zen) < lum(band);
            const bool bluer = zen.z / std::max(zen.x, 1e-4f) > band.z / std::max(band.x, 1e-4f);
            ok = ok && darker && bluer;
            obs += fmt("sun %.0f: zenith L %.3f b/r %.2f, horizon L %.3f, band L %.3f b/r %.2f; ", elev, lum(zen), zen.z / zen.x, lum(hor),
                       lum(band), band.z / band.x);
        }
        // Same angle to the sun: the direction nearer the horizon is brighter (Perez gradation term 1 + A e^(B / cos theta)).
        const SkyModel m = preethamSky(fromElevAz(45.0f, 0.0f));
        const float g1 = lum(skyRadiance(m, fromElevAz(90.0f, 0.0f))), g2 = lum(skyRadiance(m, fromElevAz(0.5f, 0.0f)));   // both 45 deg from the sun
        record("T385", "Analytic (Preetham) sky: zenith darker and bluer than the horizon by day; horizon brightening at equal sun angle",
               "sun 20 / 35 / 50 deg: L(zenith) < L(horizon 90 deg off the sun) and < band mean, zenith blue/red > band; 45 deg off the sun: "
               "horizon brighter than zenith",
               obs + fmt("equal-gamma zenith %.3f vs horizon %.3f", g1, g2), ok && g2 > g1);
    }

    // ---- T386: the sky is brightest towards DayCycle's sun (the sun disc direction of the sky shader) -----------------
    {
        bool ok = true;
        std::string obs;
        for (float hour : {8.0f, 10.5f, 16.5f}) {
            const Vec3 toSun = DayCycle::sunDirection(hour, DayCycleConfig{});
            const SkyModel m = preethamSky(toSun);
            Vec3 best{0, 1, 0};
            float bestL = -1.0f;
            for (int e = 1; e < 90; ++e)
                for (int a = 0; a < 360; a += 2) {
                    const Vec3 d = fromElevAz(static_cast<float>(e), static_cast<float>(a));
                    const float L = lum(skyRadiance(m, d));
                    if (L > bestL) { bestL = L; best = d; }
                }
            const float off = degrees(std::acos(clampf(dot(best, normalize(toSun)), -1.0f, 1.0f)));
            const float anti = lum(skyRadiance(m, normalize(Vec3{-toSun.x, std::max(toSun.y, 0.2f), -toSun.z})));
            ok = ok && off < 4.0f && bestL > 3.0f * anti && std::fabs(length(m.toSun) - 1.0f) < 1e-4f;
            obs += fmt("%.1f h: brightest %.1f deg from the sun, L %.2f vs anti-solar %.2f; ", hour, off, bestL, anti);
        }
        record("T386", "Sky model follows DayCycle's sun: the brightest sky direction is the sun's (circumsolar peak)",
               "08:00 / 10:30 / 16:30: brightest of a 1 x 2 deg grid within 4 deg of DayCycle::sunDirection, > 3x the anti-solar sky",
               obs, ok);
    }

    // ---- T387: SH ambient of the sky matches direct integration ----------------------------------------------------------
    {
        const SkyModel m = preethamSky(DayCycle::sunDirection(10.5f, DayCycleConfig{}));
        const Vec3 ground{0.2f, 0.18f, 0.15f};
        const AmbientSH sh = ambientSH(m, ground);
        float worst = 0.0f;
        const Vec3 normals[6] = {{0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const Vec3& n : normals) {
            const Vec3 a = evalAmbient(sh, n), b = ambientBruteForce(m, ground, n);
            worst = std::max(worst, std::fabs(lum(a) - lum(b)) / std::max(lum(b), 1e-4f));
        }
        const float up = lum(evalAmbient(sh, {0, 1, 0})), down = lum(evalAmbient(sh, {0, -1, 0}));
        const float side = lum(evalAmbient(sh, normalize(Vec3{1, 0, 1})));
        record("T387", "Ambient light: order-2 SH irradiance of the sky + ground matches the numerical hemisphere integral",
               "6 axis normals at 10:30: |SH - integral| / integral < 5 %; up > side > down",
               fmt("worst error %.2f %%, ambient up %.3f side %.3f down %.3f", worst * 100.0f, up, side, down),
               worst < 0.05f && up > side && side > down);
    }

    // ---- T388: DayCycle's day ambient comes from the same sky model; night untouched -----------------------------------
    {
        const LightRig noon = DayCycle::rigAt(12.0f), night = DayCycle::rigAt(23.0f);
        AmbientSH sh;
        for (int i = 0; i < 9; ++i) sh.c[i] = noon.atmo.skySH[i];
        const Vec3 up = evalAmbient(sh, {0, 1, 0});
        const bool same = std::fabs(up.x - noon.atmo.skyZenith.x) < 1e-4f && std::fabs(up.z - noon.atmo.skyZenith.z) < 1e-4f;
        bool nightZero = true;
        for (int i = 0; i < 9; ++i) nightZero = nightZero && length(night.atmo.skySH[i]) == 0.0f;
        const Atmosphere def{};
        const bool nightSame = night.atmo.skyZenith.z == def.skyZenith.z && night.atmo.fogColor.x == def.fogColor.x;
        const float L = lum(up);
        record("T388", "Day ambient = SH of the analytic sky (the sky that is drawn); calibrated to the v8 noon level; night unchanged",
               "noon skyZenith = SH ambient up (1e-4), luminance 0.35..0.55 (v8 0.49), blue > red; 23:00: SH all zero, v5 sky / fog literals",
               fmt("noon SH up (%.3f %.3f %.3f) L %.3f vs skyZenith (%.3f %.3f %.3f); night SH zero %d, v5 literals %d", up.x, up.y, up.z, L,
                   noon.atmo.skyZenith.x, noon.atmo.skyZenith.y, noon.atmo.skyZenith.z, nightZero ? 1 : 0, nightSame ? 1 : 0),
               same && L > 0.35f && L < 0.55f && up.z > up.x && nightZero && nightSame);
    }

    // ---- T389: shadow depth range: stored depth differences convert to metres along the light (PCSS gap) ---------------
    {
        const Vec3 lightDir = normalize(Vec3{0.4f, -0.8f, 0.3f});
        const LightMatrix lm = fitDirectionalShadow(lightDir, Sphere{{1.0f, 1.0f, 2.0f}, 12.0f}, 4096, 30.0f);
        const Vec3 p0{2.0f, 0.5f, 1.0f}, p1 = p0 - lightDir * 3.0f;   // p1 is 3 m closer to the light
        const Vec4 a = lm.viewProj * Vec4{p0.x, p0.y, p0.z, 1.0f}, b = lm.viewProj * Vec4{p1.x, p1.y, p1.z, 1.0f};
        const float za = (a.z / a.w) * 0.5f + 0.5f, zb = (b.z / b.w) * 0.5f + 0.5f;
        const float gap = (za - zb) * lm.depthRange;
        const float r = 12.0f * 4096.0f / 4094.0f;
        record("T389", "Shadow-map depth range: a stored depth difference x depthRange = metres between blocker and receiver",
               "depthRange = r + margin + r of the ortho fit; two points 3 m apart along the light -> 3.000 m",
               fmt("depthRange %.3f m (expected %.3f), gap %.4f m", lm.depthRange, 2.0f * r + 30.0f, gap),
               std::fabs(lm.depthRange - (2.0f * r + 30.0f)) < 1e-3f && std::fabs(gap - 3.0f) < 2e-3f);
    }
}
