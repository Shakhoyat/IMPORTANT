// v7 continuous sources: intruder rover drive and the bunker's diesel generator.
#include <algorithm>
#include <cmath>

#include "audio/Synth.h"

namespace ts::audio {

RoverWhineGen::RoverWhineGen(uint32_t seed) : rng_(seed) { phase_ = rng_.uni() * kTwoPi; }

void RoverWhineGen::render(float* out, int n, float speed, float pitch) {
    if (!primed_) { speed_ = speed; primed_ = true; }
    const float sm = 1.0f - std::exp(-1.0f / (0.1f * kSampleRate));
    const float aCrunch = onePoleAlpha(5000.0f);
    const float dt = 1.0f / kSampleRate;
    for (int i = 0; i < n; ++i) {
        speed_ += sm * (std::fabs(speed) - speed_);
        load_ += sm * 2.0f * (loadTarget_ - load_);   // the motors answer the throttle before the wheels catch up
        const float sp = clampf(std::max(speed_, 2.2f * load_), 0.0f, 3.0f);
        const float move = clampf(std::max(speed_ / 0.6f, 1.5f * load_), 0.0f, 1.0f);
        // Drive motors: the whine follows the wheel speed (gearbox 1:~20); the gear mesh sits 3.7x above it.
        const float f = (180.0f + 420.0f * sp) * pitch;
        phase_ += kTwoPi * f * dt;
        if (phase_ > kTwoPi) phase_ -= kTwoPi;
        gear_ += kTwoPi * f * 3.7f * dt;
        if (gear_ > kTwoPi) gear_ -= kTwoPi;
        // under load the drive strains: more upper harmonics
        float s = (std::sin(phase_) + (0.35f + 0.3f * load_) * std::sin(2.0f * phase_) + (0.15f + 0.25f * load_) * std::sin(3.0f * phase_)) *
                  (0.08f + 0.22f * move);
        s += 0.05f * move * std::sin(gear_);
        // PWM switching hiss of the motor drivers
        pwm_.process(rng_.bi(), 8000.0f * pitch, 3.0f);
        s += pwm_.band * 0.05f * (0.3f + 0.7f * move);
        // tyre crunch: sparse grains on the dirt, denser with speed
        const float grain = rng_.uni() < 0.004f * sp ? rng_.bi() * 3.0f : 0.0f;
        crunch_.process(crunchLp_.lp(grain + rng_.bi() * 0.08f * move, aCrunch), 2600.0f, 1.4f);
        s += crunch_.band * 0.35f * move;
        // steering servo: an occasional short chirp
        servoT_ += dt;
        if (move > 0.2f && rng_.uni() < 0.00001f) servoT_ = 0.0f;
        if (servoT_ < 0.09f) {
            servo_ += kTwoPi * (900.0f + 4000.0f * servoT_) * dt;
            if (servo_ > kTwoPi) servo_ -= kTwoPi;
            s += 0.06f * std::sin(servo_) * std::sin(kPi * servoT_ / 0.09f);
        }
        out[i] = s;
    }
}

GeneratorGen::GeneratorGen(uint32_t seed) : rng_(seed) {}

void GeneratorGen::render(float* out, int n, float load, float pitch) {
    const float dt = 1.0f / kSampleRate;
    const float l = clampf(load, 0.0f, 1.0f);
    for (int i = 0; i < n; ++i) {
        // 6-cylinder at 1500 rpm: 75 firings per second, with a slight per-cylinder imbalance
        fire_ += 75.0f * pitch * dt;
        if (fire_ >= 1.0f) { fire_ -= 1.0f; cyl_ = cyl_ >= 5.0f ? 0.0f : cyl_ + 1.0f; }
        const float imb = 1.0f + 0.12f * std::sin(cyl_ * 1.7f);
        float s = std::tanh(lpFire_.lp(std::exp(-fire_ * 6.0f) * 2.0f * imb - 0.3f, onePoleAlpha(500.0f)) * (1.5f + l)) * 0.5f;
        // alternator: 50 Hz mains with harmonics
        hum_ += kTwoPi * 50.0f * dt;
        if (hum_ > kTwoPi) hum_ -= kTwoPi;
        s += 0.10f * (std::sin(hum_) + 0.6f * std::sin(2.0f * hum_) + 0.3f * std::sin(3.0f * hum_));
        // radiator fan: blade-pass chopped noise
        fan_ += 150.0f * dt;
        if (fan_ >= 1.0f) fan_ -= 1.0f;
        s += fanLp_.lp(rng_.bi(), onePoleAlpha(1200.0f)) * 0.5f * (0.7f + 0.3f * std::sin(kTwoPi * fan_));
        s += lpNoise_.lp(rng_.bi(), onePoleAlpha(150.0f)) * 1.2f;
        // loose panel rattle excited by the firing pulses
        rattle_.process(std::exp(-fire_ * 40.0f) * rng_.bi(), 1700.0f, 14.0f);
        s += rattle_.band * 0.25f;
        out[i] = 0.7f * s;
    }
}

}  // namespace ts::audio
