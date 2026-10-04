#include "audio/Synth.h"

#include <algorithm>
#include <cmath>

namespace ts::audio {

namespace {

inline size_t samples(float seconds) { return static_cast<size_t>(seconds * kSampleRate); }
inline float timeOf(size_t i) { return static_cast<float>(i) / kSampleRate; }

void normalize(std::vector<float>& v, float peak) {
    float m = 0.0f;
    for (float s : v) m = std::max(m, std::fabs(s));
    if (m < 1e-9f) return;
    const float g = peak / m;
    for (float& s : v) s *= g;
}
void fadeOut(std::vector<float>& v, float seconds) {
    const size_t n = std::min(v.size(), samples(seconds));
    for (size_t i = 0; i < n; ++i) v[v.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(n);
}
void fadeIn(std::vector<float>& v, float seconds) {
    const size_t n = std::min(v.size(), samples(seconds));
    for (size_t i = 0; i < n; ++i) v[i] *= static_cast<float>(i) / static_cast<float>(n);
}

// Adds a decaying sine "thump" with a pitch drop starting at sample 'at'.
void addBoom(std::vector<float>& v, size_t at, float f0, float f1, float pitchTau, float tau, float amp) {
    float phase = 0.0f;
    const size_t len = std::min(v.size() - at, samples(tau * 6.0f));
    for (size_t i = 0; i < len; ++i) {
        const float t = timeOf(i);
        const float f = f1 + (f0 - f1) * expEnv(t, pitchTau);
        phase += kTwoPi * f / kSampleRate;
        if (phase > kTwoPi) phase -= kTwoPi;
        v[at + i] += amp * std::sin(phase) * expEnv(t, tau) * (1.0f - expEnv(t, 0.003f));
    }
}

}  // namespace

// --------------------------------------------------------------------------------------------- explosion
std::vector<float> synthExplosion(float power, uint32_t seed) {
    const float p = clampf(power, 0.05f, 4.0f);
    const float dur = std::min(7.0f, 2.0f + 1.5f * p);
    std::vector<float> v(samples(dur), 0.0f);
    Rng rng(seed);
    OnePole hpCrack, lp1, lp2, lp3, lp4;
    Svf body;
    const float aHp = onePoleAlpha(1500.0f), aLow = onePoleAlpha(90.0f);
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = timeOf(i);
        float s = 0.0f;
        // 1. crack: very short broadband burst, high-passed, with a slightly longer grit tail
        const float n1 = rng.bi();
        s += hpCrack.hp(n1, aHp) * (expEnv(t, 0.006f) * 1.2f + expEnv(t, 0.035f) * 0.35f);
        // 3. body: noise through a low-pass that closes from 4 kHz to ~250 Hz
        body.process(rng.bi(), 250.0f + 4000.0f * expEnv(t, 0.22f), 0.8f);
        s += body.lo * 1.4f * expEnv(t, 0.5f + 0.3f * p) * (1.0f - expEnv(t, 0.004f));
        // 4. rumble tail: heavily low-passed noise, slow tremolo, long decay
        const float n4 = lp2.lp(lp1.lp(rng.bi(), aLow), aLow);
        s += n4 * 22.0f * expEnv(t, 1.0f + 0.7f * p) * (1.0f + 0.3f * std::sin(kTwoPi * 7.0f * t)) *
             (1.0f - expEnv(t, 0.05f));
        v[i] = s;
    }
    (void)lp3; (void)lp4;
    // 2. boom: low sine with a pitch drop 100 -> 30 Hz
    addBoom(v, 0, 100.0f, 30.0f, 0.25f, 0.35f + 0.25f * p, 1.6f);
    normalize(v, clampf(0.3f + 0.45f * std::sqrt(p), 0.2f, 1.2f));
    fadeOut(v, 0.08f);
    return v;
}

std::vector<float> synthCannon(uint32_t seed) {
    std::vector<float> v(samples(1.4f), 0.0f);
    Rng rng(seed);
    OnePole hp, lp1, lp2;
    Svf body;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = timeOf(i);
        float s = hp.hp(rng.bi(), onePoleAlpha(1800.0f)) * 1.3f * expEnv(t, 0.004f);
        body.process(rng.bi(), 200.0f + 1500.0f * expEnv(t, 0.12f), 0.9f);
        s += body.lo * 1.2f * expEnv(t, 0.16f);
        s += lp2.lp(lp1.lp(rng.bi(), onePoleAlpha(130.0f)), onePoleAlpha(130.0f)) * 18.0f * expEnv(t, 0.45f);
        v[i] = s;
    }
    addBoom(v, 0, 95.0f, 42.0f, 0.12f, 0.18f, 1.5f);
    normalize(v, 1.0f);
    fadeOut(v, 0.05f);
    return v;
}

std::vector<float> synthGatling(uint32_t seed) {
    std::vector<float> v(samples(1.4f), 0.0f);
    Rng rng(seed);
    float next = 0.0f;
    OnePole hp;
    float phaseWhirr = 0.0f;
    // Pre-compute shot times (spin-up from ~17 to 55 shots/s within 0.2 s, jitter +-6 %).
    std::vector<size_t> shots;
    while (next < 1.3f) {
        shots.push_back(static_cast<size_t>(next * kSampleRate));
        const float rate = 55.0f * clampf(0.3f + next / 0.2f * 0.7f, 0.3f, 1.0f);
        next += (1.0f / rate) * (1.0f + 0.06f * rng.bi());
    }
    for (size_t s0 : shots) {
        const float a = 0.75f + 0.25f * rng.uni();
        const size_t len = std::min(v.size() - s0, samples(0.03f));
        OnePole h;
        float ph = 0.0f;
        for (size_t i = 0; i < len; ++i) {
            const float t = timeOf(i);
            ph += kTwoPi * 150.0f / kSampleRate;
            float click = h.hp(rng.bi(), onePoleAlpha(2500.0f)) * expEnv(t, 0.0025f) * 1.2f;
            click += std::sin(ph) * expEnv(t, 0.006f) * 0.7f;
            click += rng.bi() * 0.25f * expEnv(t, 0.012f);
            v[s0 + i] += a * click;
        }
    }
    for (size_t i = 0; i < v.size(); ++i) {  // motor whirr under the burst
        phaseWhirr += kTwoPi * 310.0f / kSampleRate;
        v[i] += 0.12f * std::sin(phaseWhirr) * clampf(timeOf(i) / 0.2f, 0.0f, 1.0f);
    }
    (void)hp;
    normalize(v, 0.8f);
    fadeIn(v, 0.002f);
    fadeOut(v, 0.06f);
    return v;
}

std::vector<float> synthWhoosh(uint32_t seed) {
    const float dur = 2.0f;
    std::vector<float> v(samples(dur), 0.0f);
    Rng rng(seed);
    Svf band;
    OnePole crackleHp;
    float crackle = 0.0f;
    for (size_t i = 0; i < v.size(); ++i) {
        const float u = timeOf(i) / dur;
        const float bump = u < 0.3f ? std::sin(0.5f * kPi * u / 0.3f) : std::pow(std::cos(0.5f * kPi * (u - 0.3f) / 0.7f), 1.5f);
        band.process(rng.bi(), 350.0f + 2400.0f * bump, 1.4f);
        float s = band.band * (0.15f + bump) * 1.2f;
        // crackle: random impulses (density falls with time) ringing as short hp noise bursts
        if (rng.uni() < 0.0018f * (1.0f - u)) crackle = 0.5f + 0.5f * rng.uni();
        crackle *= 0.985f;
        s += crackleHp.hp(rng.bi(), onePoleAlpha(2000.0f)) * crackle * 0.5f;
        v[i] = s;
    }
    normalize(v, 0.8f);
    fadeIn(v, 0.01f);
    fadeOut(v, 0.15f);
    return v;
}

std::vector<float> synthDoor(uint32_t seed) {
    std::vector<float> v(samples(2.9f), 0.0f);
    Rng rng(seed);
    float ph = 0.0f, phW = 0.0f;
    OnePole lp;
    for (size_t i = 0; i < 2.35f * kSampleRate; ++i) {
        const float t = timeOf(i);
        const float f = 70.0f * clampf(0.5f + t / 0.4f * 0.5f, 0.5f, 1.0f) * (t > 2.0f ? 1.0f - (t - 2.0f) * 0.8f : 1.0f);
        ph += kTwoPi * f / kSampleRate;
        float s = 0.0f;
        for (int h = 1; h <= 5; ++h) s += std::sin(ph * static_cast<float>(h)) / static_cast<float>(h);
        phW += kTwoPi * (6.0f * f) / kSampleRate;
        s += 0.12f * std::sin(phW) + 0.5f * lp.lp(rng.bi(), onePoleAlpha(300.0f)) * 3.0f;
        const float env = clampf(t / 0.25f, 0.0f, 1.0f) * (t > 2.0f ? clampf((2.35f - t) / 0.35f, 0.0f, 1.0f) : 1.0f);
        v[i] = 0.35f * s * env;
    }
    const size_t at = samples(2.35f);
    addBoom(v, at, 70.0f, 45.0f, 0.04f, 0.10f, 1.4f);
    float p1 = 0.0f, p2 = 0.0f;
    for (size_t i = 0; at + i < v.size() && i < samples(0.5f); ++i) {
        const float t = timeOf(i);
        p1 += kTwoPi * 430.0f / kSampleRate;
        p2 += kTwoPi * 790.0f / kSampleRate;
        v[at + i] += 0.25f * (std::sin(p1) + 0.6f * std::sin(p2)) * expEnv(t, 0.12f) + rng.bi() * 0.5f * expEnv(t, 0.012f);
    }
    normalize(v, 0.8f);
    fadeOut(v, 0.1f);
    return v;
}

std::vector<float> synthSiren(uint32_t) {
    std::vector<float> v(samples(6.0f), 0.0f);
    float ph = 0.0f;
    OnePole lp;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = timeOf(i);
        const float cyc = std::fmod(t / 2.4f, 1.0f);
        const float tri = cyc < 0.5f ? cyc * 2.0f : 2.0f - cyc * 2.0f;
        ph += kTwoPi * (650.0f + 350.0f * tri) / kSampleRate;
        if (ph > kTwoPi) ph -= kTwoPi;
        const float s = std::sin(ph) + 0.33f * std::sin(3.0f * ph) + 0.2f * std::sin(5.0f * ph);
        v[i] = lp.lp(s, onePoleAlpha(3500.0f));
    }
    normalize(v, 0.7f);
    fadeIn(v, 0.05f);
    fadeOut(v, 0.3f);
    return v;
}

std::vector<float> synthAlarm(uint32_t) {
    std::vector<float> v(samples(1.6f), 0.0f);
    float ph = 0.0f;
    for (size_t i = 0; i < v.size(); ++i) {
        const float t = timeOf(i);
        ph += kTwoPi * ((static_cast<int>(t / 0.4f) % 2 == 0) ? 820.0f : 610.0f) / kSampleRate;
        if (ph > kTwoPi) ph -= kTwoPi;
        const float sq = std::sin(ph) + 0.4f * std::sin(3.0f * ph) + 0.2f * std::sin(5.0f * ph);
        v[i] = sq * edge(static_cast<int>(i % samples(0.4f)), static_cast<int>(samples(0.4f)), 96);
    }
    normalize(v, 0.7f);
    return v;
}

namespace {
void addTone(std::vector<float>& v, size_t at, float f0, float f1, float seconds, float amp, float decayTau) {
    const size_t len = std::min(v.size() - at, samples(seconds));
    float ph = 0.0f;
    for (size_t i = 0; i < len; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(len);
        ph += kTwoPi * lerpf(f0, f1, u) / kSampleRate;
        float env = edge(static_cast<int>(i), static_cast<int>(len), 240);
        if (decayTau > 0.0f) env *= expEnv(timeOf(i), decayTau);
        v[at + i] += amp * env * (std::sin(ph) + 0.2f * std::sin(2.0f * ph));
    }
}
}  // namespace

std::vector<float> synthLockTone() {
    std::vector<float> v(samples(0.3f), 0.0f);
    addTone(v, 0, 1250.0f, 1250.0f, 0.08f, 0.5f, 0.0f);
    addTone(v, samples(0.15f), 1250.0f, 1250.0f, 0.08f, 0.5f, 0.0f);
    return v;
}
std::vector<float> synthConfirmChirp() {
    std::vector<float> v(samples(0.18f), 0.0f);
    addTone(v, 0, 900.0f, 1900.0f, 0.13f, 0.5f, 0.0f);
    return v;
}
std::vector<float> synthKillConfirm() {
    std::vector<float> v(samples(0.6f), 0.0f);
    addTone(v, 0, 880.0f, 880.0f, 0.09f, 0.45f, 0.0f);
    addTone(v, samples(0.1f), 1175.0f, 1175.0f, 0.09f, 0.45f, 0.0f);
    addTone(v, samples(0.2f), 1568.0f, 1568.0f, 0.3f, 0.5f, 0.12f);
    return v;
}

// ------------------------------------------------------------------------------------------ continuous
RotorHumGen::RotorHumGen(uint32_t seed) : rng_(seed) {
    for (int i = 0; i < 4; ++i) {
        detune_[i] = 1.0f + 0.012f * (static_cast<float>(i) - 1.5f) + 0.004f * rng_.bi();
        phase_[i] = rng_.uni() * kTwoPi;
    }
}
void RotorHumGen::render(float* out, int n, float speed, float pitch) {
    if (!primed_) { speed_ = speed; primed_ = true; }
    const float sm = 1.0f - std::exp(-1.0f / (0.05f * kSampleRate));
    for (int i = 0; i < n; ++i) {
        speed_ += sm * (speed - speed_);
        const float sp = std::min(std::max(speed_, 0.0f), 15.0f);
        const float f0 = (105.0f + 7.0f * sp) * pitch;
        wobble_ += kTwoPi * 0.3f / kSampleRate;
        if (wobble_ > kTwoPi) wobble_ -= kTwoPi;
        float s = 0.0f;
        for (int r = 0; r < 4; ++r) {
            phase_[r] += kTwoPi * f0 * detune_[r] / kSampleRate;
            if (phase_[r] > kTwoPi) phase_[r] -= kTwoPi;
            float h = 0.0f;
            for (int k = 1; k <= 6; ++k) h += std::sin(phase_[r] * static_cast<float>(k)) * std::pow(static_cast<float>(k), -1.3f);
            s += h;
        }
        s *= 0.12f * (1.0f + 0.05f * std::sin(wobble_));
        band_.process(rng_.bi(), 1800.0f * pitch, 1.2f);
        s += band_.band * 0.10f * (0.5f + 0.5f * sp / 15.0f);
        out[i] = s;
    }
}

HeliThumpGen::HeliThumpGen(uint32_t seed) : rng_(seed) {}
void HeliThumpGen::render(float* out, int n, float rotorFrac, float pitch) {
    const float sm = 1.0f - std::exp(-1.0f / (0.3f * kSampleRate));
    for (int i = 0; i < n; ++i) {
        frac_ += sm * (rotorFrac - frac_);
        phase_ += 25.0f * frac_ * pitch / kSampleRate;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            slapT_ = 0.0f;
            slapAmp_ = 0.85f + 0.3f * rng_.uni();
            slapPhase_ = 0.0f;
        }
        slapT_ += 1.0f / kSampleRate;
        slapPhase_ += kTwoPi * 55.0f / kSampleRate;
        float s = std::sin(slapPhase_) * expEnv(slapT_, 0.045f) * slapAmp_;
        s += lpA_.lp(rng_.bi(), onePoleAlpha(900.0f)) * expEnv(slapT_, 0.012f) * 1.2f * slapAmp_;
        s *= 0.9f * std::sqrt(frac_);
        whine_ += kTwoPi * 2400.0f * (0.4f + 0.6f * frac_) / kSampleRate;
        if (whine_ > kTwoPi) whine_ -= kTwoPi;
        s += 0.02f * frac_ * std::sin(whine_);
        s += lpB_.lp(rng_.bi(), onePoleAlpha(400.0f)) * 0.35f * frac_;
        out[i] = s;
    }
}

TankEngineGen::TankEngineGen(uint32_t seed) : rng_(seed) {}
void TankEngineGen::render(float* out, int n, float speed, float pitch) {
    const float sm = 1.0f - std::exp(-1.0f / (0.25f * kSampleRate));
    for (int i = 0; i < n; ++i) {
        speed_ += sm * (speed - speed_);
        const float sp = clampf(speed_, 0.0f, 8.0f);
        phase_ += (16.0f + 3.0f * sp) * pitch / kSampleRate;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        mod_ += kTwoPi * (16.0f + 3.0f * sp) / kSampleRate;
        if (mod_ > kTwoPi) mod_ -= kTwoPi;
        float s = lpPulse_.lp(std::exp(-phase_ * 8.0f) * 2.0f - 0.25f, onePoleAlpha(300.0f)) * 1.6f;
        s += lpNoise_.lp(rng_.bi(), onePoleAlpha(130.0f)) * 3.0f * (0.7f + 0.3f * std::cos(mod_));
        const float drive = clampf(sp / 4.0f, 0.0f, 1.0f);
        if (drive > 0.05f && rng_.uni() < 0.004f * (1.0f + sp)) {
            s += hpClatter_.hp(rng_.bi(), onePoleAlpha(1200.0f)) * 0.5f * drive;
        } else {
            hpClatter_.hp(0.0f, onePoleAlpha(1200.0f));
        }
        s += lpClatter_.lp(rng_.bi(), onePoleAlpha(2500.0f)) * 0.25f * drive;
        out[i] = 0.6f * s;
    }
}

RocketGen::RocketGen(uint32_t seed) : rng_(seed) {}
void RocketGen::render(float* out, int n, float kind, float pitch) {
    for (int i = 0; i < n; ++i) {
        const float x = rng_.bi();
        float s;
        if (kind < 0.5f) {
            band_.process(x, 900.0f * pitch, 0.8f);
            s = band_.band * 0.5f + lpB_.lp(lpA_.lp(x, onePoleAlpha(250.0f)), onePoleAlpha(250.0f)) * 3.5f;
        } else {
            band_.process(x, 2200.0f * pitch, 4.0f);
            s = band_.band * 0.9f;
        }
        out[i] = 0.45f * s;
    }
}

AmbientGen::AmbientGen(uint32_t seed) : rng_(seed) {
    Rng r(seed + 7);
    for (int i = 0; i < 7; ++i) {
        Cricket& c = crickets_[i];
        c.freq = 4300.0f + 140.0f * static_cast<float>(i) + 40.0f * r.bi();
        c.phase = 0.0f;
        c.period = 0.30f + 0.035f * static_cast<float>(i) + 0.02f * r.uni();
        c.t = r.uni() * c.period;
        c.amp = 0.010f + 0.010f * r.uni();
        const float pan = 0.9f * r.bi();
        float l, rr;
        const float a = (pan + 1.0f) * 0.25f * kPi;
        l = std::cos(a); rr = std::sin(a);
        c.panL = l; c.panR = rr;
        c.on = (i % 2 == 0);
        c.onT = 0.0f;
        c.offT = 3.0f + 5.0f * r.uni();   // time left in the current on / off state
        c.rs = static_cast<uint32_t>(seed * 31u + i * 7919u + 13u);
    }
}
void AmbientGen::render(float* out, int n, float gain) {
    const float aGen = onePoleAlpha(220.0f);
    for (int i = 0; i < n; ++i) {
        t_ += 1.0f / kSampleRate;
        const float gust = clampf(0.5f + 0.5f * (0.5f * std::sin(kTwoPi * 0.07f * t_) + 0.3f * std::sin(kTwoPi * 0.13f * t_ + 1.3f) +
                                                 0.2f * std::sin(kTwoPi * 0.31f * t_ + 2.1f)), 0.0f, 1.0f);
        const float fc = 250.0f + 350.0f * gust;
        const float wa = 0.10f + 0.18f * gust * gust;
        windL_.process(rng_.bi(), fc, 0.7f);
        windR_.process(rng_.bi(), fc * 1.04f, 0.7f);
        float l = windL_.band * wa * 1.6f, r = windR_.band * wa * 1.6f;
        // distant generator: 50 Hz mains-driven hum with harmonics, panned right
        genPhase_ += kTwoPi * 50.0f / kSampleRate;
        if (genPhase_ > kTwoPi) genPhase_ -= kTwoPi;
        const float g = genLp_.lp(std::sin(genPhase_ * 2.0f) + 0.5f * std::sin(genPhase_ * 4.0f) + 0.3f * std::sin(genPhase_ * 6.0f), aGen) *
                        0.03f * (1.0f + 0.1f * std::sin(kTwoPi * 0.4f * t_));
        l += g * 0.4f; r += g * 0.9f;
        for (Cricket& c : crickets_) {
            c.offT -= 1.0f / kSampleRate;
            if (c.offT <= 0.0f) {
                c.on = !c.on;
                c.rs = c.rs * 1664525u + 1013904223u;
                const float u = static_cast<float>(c.rs >> 8) * (1.0f / 16777216.0f);
                c.offT = c.on ? 4.0f + 5.0f * u : 2.0f + 4.0f * u;
            }
            c.onT += ((c.on ? 1.0f : 0.0f) - c.onT) * 0.0005f;  // fade the train in / out
            if (c.onT < 1e-3f) continue;
            c.t += 1.0f / kSampleRate;
            if (c.t >= c.period) c.t -= c.period;
            // chirp = 3 pulses of 14 ms every 29 ms at the start of each period
            float env = 0.0f;
            if (c.t < 0.087f) {
                const float tp = std::fmod(c.t, 0.029f);
                if (tp < 0.014f) { const float w = std::sin(kPi * tp / 0.014f); env = w * w; }
            }
            if (env <= 0.0f) continue;
            c.phase += kTwoPi * c.freq / kSampleRate;
            if (c.phase > kTwoPi) c.phase -= kTwoPi;
            const float s = std::sin(c.phase) * env * c.amp * c.onT;
            l += s * c.panL; r += s * c.panR;
        }
        out[2 * i] += l * gain;
        out[2 * i + 1] += r * gain;
    }
}

}  // namespace ts::audio
