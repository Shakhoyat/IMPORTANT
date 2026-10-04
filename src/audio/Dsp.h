// Small DSP building blocks for the procedural synthesis: deterministic RNG, one-pole filters, a TPT state-variable
// filter and helpers. Header only, no allocation, no global state (every generator owns its own seed).
#pragma once

#include <cmath>
#include <cstdint>

namespace ts::audio {

constexpr int kSampleRate = 48000;
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

// xorshift32: deterministic, platform independent.
struct Rng {
    uint32_t s = 2463534242u;
    explicit Rng(uint32_t seed = 1u) { s = seed * 2654435761u + 0x9E3779B9u; if (s == 0) s = 0x1234567u; for (int i = 0; i < 4; ++i) bits(); }
    uint32_t bits() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float>(bits() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bi() { return uni() * 2.0f - 1.0f; }                                       // [-1, 1)
};

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

// One-pole low-pass: alpha = 1 - exp(-2 pi fc / fs).
inline float onePoleAlpha(float cutoffHz) {
    cutoffHz = clampf(cutoffHz, 5.0f, 0.45f * kSampleRate);
    return 1.0f - std::exp(-kTwoPi * cutoffHz / kSampleRate);
}
struct OnePole {
    float y = 0.0f;
    float lp(float x, float alpha) { y += alpha * (x - y); return y; }
    float hp(float x, float alpha) { y += alpha * (x - y); return x - y; }
};

// Topology-preserving state-variable filter; cutoff may change every sample.
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float lo = 0.0f, band = 0.0f, hi = 0.0f;
    void process(float x, float cutoffHz, float q) {
        const float g = std::tan(kPi * clampf(cutoffHz, 10.0f, 0.45f * kSampleRate) / kSampleRate);
        const float k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lo = v2; band = v1; hi = x - k * v1 - v2;
    }
};

// Exponential decay envelope value at time t with time constant tau.
inline float expEnv(float t, float tau) { return std::exp(-t / tau); }

// Short linear attack / release window on a buffer position (avoids clicks).
inline float edge(int i, int n, int fade) {
    if (i < fade) return static_cast<float>(i) / fade;
    if (i >= n - fade) return static_cast<float>(n - 1 - i) / fade;
    return 1.0f;
}

}  // namespace ts::audio
