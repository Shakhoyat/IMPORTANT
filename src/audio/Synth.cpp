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
        motor_[i] = rng_.uni() * kTwoPi;
    }
}
void RotorHumGen::render(float* out, int n, float speed, float pitch) {
    if (!primed_) { speed_ = speed; primed_ = true; }
    const float sm = 1.0f - std::exp(-1.0f / (0.05f * kSampleRate));
    // Throttle wander: each rotor's rpm follows its own slow random target (flight controller corrections).
    for (int r = 0; r < 4; ++r) {
        if (rng_.uni() < static_cast<float>(n) / (0.35f * kSampleRate)) jitV_[r] = 0.025f * rng_.bi();
    }
    const float jsm = 1.0f - std::exp(-static_cast<float>(n) / (0.12f * kSampleRate));
    for (int r = 0; r < 4; ++r) jit_[r] += (jitV_[r] - jit_[r]) * jsm;
    for (int i = 0; i < n; ++i) {
        speed_ += sm * (speed - speed_);
        const float sp = std::min(std::max(speed_, 0.0f), 15.0f);
        const float f0 = (105.0f + 7.0f * sp) * pitch;
        wobble_ += kTwoPi * 0.3f / kSampleRate;
        if (wobble_ > kTwoPi) wobble_ -= kTwoPi;
        float s = 0.0f, whine = 0.0f;
        for (int r = 0; r < 4; ++r) {
            const float fr = f0 * detune_[r] * (1.0f + jit_[r]);
            phase_[r] += kTwoPi * fr / kSampleRate;
            if (phase_[r] > kTwoPi) phase_[r] -= kTwoPi;
            float h = 0.0f;
            for (int k = 1; k <= 6; ++k) h += std::sin(phase_[r] * static_cast<float>(k)) * std::pow(static_cast<float>(k), -1.3f);
            s += h;
            motor_[r] += kTwoPi * fr * 7.0f / kSampleRate;   // 14-pole motor: electrical rate = 7 x blade pass
            if (motor_[r] > kTwoPi) motor_[r] -= kTwoPi;
            whine += std::sin(motor_[r]) + 0.3f * std::sin(2.0f * motor_[r]);
        }
        s *= 0.12f * (1.0f + 0.05f * std::sin(wobble_));
        s += whine * 0.006f * (0.6f + 0.4f * sp / 15.0f);
        band_.process(rng_.bi(), 1800.0f * pitch, 1.2f);
        const float chop = 0.65f + 0.35f * std::sin(phase_[0]);   // blade-vortex modulation of the broadband noise
        s += band_.band * 0.10f * (0.5f + 0.5f * sp / 15.0f) * chop;
        s += air_.lp(rng_.bi(), onePoleAlpha(220.0f)) * 0.25f;     // downwash rumble
        out[i] = s;
    }
}

HeliThumpGen::HeliThumpGen(uint32_t seed) : rng_(seed) {}
void HeliThumpGen::render(float* out, int n, float rotorFrac, float pitch) {
    static constexpr float kBlade[4] = {1.0f, 0.82f, 0.93f, 0.76f};   // blade tracking: one blade slaps hardest
    const float sm = 1.0f - std::exp(-1.0f / (0.3f * kSampleRate));
    for (int i = 0; i < n; ++i) {
        frac_ += sm * (rotorFrac - frac_);
        phase_ += 22.0f * frac_ * pitch / kSampleRate;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            blade_ = (blade_ + 1) & 3;
            slapT_ = 0.0f;
            slapAmp_ = kBlade[blade_] * (0.9f + 0.2f * rng_.uni());
            slapPhase_ = 0.0f;
        }
        slapT_ += 1.0f / kSampleRate;
        slapPhase_ += kTwoPi * 58.0f * pitch / kSampleRate;
        // thump (low, 45 ms) + blade slap (band-limited crack, 10 ms) + wash
        float s = std::sin(slapPhase_) * expEnv(slapT_, 0.045f) * slapAmp_;
        s += lpA_.lp(rng_.bi(), onePoleAlpha(900.0f)) * expEnv(slapT_, 0.012f) * 1.2f * slapAmp_;
        slapBand_.process(rng_.bi(), 1400.0f * pitch, 1.1f);
        s += slapBand_.band * expEnv(slapT_, 0.008f) * 0.9f * slapAmp_ * frac_;
        s *= 0.9f * std::sqrt(frac_);
        // tail rotor: 5.3 x faster buzz with harmonics
        tail_ += kTwoPi * 22.0f * 5.3f * frac_ * pitch / kSampleRate;
        if (tail_ > kTwoPi) tail_ -= kTwoPi;
        s += 0.05f * frac_ * (std::sin(tail_) + 0.5f * std::sin(2.0f * tail_) + 0.25f * std::sin(3.0f * tail_));
        // turbine: two whine tones beating slowly + jet noise
        whine_ += kTwoPi * 2400.0f * (0.4f + 0.6f * frac_) * pitch / kSampleRate;
        if (whine_ > kTwoPi) whine_ -= kTwoPi;
        whine2_ += kTwoPi * 3730.0f * (0.4f + 0.6f * frac_) * pitch / kSampleRate;
        if (whine2_ > kTwoPi) whine2_ -= kTwoPi;
        s += frac_ * (0.02f * std::sin(whine_) + 0.008f * std::sin(whine2_));
        s += hpJet_.hp(rng_.bi(), onePoleAlpha(1500.0f)) * 0.03f * frac_;
        s += lpB_.lp(rng_.bi(), onePoleAlpha(400.0f)) * 0.35f * frac_;
        out[i] = s;
    }
}

TankEngineGen::TankEngineGen(uint32_t seed) : rng_(seed) {
    for (float& a : cylAmp_) a = 0.8f + 0.4f * rng_.uni();   // per-cylinder imbalance: the lumpy diesel note
}
void TankEngineGen::render(float* out, int n, float speed, float pitch) {
    const float sm = 1.0f - std::exp(-1.0f / (0.25f * kSampleRate));
    const float aPulse = onePoleAlpha(420.0f), aNoise = onePoleAlpha(160.0f), aThud = onePoleAlpha(250.0f);
    for (int i = 0; i < n; ++i) {
        speed_ += sm * (speed - speed_);
        load_ += sm * 1.5f * (loadTarget_ - load_);
        const float sp = clampf(std::fabs(speed_), 0.0f, 8.0f);
        const float load = std::max(sp / 8.0f, load_);
        const float rpm = 650.0f + 220.0f * sp + 600.0f * load_;   // the driver's throttle revs the engine ahead
        const float fireHz = rpm / 60.0f * 6.0f * pitch;   // V12 four-stroke: 6 firings per crank revolution
        fire_ += fireHz / kSampleRate;
        if (fire_ >= 1.0f) { fire_ -= 1.0f; cyl_ = (cyl_ + 1) % 12; }
        rev_ += kTwoPi * rpm / 120.0f * pitch / kSampleRate;   // half-order (cam) rumble
        if (rev_ > kTwoPi) rev_ -= kTwoPi;
        const float pulse = std::exp(-fire_ * 7.0f) * cylAmp_[cyl_] * (1.0f + 0.15f * rng_.bi());
        float s = std::tanh(lpPulse_.lp(pulse * 2.2f - 0.3f, aPulse) * (1.4f + 0.8f * load)) * 0.55f;
        s += 0.12f * std::sin(rev_);
        s += lpNoise_.lp(rng_.bi(), aNoise) * 2.2f * (0.5f + 0.5f * load);   // exhaust
        turbo_ += kTwoPi * (1800.0f + 280.0f * sp) * pitch / kSampleRate;
        if (turbo_ > kTwoPi) turbo_ -= kTwoPi;
        s += 0.035f * (0.25f + load) * std::sin(turbo_);
        // tracks: one link engages the sprocket every 0.16 m
        const float drive = clampf(sp / 1.5f, 0.0f, 1.0f);
        if (drive > 0.02f) {
            link_ += sp / 0.16f / kSampleRate;
            if (link_ >= 1.0f) { link_ -= 1.0f; clank_ = 1.0f; clankAmp_ = (0.6f + 0.4f * rng_.uni()) * drive; }
        }
        clank_ *= 0.9965f;   // ~6 ms
        const float exc = rng_.bi() * clank_ * clankAmp_;
        res1_.process(exc, 1250.0f * pitch, 9.0f);
        res2_.process(exc, 2380.0f * pitch, 12.0f);
        s += (res1_.band * 0.9f + res2_.band * 0.6f) * 0.9f;
        s += thud_.lp(exc, aThud) * 1.6f;
        s += rumble_.lp(rng_.bi(), onePoleAlpha(90.0f)) * 3.0f * drive * (0.4f + 0.6f * load);
        // squeal: intermittent narrow-band screech while moving
        if (drive > 0.3f && rng_.uni() < 0.00002f) squealEnv_ = 1.0f;
        squealEnv_ *= 0.99995f;
        if (squealEnv_ > 1e-3f) {
            squeal_.process(rng_.bi(), 3150.0f * pitch, 25.0f);
            s += squeal_.band * 0.5f * squealEnv_ * drive;
        }
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

}  // namespace ts::audio
