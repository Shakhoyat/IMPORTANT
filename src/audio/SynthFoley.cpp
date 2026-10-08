// v7 one-shot foley and UI cues: footsteps per surface, radio chatter, game UI sounds. Deterministic per seed.
#include <algorithm>
#include <cmath>

#include "audio/Synth.h"

namespace ts::audio {

namespace {

inline size_t samples(float seconds) { return static_cast<size_t>(seconds * kSampleRate); }
inline float timeOf(size_t i) { return static_cast<float>(i) / kSampleRate; }

void normalizePeak(std::vector<float>& v, float peak) {
    float m = 0.0f;
    for (float s : v) m = std::max(m, std::fabs(s));
    if (m < 1e-9f) return;
    const float g = peak / m;
    for (float& s : v) s *= g;
}
void fadeEdges(std::vector<float>& v, float in, float out) {
    const size_t a = std::min(v.size(), samples(in)), b = std::min(v.size(), samples(out));
    for (size_t i = 0; i < a; ++i) v[i] *= static_cast<float>(i) / static_cast<float>(a);
    for (size_t i = 0; i < b; ++i) v[v.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(b);
}
// Decaying sine at 'at' with a pitch glide f0 -> f1 (time constant glideTau) and decay tau.
void addSine(std::vector<float>& v, size_t at, float f0, float f1, float glideTau, float tau, float amp, float attack = 0.001f) {
    if (at >= v.size()) return;
    float ph = 0.0f;
    const size_t len = std::min(v.size() - at, samples(tau * 7.0f));
    for (size_t i = 0; i < len; ++i) {
        const float t = timeOf(i);
        const float f = f1 + (f0 - f1) * expEnv(t, glideTau);
        ph += kTwoPi * f / kSampleRate;
        if (ph > kTwoPi) ph -= kTwoPi;
        v[at + i] += amp * std::sin(ph) * expEnv(t, tau) * clampf(t / attack, 0.0f, 1.0f);
    }
}
// Linear-sweep tone with a short raised-cosine window, optional odd harmonics (square-ish).
void addSweep(std::vector<float>& v, size_t at, float f0, float f1, float seconds, float amp, float odd = 0.0f) {
    if (at >= v.size()) return;
    const size_t len = std::min(v.size() - at, samples(seconds));
    float ph = 0.0f;
    for (size_t i = 0; i < len; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(len);
        ph += kTwoPi * lerpf(f0, f1, u) / kSampleRate;
        if (ph > kTwoPi) ph -= kTwoPi;
        const float w = edge(static_cast<int>(i), static_cast<int>(len), 200);
        v[at + i] += amp * w * (std::sin(ph) + odd * (std::sin(3.0f * ph) / 3.0f + std::sin(5.0f * ph) / 5.0f));
    }
}

// One impact of a foot on a surface into v at 'at' (heel: amp 1, toe: lower).
void footImpact(std::vector<float>& v, size_t at, Surface s, float amp, Rng& rng) {
    const size_t len = std::min(v.size() - at, samples(0.3f));
    switch (s) {
        case Surface::Dirt: {
            OnePole lp, lp2; Svf scuff;
            for (size_t i = 0; i < len; ++i) {
                const float t = timeOf(i);
                float x = lp2.lp(lp.lp(rng.bi(), onePoleAlpha(500.0f)), onePoleAlpha(500.0f)) * 6.0f * expEnv(t, 0.018f);
                scuff.process(rng.bi(), 900.0f, 0.8f);
                x += scuff.band * 0.3f * expEnv(t, 0.05f) * clampf(t / 0.01f, 0.0f, 1.0f);
                v[at + i] += amp * x;
            }
            addSine(v, at, 90.0f, 60.0f, 0.02f, 0.025f, amp * 0.5f);
            break;
        }
        case Surface::Gravel: {
            OnePole lp;
            for (size_t i = 0; i < len; ++i)
                v[at + i] += amp * lp.lp(rng.bi(), onePoleAlpha(400.0f)) * 3.0f * expEnv(timeOf(i), 0.02f);
            const int grains = 40 + static_cast<int>(rng.uni() * 30.0f);
            for (int g = 0; g < grains; ++g) {
                const float u = rng.uni();
                const size_t g0 = at + samples(0.16f * u * u);   // dense at the impact, thinning out
                const float f = 2000.0f + 5000.0f * rng.uni();
                const float a = amp * (0.25f + 0.75f * rng.uni()) * (1.0f - 0.6f * u);
                float ph = rng.uni() * kTwoPi;
                OnePole hp;
                for (size_t i = 0; i < samples(0.004f) && g0 + i < v.size(); ++i) {
                    const float t = timeOf(i);
                    ph += kTwoPi * f / kSampleRate;
                    v[g0 + i] += a * (0.6f * std::sin(ph) + hp.hp(rng.bi(), onePoleAlpha(2500.0f))) * expEnv(t, 0.0012f);
                }
            }
            break;
        }
        case Surface::Concrete: {
            OnePole hp; Svf body, scuff;
            for (size_t i = 0; i < len; ++i) {
                const float t = timeOf(i);
                const float n = rng.bi();
                float x = hp.hp(n, onePoleAlpha(1800.0f)) * 1.1f * expEnv(t, 0.004f);
                body.process(n, 2400.0f, 3.0f);
                x += body.band * 0.8f * expEnv(t, 0.018f);
                scuff.process(rng.bi(), 3500.0f, 1.0f);
                if (t > 0.03f) x += scuff.band * 0.12f * expEnv(t - 0.03f, 0.04f);
                v[at + i] += amp * x;
            }
            addSine(v, at, 140.0f, 105.0f, 0.01f, 0.02f, amp * 0.45f);
            break;
        }
        case Surface::Metal: {
            static constexpr float kModes[5] = {420.0f, 1130.0f, 1870.0f, 2690.0f, 3550.0f};
            static constexpr float kAmps[5] = {1.0f, 0.7f, 0.5f, 0.35f, 0.25f};
            static constexpr float kTaus[5] = {0.22f, 0.16f, 0.11f, 0.08f, 0.06f};
            OnePole hp;
            for (size_t i = 0; i < std::min(len, samples(0.02f)); ++i)
                v[at + i] += amp * hp.hp(rng.bi(), onePoleAlpha(2000.0f)) * 0.9f * expEnv(timeOf(i), 0.003f);
            for (int m = 0; m < 5; ++m) {
                const float f = kModes[m] * (1.0f + 0.03f * rng.bi());
                addSine(v, at, f, f, 1.0f, kTaus[m], amp * kAmps[m] * 0.22f * (0.7f + 0.6f * rng.uni()));
            }
            addSine(v, at, 190.0f, 175.0f, 0.03f, 0.08f, amp * 0.35f);   // hollow deck boom
            break;
        }
        case Surface::Grass: {
            Svf swish; OnePole lp;
            for (size_t i = 0; i < len; ++i) {
                const float t = timeOf(i);
                swish.process(rng.bi(), 3200.0f, 0.7f);
                float x = swish.band * 0.5f * clampf(t / 0.015f, 0.0f, 1.0f) * expEnv(t, 0.08f);
                x += lp.lp(rng.bi(), onePoleAlpha(300.0f)) * 2.0f * expEnv(t, 0.02f);
                v[at + i] += amp * x;
            }
            break;
        }
    }
}

}  // namespace

std::vector<float> synthFootstep(Surface s, uint32_t variant) {
    Rng rng(variant * 7919u + static_cast<uint32_t>(s) * 131u + 17u);
    std::vector<float> v(samples(s == Surface::Metal ? 0.6f : 0.35f), 0.0f);
    footImpact(v, 0, s, 1.0f, rng);
    footImpact(v, samples(0.035f + 0.03f * rng.uni()), s, 0.45f + 0.2f * rng.uni(), rng);   // toe
    static constexpr float kPeak[kSurfaceCount] = {0.45f, 0.5f, 0.55f, 0.55f, 0.35f};
    normalizePeak(v, kPeak[static_cast<int>(s)] * (0.85f + 0.15f * rng.uni()));
    fadeEdges(v, 0.0005f, 0.03f);
    return v;
}

std::vector<float> synthRadioChatter(uint32_t seed) {
    Rng r(seed * 2654435761u + 11u);
    struct Vowel { float f1, f2; };
    static constexpr Vowel kV[5] = {{730, 1090}, {530, 1840}, {300, 2250}, {570, 840}, {320, 870}};
    const int syl = 4 + static_cast<int>(r.uni() * 6.0f);
    // syllable plan
    struct Syl { float t0, dur; int vowel; float pitch; bool fric; };
    std::vector<Syl> plan;
    float t = 0.09f;
    for (int k = 0; k < syl; ++k) {
        Syl s{t, 0.08f + 0.12f * r.uni(), static_cast<int>(r.uni() * 5.0f) % 5, 1.0f + 0.18f * r.bi(), r.uni() < 0.35f};
        plan.push_back(s);
        t += s.dur + 0.02f + 0.06f * r.uni() + (r.uni() < 0.2f ? 0.12f : 0.0f);
    }
    const float voiceEnd = t;
    const float total = voiceEnd + 0.26f;
    std::vector<float> v(samples(total), 0.0f);
    const float f0 = 105.0f + 30.0f * r.uni();   // the speaker
    float glot = 0.0f, f1 = 600.0f, f2 = 1200.0f;
    Svf form1, form2, form3, fric, band1, band2;
    OnePole hp1, hp2, srcLp;
    size_t si = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        const float tt = timeOf(i);
        while (si + 1 < plan.size() && tt > plan[si].t0 + plan[si].dur) ++si;
        const Syl& s = plan[si];
        float env = 0.0f;
        if (tt >= s.t0 && tt < s.t0 + s.dur) env = std::pow(std::sin(kPi * (tt - s.t0) / s.dur), 0.5f);
        const float decl = 1.0f - 0.15f * tt / voiceEnd;   // pitch declination over the phrase
        glot += f0 * s.pitch * decl / kSampleRate;
        if (glot >= 1.0f) glot -= 1.0f;
        const float pulse = srcLp.lp(glot < 0.08f ? 1.0f : -0.087f, onePoleAlpha(2500.0f));
        f1 += (kV[s.vowel].f1 - f1) * 0.002f;   // formants glide between syllables
        f2 += (kV[s.vowel].f2 - f2) * 0.002f;
        form1.process(pulse, f1, 6.0f);
        form2.process(pulse, f2, 8.0f);
        form3.process(pulse, 2500.0f, 10.0f);
        float x = (form1.band + 0.7f * form2.band + 0.25f * form3.band) * env;
        if (s.fric && tt >= s.t0 - 0.04f && tt < s.t0) {   // consonant noise just before the vowel
            fric.process(r.bi(), 2600.0f, 1.5f);
            x += fric.band * 0.6f;
        }
        // squelch open (start) and tail (end)
        if (tt < 0.06f) x += r.bi() * 0.9f * (1.0f - tt / 0.06f);
        if (tt > voiceEnd && tt < voiceEnd + 0.1f) x += r.bi() * 0.7f * (1.0f - (tt - voiceEnd) / 0.1f);
        x += r.bi() * 0.04f * (tt < voiceEnd + 0.1f ? 1.0f : 0.0f);   // carrier hiss
        // radio channel: 300 Hz - 3 kHz, overdriven
        x = hp2.hp(hp1.hp(x, onePoleAlpha(300.0f)), onePoleAlpha(300.0f));
        band1.process(x, 3000.0f, 0.7f);
        band2.process(band1.lo, 3000.0f, 0.7f);
        x = std::tanh(band2.lo * 4.0f) * (1.0f + 0.05f * std::sin(kTwoPi * 7.0f * tt));
        v[i] = x;
    }
    normalizePeak(v, 0.5f);
    // roger beep after the tail
    addSweep(v, samples(voiceEnd + 0.12f), 2475.0f, 2475.0f, 0.08f, 0.22f);
    fadeEdges(v, 0.002f, 0.02f);
    return v;
}

const char* uiCueName(UiCue c) {
    switch (c) {
        case UiCue::PovJump: return "pov_jump";
        case UiCue::PickerOpen: return "picker_open";
        case UiCue::PickerClose: return "picker_close";
        case UiCue::PickerMove: return "picker_move";
        case UiCue::Possess: return "possess";
        case UiCue::Release: return "release";
        case UiCue::HitMarker: return "hit_marker";
        case UiCue::Alert: return "alert";
        case UiCue::FreeCam: return "free_cam";
    }
    return "?";
}

std::vector<float> synthUiCue(UiCue c) {
    Rng rng(1000u + static_cast<uint32_t>(c));
    std::vector<float> v;
    switch (c) {
        case UiCue::PovJump: {   // swoosh up through the air + a sub thump when the new view lands
            v.assign(samples(0.6f), 0.0f);
            Svf band;
            for (size_t i = 0; i < v.size(); ++i) {
                const float u = timeOf(i) / 0.6f;
                const float fc = u < 0.5f ? 400.0f * std::pow(15.0f, u / 0.5f) : 6000.0f * std::pow(0.25f, (u - 0.5f) / 0.5f);
                band.process(rng.bi(), fc, 1.6f);
                v[i] = band.band * std::pow(std::sin(kPi * u), 1.5f) * 1.2f;
            }
            addSweep(v, 0, 300.0f, 1800.0f, 0.28f, 0.12f);
            addSine(v, samples(0.3f), 70.0f, 40.0f, 0.05f, 0.09f, 0.9f, 0.004f);
            normalizePeak(v, 0.6f);
            break;
        }
        case UiCue::PickerOpen:
        case UiCue::PickerClose: {
            v.assign(samples(0.2f), 0.0f);
            const float a = c == UiCue::PickerOpen ? 660.0f : 990.0f, b = c == UiCue::PickerOpen ? 990.0f : 660.0f;
            addSine(v, 0, a, a, 1.0f, 0.04f, 0.6f, 0.002f);
            addSine(v, 0, a * 2.0f, a * 2.0f, 1.0f, 0.015f, 0.2f, 0.002f);
            addSine(v, samples(0.07f), b, b, 1.0f, 0.05f, 0.6f, 0.002f);
            addSine(v, samples(0.07f), b * 2.0f, b * 2.0f, 1.0f, 0.02f, 0.2f, 0.002f);
            normalizePeak(v, c == UiCue::PickerOpen ? 0.45f : 0.4f);
            break;
        }
        case UiCue::PickerMove: {
            v.assign(samples(0.05f), 0.0f);
            addSine(v, 0, 2200.0f, 2200.0f, 1.0f, 0.006f, 0.6f, 0.0005f);
            for (size_t i = 0; i < samples(0.002f); ++i) v[i] += rng.bi() * 0.3f;
            normalizePeak(v, 0.3f);
            break;
        }
        case UiCue::Possess: {   // thump + rising charge + lock click
            v.assign(samples(0.5f), 0.0f);
            addSine(v, 0, 85.0f, 45.0f, 0.05f, 0.08f, 1.0f, 0.003f);
            addSweep(v, 0, 300.0f, 1200.0f, 0.22f, 0.3f, 0.6f);
            addSine(v, samples(0.22f), 1800.0f, 1800.0f, 1.0f, 0.04f, 0.4f, 0.0005f);
            addSine(v, samples(0.22f), 2700.0f, 2700.0f, 1.0f, 0.03f, 0.3f, 0.0005f);
            normalizePeak(v, 0.6f);
            break;
        }
        case UiCue::Release: {
            v.assign(samples(0.4f), 0.0f);
            addSweep(v, 0, 1200.0f, 300.0f, 0.25f, 0.3f, 0.6f);
            addSine(v, samples(0.24f), 70.0f, 45.0f, 0.05f, 0.06f, 0.7f, 0.003f);
            normalizePeak(v, 0.5f);
            break;
        }
        case UiCue::HitMarker: {   // inharmonic metallic tick
            v.assign(samples(0.1f), 0.0f);
            addSine(v, 0, 3150.0f, 3150.0f, 1.0f, 0.025f, 0.6f, 0.0003f);
            addSine(v, 0, 4720.0f, 4720.0f, 1.0f, 0.018f, 0.4f, 0.0003f);
            addSine(v, 0, 6100.0f, 6100.0f, 1.0f, 0.012f, 0.25f, 0.0003f);
            OnePole hp;
            for (size_t i = 0; i < samples(0.003f); ++i) v[i] += hp.hp(rng.bi(), onePoleAlpha(3000.0f)) * 0.5f;
            normalizePeak(v, 0.55f);
            break;
        }
        case UiCue::Alert: {
            v.assign(samples(0.75f), 0.0f);
            for (int k = 0; k < 3; ++k) addSweep(v, samples(0.25f * static_cast<float>(k)), 1760.0f, 1760.0f, 0.12f, 0.5f, 0.8f);
            normalizePeak(v, 0.5f);
            break;
        }
        case UiCue::FreeCam: {
            v.assign(samples(0.4f), 0.0f);
            Svf band;
            for (size_t i = 0; i < v.size(); ++i) {
                const float u = timeOf(i) / 0.4f;
                band.process(rng.bi(), 2000.0f * std::pow(0.4f, u), 1.0f);
                v[i] = band.band * std::sin(kPi * u);
            }
            normalizePeak(v, 0.35f);
            break;
        }
    }
    fadeEdges(v, 0.0005f, 0.01f);
    return v;
}

std::vector<float> synthRifleShot(uint32_t take) {
    Rng rng(take * 40503u + 977u);
    std::vector<float> v(samples(0.7f), 0.0f);
    // supersonic crack: an N-wave (sharp rise, linear fall, sharp return) ~0.35 ms
    const size_t nw = samples(0.00035f);
    for (size_t i = 0; i < nw; ++i) v[i] += 1.4f * (1.0f - 2.0f * static_cast<float>(i) / static_cast<float>(nw));
    // muzzle blast: broadband burst + a body whose low-pass closes quickly + a low boom
    OnePole hp;
    Svf body;
    const float bright = 0.9f + 0.2f * rng.uni();
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = timeOf(i);
        const float n = rng.bi();
        float x = hp.hp(n, onePoleAlpha(400.0f)) * 1.3f * expEnv(t, 0.003f * bright);
        body.process(n, 300.0f + 3500.0f * expEnv(t, 0.02f), 0.9f);
        x += body.lo * 1.1f * expEnv(t, 0.035f) * (1.0f - expEnv(t, 0.0008f));
        x += rng.bi() * 0.06f * expEnv(t, 0.09f);   // gas / crackle tail
        v[i] += x;
    }
    addSine(v, 0, 140.0f, 60.0f, 0.02f, 0.045f, 0.9f, 0.0008f);
    // bolt carrier cycling ~30 ms later: two small metallic clicks
    for (int k = 0; k < 2; ++k) {
        const size_t at = samples(0.028f + 0.018f * static_cast<float>(k) + 0.004f * rng.uni());
        addSine(v, at, 3200.0f, 3200.0f, 1.0f, 0.007f, 0.10f, 0.0002f);
        addSine(v, at, 5100.0f, 5100.0f, 1.0f, 0.005f, 0.07f, 0.0002f);
    }
    normalizePeak(v, 0.95f);
    fadeEdges(v, 0.0f, 0.05f);
    return v;
}

std::vector<float> synthImpact(Surface s, uint32_t take) {
    Rng rng(take * 7907u + static_cast<uint32_t>(s) * 389u + 5u);
    const bool hard = s == Surface::Concrete || s == Surface::Metal;
    const bool ricochet = hard && rng.uni() < 0.45f;
    std::vector<float> v(samples(ricochet ? 0.6f : 0.3f), 0.0f);
    OnePole hp, lp, grit;
    for (size_t i = 0; i < samples(0.12f); ++i) {
        const float t = timeOf(i);
        const float n = rng.bi();
        float x = hp.hp(n, onePoleAlpha(hard ? 2500.0f : 900.0f)) * expEnv(t, hard ? 0.002f : 0.004f);   // the crack
        x += lp.lp(n, onePoleAlpha(hard ? 1200.0f : 500.0f)) * (hard ? 1.0f : 3.0f) * expEnv(t, 0.012f);   // thud
        if (!hard) x += grit.lp(rng.bi(), onePoleAlpha(1800.0f)) * 0.5f * expEnv(t, 0.05f) * (s == Surface::Grass ? 0.4f : 1.0f);   // soil spray
        v[i] += x;
    }
    if (s == Surface::Gravel)
        for (int g = 0; g < 18; ++g) {
            const size_t at = samples(0.08f * rng.uni());
            addSine(v, at, 2500.0f + 4000.0f * rng.uni(), 2500.0f, 1.0f, 0.0015f, 0.15f * rng.uni(), 0.0002f);
        }
    if (s == Surface::Metal) {
        addSine(v, 0, 1830.0f, 1830.0f, 1.0f, 0.12f, 0.35f, 0.0003f);
        addSine(v, 0, 2950.0f, 2950.0f, 1.0f, 0.08f, 0.25f, 0.0003f);
        addSine(v, 0, 4410.0f, 4410.0f, 1.0f, 0.05f, 0.18f, 0.0003f);
    }
    if (ricochet) addSweep(v, samples(0.01f), 3400.0f + 600.0f * rng.uni(), 1300.0f, 0.4f, 0.12f);
    normalizePeak(v, hard ? 0.6f : 0.5f);
    fadeEdges(v, 0.0f, 0.02f);
    return v;
}

}  // namespace ts::audio
