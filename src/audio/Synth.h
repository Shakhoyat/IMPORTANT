// Procedural sound synthesis (no sound files). One-shot clips are rendered into mono 48 kHz float buffers;
// continuous sources are small stateful generators that stream blocks. Everything is deterministic (explicit seeds,
// no wall clock, no global state) and finite.
#pragma once

#include <cstdint>
#include <vector>

#include "audio/Dsp.h"

namespace ts::audio {

// ---- one-shot clips (mono, peak <= ~1.2, fade-out at the end) ----
std::vector<float> synthExplosion(float power, uint32_t seed = 1);  // power = event value / 100 (~0.2 .. 4)
std::vector<float> synthCannon(uint32_t seed = 2);                  // sharp crack + boom
std::vector<float> synthGatling(uint32_t seed = 3);                 // 1.4 s burst of short clicks, ~55 shots/s
std::vector<float> synthWhoosh(uint32_t seed = 4);                  // launch: band-passed noise rising then falling + crackle
std::vector<float> synthDoor(uint32_t seed = 5);                    // motor hum + final clunk
std::vector<float> synthSiren(uint32_t seed = 6);                   // wave-start siren, 6 s
std::vector<float> synthAlarm(uint32_t seed = 7);                   // two-tone klaxon, 1.6 s (breach)
std::vector<float> synthLockTone();                                 // two short beeps
std::vector<float> synthConfirmChirp();                             // rising chirp
std::vector<float> synthKillConfirm();                              // three rising notes

// ---- continuous generators ----
class ContinuousGen {
public:
    virtual ~ContinuousGen() = default;
    // Adds nothing: overwrites out[0..n). 'param' is the source state (speed m/s or rotor fraction), 'pitch' a
    // multiplier (Doppler).
    virtual void render(float* out, int n, float param, float pitch) = 0;
};

// Quad-rotor drone: four detuned rotors, 6 blade-pass harmonics each, plus broadband noise; pitch follows speed.
class RotorHumGen : public ContinuousGen {
public:
    explicit RotorHumGen(uint32_t seed);
    void render(float* out, int n, float speed, float pitch) override;
private:
    Rng rng_; float phase_[4] = {0, 0, 0, 0}, detune_[4] = {1, 1, 1, 1}, wobble_ = 0.0f, speed_ = 0.0f;
    Svf band_; bool primed_ = false;
};

// Helicopter: main-rotor blade slaps at 25 Hz x rotor fraction, turbine whine, low noise.
class HeliThumpGen : public ContinuousGen {
public:
    explicit HeliThumpGen(uint32_t seed);
    void render(float* out, int n, float rotorFrac, float pitch) override;
private:
    Rng rng_; float phase_ = 0.0f, slapT_ = 1.0f, slapAmp_ = 0.0f, slapPhase_ = 0.0f, frac_ = 0.0f, whine_ = 0.0f;
    OnePole lpA_, lpB_;
};

// Tank diesel: firing-pulse train (16 Hz idle, rising with speed), low noise, track clatter when driving.
class TankEngineGen : public ContinuousGen {
public:
    explicit TankEngineGen(uint32_t seed);
    void render(float* out, int n, float speed, float pitch) override;
private:
    Rng rng_; float phase_ = 0.0f, speed_ = 0.0f, mod_ = 0.0f; OnePole lpPulse_, lpNoise_, hpClatter_, lpClatter_;
};

// Round in flight: rocket motor roar (param 0) or shell whistle (param 1).
class RocketGen : public ContinuousGen {
public:
    explicit RocketGen(uint32_t seed);
    void render(float* out, int n, float kind, float pitch) override;
private:
    Rng rng_; Svf band_; OnePole lpA_, lpB_;
};

// Night ambience (stereo, not spatialised): gusting wind + cricket chirp trains + distant generator hum.
class AmbientGen {
public:
    explicit AmbientGen(uint32_t seed = 99);
    // Adds the bed into interleaved stereo 'out' (n frames) times gain.
    void render(float* out, int n, float gain);
private:
    struct Cricket { float freq, phase, period, t, amp, panL, panR, onT, offT; bool on; uint32_t rs; };
    Rng rng_; Svf windL_, windR_; float t_ = 0.0f, genPhase_ = 0.0f; OnePole genLp_;
    Cricket crickets_[7];
};

}  // namespace ts::audio
