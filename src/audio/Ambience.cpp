#include "audio/Ambience.h"

#include <algorithm>
#include <cmath>

namespace ts::audio {

namespace {
void panOf(float pan, float& l, float& r) {
    const float a = (clampf(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    l = std::cos(a);
    r = std::sin(a);
}
}  // namespace

AmbientGen::AmbientGen(uint32_t seed) : rng_(seed) {
    Rng r(seed + 7);
    for (int i = 0; i < 7; ++i) {
        Cricket& c = crickets_[i];
        c.freq = 4300.0f + 140.0f * static_cast<float>(i) + 40.0f * r.bi();
        c.phase = 0.0f;
        c.period = 0.30f + 0.035f * static_cast<float>(i) + 0.02f * r.uni();
        c.t = r.uni() * c.period;
        c.amp = 0.022f + 0.020f * r.uni();   // v7: crickets carry the night (v5: 0.010 + 0.010 u)
        const float pan = 0.9f * r.bi();
        panOf(pan, c.panL, c.panR);
        c.on = (i % 2 == 0);
        c.onT = 0.0f;
        c.offT = 3.0f + 5.0f * r.uni();   // time left in the current on / off state
        c.rs = static_cast<uint32_t>(seed * 31u + i * 7919u + 13u);
    }
    for (int i = 0; i < 4; ++i) {
        Bird& b = birds_[i];
        b.rng = Rng(seed * 131u + 17u * static_cast<uint32_t>(i) + 5u);
        b.wait = 0.4f + 2.5f * b.rng.uni();
    }
    for (int i = 0; i < 2; ++i) {
        Cicada& c = cicadas_[i];
        c.rng = Rng(seed * 977u + 31u * static_cast<uint32_t>(i) + 3u);
        c.freq = 4900.0f + 700.0f * c.rng.uni();
        c.rate = 140.0f + 50.0f * c.rng.uni();
        c.swellLen = 8.0f + 10.0f * c.rng.uni();
        c.swellT = c.rng.uni() * c.swellLen;
        panOf(i == 0 ? -0.6f : 0.55f, c.panL, c.panR);
    }
}

void AmbientGen::setTimeOfDay(float hour, float daylight) {
    hour_ = std::isfinite(hour) ? std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f) : 23.0f;
    daylight_ = std::isfinite(daylight) ? clampf(daylight, 0.0f, 1.0f) : 0.0f;
}
void AmbientGen::setUnderground(float amount) { ugTarget_ = std::isfinite(amount) ? clampf(amount, 0.0f, 1.0f) : 0.0f; }

// Builds one call (a short note sequence) for a species that fits the time of day.
void AmbientGen::startCall(Bird& b) {
    Rng& r = b.rng;
    const bool day = wDay_ > 0.3f;
    const bool night = wDay_ < 0.15f;
    if (!day && !night) {   // deep twilight: mostly quiet, an occasional early bird
        if (r.uni() < 0.7f) { b.wait = 2.0f + 4.0f * r.uni(); return; }
    }
    if (!night) {   // day, or an early / late bird in the twilight
        const float u = r.uni();
        const float koel = 0.18f + 0.25f * wDawn_;   // koels dominate the dawn chorus
        b.species = u < koel ? Species::Koel : u < koel + 0.3f ? Species::Myna : u < koel + 0.6f ? Species::Sparrow
                                                                                       : Species::Warbler;
    } else {
        if (r.uni() < 0.55f) { b.wait = 8.0f + 14.0f * r.uni(); return; }   // night: long silences
        b.species = r.uni() < 0.5f ? Species::Owl : Species::Gecko;
    }
    b.count = 0;
    float t = 0.0f;
    auto add = [&](float dur, float f0, float f1, float amp, float vibHz = 0.0f, float vibDepth = 0.0f) {
        if (b.count >= 16) return;
        b.notes[b.count++] = {t, dur, f0, f1, amp, vibHz, vibDepth};
    };
    b.h2 = 0.08f; b.h3 = 0.0f;
    switch (b.species) {
        case Species::Koel: {   // "ku-OO ku-OO ..." rising with every repeat
            const int reps = 3 + static_cast<int>(r.uni() * 4.0f);
            const float base = 780.0f + 120.0f * r.uni();
            for (int k = 0; k < reps && b.count < 15; ++k) {
                const float lift = 1.0f + 0.07f * static_cast<float>(k);
                add(0.12f, base * lift, base * lift * 1.05f, 0.7f); t += 0.15f;
                add(0.30f, base * 1.32f * lift, base * 1.42f * lift, 1.0f, 5.0f, 0.004f); t += 0.62f;
            }
            b.h2 = 0.12f;
            break;
        }
        case Species::Myna: {   // varied chatter, rough harmonics
            const int n = 5 + static_cast<int>(r.uni() * 7.0f);
            for (int k = 0; k < n; ++k) {
                const float f = 1700.0f + 2600.0f * r.uni();
                const float d = 0.04f + 0.06f * r.uni();
                add(d, f, f * (0.8f + 0.4f * r.uni()), 0.6f + 0.4f * r.uni(), 40.0f, 0.03f);
                t += d + 0.02f + 0.06f * r.uni();
            }
            b.h2 = 0.25f; b.h3 = 0.12f;
            break;
        }
        case Species::Sparrow: {   // "chip chip chip": fast downward sweeps
            const int n = 3 + static_cast<int>(r.uni() * 4.0f);
            const float f = 4200.0f + 900.0f * r.uni();
            for (int k = 0; k < n; ++k) { add(0.045f, f, f * 0.62f, 0.9f); t += 0.13f + 0.05f * r.uni(); }
            break;
        }
        case Species::Warbler: {   // quick melodic trill with vibrato
            const int n = 8 + static_cast<int>(r.uni() * 7.0f);
            float f = 2600.0f + 1200.0f * r.uni();
            for (int k = 0; k < n; ++k) {
                const float f2 = clampf(f + 900.0f * r.bi(), 2200.0f, 5200.0f);
                add(0.055f, f, f2, 0.8f, 28.0f, 0.02f);
                f = f2;
                t += 0.07f;
            }
            break;
        }
        case Species::Owl: {   // "hoo ... hoo-hoo"
            const float f = 360.0f + 40.0f * r.uni();
            add(0.38f, f, f * 0.95f, 1.0f); t += 0.85f;
            add(0.22f, f * 1.02f, f, 0.8f); t += 0.3f;
            add(0.45f, f, f * 0.93f, 0.9f);
            b.h2 = 0.05f;
            break;
        }
        case Species::Gecko: {   // tokay: a croak, then "to-KAY" pairs
            const int n = 4 + static_cast<int>(r.uni() * 4.0f);
            add(0.25f, 210.0f, 190.0f, 0.6f, 35.0f, 0.08f); t += 0.6f;
            for (int k = 0; k < n && b.count < 15; ++k) {
                add(0.11f, 420.0f, 520.0f, 0.9f); t += 0.16f;
                add(0.22f, 470.0f, 330.0f, 0.75f); t += 0.85f;
            }
            b.h2 = 0.45f; b.h3 = 0.3f;
            break;
        }
    }
    const Note& last = b.notes[b.count - 1];
    b.length = last.start + last.dur;
    b.t = 0.0f;
    b.active = true;
    b.phase = 0.0f;
    // A bird somewhere in the treeline: random side, distance -> level and dullness.
    panOf(0.95f * r.bi(), b.panL, b.panR);
    const float dist = r.uni();
    const bool nightBird = b.species == Species::Owl || b.species == Species::Gecko;
    b.amp = (nightBird ? 0.040f : 0.045f) * (1.0f - 0.6f * dist);
    b.lpAlpha = onePoleAlpha(nightBird ? 1800.0f : 9000.0f - 5000.0f * dist);
}

void AmbientGen::render(float* out, int n, float gain) {
    const float aGen = onePoleAlpha(220.0f);
    // Layer weights glide once per block (2 s for time of day, 0.3 s for the bunker).
    const float kDay = 1.0f - std::exp(-static_cast<float>(n) / (2.0f * kSampleRate));
    const float kUg = 1.0f - std::exp(-static_cast<float>(n) / (0.3f * kSampleRate));
    wDay_ += (daylight_ - wDay_) * kDay;
    const float dawnBump = std::exp(-((hour_ - 6.3f) / 1.0f) * ((hour_ - 6.3f) / 1.0f));
    wDawn_ += (dawnBump * daylight_ - wDawn_) * kDay;
    ug_ += (ugTarget_ - ug_) * kUg;
    const float wNight = 1.0f - wDay_;
    const float midday = std::exp(-((hour_ - 13.5f) / 3.0f) * ((hour_ - 13.5f) / 3.0f));
    const float wCicada = wDay_ * (0.25f + 0.75f * midday);
    const float ugCut = onePoleAlpha(20000.0f * std::pow(250.0f / 20000.0f, ug_));
    const float ugGain = 1.0f - 0.72f * ug_;
    const float dt = 1.0f / kSampleRate;
    for (int i = 0; i < n; ++i) {
        t_ += dt;
        const float gust = clampf(0.5f + 0.5f * (0.5f * std::sin(kTwoPi * 0.07f * t_) + 0.3f * std::sin(kTwoPi * 0.13f * t_ + 1.3f) +
                                                 0.2f * std::sin(kTwoPi * 0.31f * t_ + 2.1f)), 0.0f, 1.0f);
        const float fc = 250.0f + 350.0f * gust * (1.0f + 0.4f * wDay_);
        const float wa = (0.10f + 0.18f * gust * gust) * (1.0f + 0.35f * wDay_);
        windL_.process(rng_.bi(), fc, 0.7f);
        windR_.process(rng_.bi(), fc * 1.04f, 0.7f);
        // v7: a gentle 1.1 kHz low-pass keeps the wind out of the birds' / insects' bands
        windLpL_.process(windL_.band, 1100.0f, 0.7071f);
        windLpR_.process(windR_.band, 1100.0f, 0.7071f);
        float l = windLpL_.lo * wa * 1.6f, r = windLpR_.lo * wa * 1.6f;
        // distant generator: 50 Hz mains-driven hum with harmonics, panned right; masked by day
        genPhase_ += kTwoPi * 50.0f / kSampleRate;
        if (genPhase_ > kTwoPi) genPhase_ -= kTwoPi;
        const float g = genLp_.lp(std::sin(genPhase_ * 2.0f) + 0.5f * std::sin(genPhase_ * 4.0f) + 0.3f * std::sin(genPhase_ * 6.0f), aGen) *
                        0.03f * (1.0f + 0.1f * std::sin(kTwoPi * 0.4f * t_)) * (1.0f - 0.5f * wDay_);
        l += g * 0.4f; r += g * 0.9f;
        if (wNight > 1e-3f) {
            for (Cricket& c : crickets_) {
                c.offT -= dt;
                if (c.offT <= 0.0f) {
                    c.on = !c.on;
                    c.rs = c.rs * 1664525u + 1013904223u;
                    const float u = static_cast<float>(c.rs >> 8) * (1.0f / 16777216.0f);
                    c.offT = c.on ? 4.0f + 5.0f * u : 2.0f + 4.0f * u;
                }
                c.onT += ((c.on ? 1.0f : 0.0f) - c.onT) * 0.0005f;  // fade the train in / out
                if (c.onT < 1e-3f) continue;
                c.t += dt;
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
                const float s = std::sin(c.phase) * env * c.amp * c.onT * wNight;
                l += s * c.panL; r += s * c.panR;
            }
        }
        // birds (day) / owl + gecko (night)
        for (Bird& b : birds_) {
            if (!b.active) {
                b.wait -= dt;
                if (b.wait <= 0.0f) {
                    startCall(b);
                    if (!b.active) continue;
                } else {
                    continue;
                }
            }
            b.t += dt;
            if (b.t >= b.length) {
                b.active = false;
                const bool nightBird = b.species == Species::Owl || b.species == Species::Gecko;
                b.wait = nightBird ? 14.0f + 22.0f * b.rng.uni()
                                   : (1.2f + 4.5f * b.rng.uni()) * (1.0f - 0.6f * wDawn_);
                continue;
            }
            float s = 0.0f;
            for (int k = 0; k < b.count; ++k) {
                const Note& nt = b.notes[k];
                if (b.t < nt.start || b.t >= nt.start + nt.dur) continue;
                const float u = (b.t - nt.start) / nt.dur;
                b.vib += kTwoPi * nt.vibHz * dt;
                if (b.vib > kTwoPi) b.vib -= kTwoPi;
                const float f = lerpf(nt.f0, nt.f1, u) * (1.0f + nt.vibDepth * std::sin(b.vib));
                b.phase += kTwoPi * f * dt;
                if (b.phase > kTwoPi) b.phase -= kTwoPi;
                const float env = std::pow(std::sin(kPi * u), 0.6f) * nt.amp;
                s = env * (std::sin(b.phase) + b.h2 * std::sin(2.0f * b.phase) + b.h3 * std::sin(3.0f * b.phase));
                break;
            }
            s = b.lp.lp(s, b.lpAlpha) * b.amp;
            l += s * b.panL; r += s * b.panR;
        }
        // cicadas: pulse-modulated narrow-band noise in slow swells (hot middle of the day)
        if (wCicada > 1e-3f) {
            for (Cicada& c : cicadas_) {
                c.swellT += dt;
                if (c.swellT >= c.swellLen) { c.swellT -= c.swellLen; c.swellLen = 8.0f + 10.0f * c.rng.uni(); }
                const float u = c.swellT / c.swellLen;
                const float swell = u < 0.6f ? std::pow(std::sin(kPi * u / 0.6f), 2.0f) : 0.0f;
                c.pulse += c.rate * dt;
                if (c.pulse >= 1.0f) c.pulse -= 1.0f;
                const float p = std::sin(kPi * c.pulse);
                c.band.process(c.rng.bi(), c.freq, 6.0f);
                const float s = c.band.band * p * p * p * p * swell * 0.05f * wCicada;
                l += s * c.panL; r += s * c.panR;
            }
        } else {
            for (Cicada& c : cicadas_) c.swellT += dt;
        }
        // underground: the bed comes through the earth (low-passed, quieter) + room tone
        if (ug_ > 1e-3f) {
            l = ugL2_.lp(ugL1_.lp(l, ugCut), ugCut) * ugGain;
            r = ugR2_.lp(ugR1_.lp(r, ugCut), ugCut) * ugGain;
            vent_.process(rng_.bi(), 380.0f, 0.8f);
            const float vent = ventLp1_.lp(vent_.lo, onePoleAlpha(900.0f)) * 1.6f;
            humPhase_ += kTwoPi * 100.0f * dt;
            if (humPhase_ > kTwoPi) humPhase_ -= kTwoPi;
            const float hum = std::sin(humPhase_) + 0.4f * std::sin(2.0f * humPhase_) + 0.15f * std::sin(3.0f * humPhase_);
            const float tone = (vent * 0.10f + hum * 0.006f) * ug_;
            l += tone; r += tone * 0.95f;
        }
        out[2 * i] += l * gain;
        out[2 * i + 1] += r * gain;
    }
}

}  // namespace ts::audio
