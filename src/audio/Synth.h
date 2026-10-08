// Procedural sound synthesis (no sound files). One-shot clips are rendered into mono 48 kHz float buffers;
// continuous sources are small stateful generators that stream blocks. Everything is deterministic (explicit seeds,
// no wall clock, no global state) and finite.
#pragma once

#include <cstdint>
#include <vector>

#include "audio/Acoustics.h"
#include "audio/Ambience.h"
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
    // v7: the player's throttle (0..1) when this source is the possessed vehicle (engine revs ahead of the speed).
    virtual void setLoad(float load) { (void)load; }
};

// Quad-rotor drone: four detuned rotors (6 blade-pass harmonics each) whose throttles wander independently (the
// characteristic beating), motor whine, blade-chopped broadband noise; pitch follows speed.
class RotorHumGen : public ContinuousGen {
public:
    explicit RotorHumGen(uint32_t seed);
    void render(float* out, int n, float speed, float pitch) override;
private:
    Rng rng_; float phase_[4] = {0, 0, 0, 0}, detune_[4] = {1, 1, 1, 1}, jit_[4] = {0, 0, 0, 0}, jitV_[4] = {0, 0, 0, 0};
    float motor_[4] = {0, 0, 0, 0}, wobble_ = 0.0f, speed_ = 0.0f;
    Svf band_; OnePole air_; bool primed_ = false;
};

// Helicopter: 4-blade main rotor (22 Hz blade pass x rotor fraction): low thump + impulsive blade slap with a
// per-blade level pattern, tail-rotor buzz, two turbine whine tones + jet noise, rotor-wash rumble.
class HeliThumpGen : public ContinuousGen {
public:
    explicit HeliThumpGen(uint32_t seed);
    void render(float* out, int n, float rotorFrac, float pitch) override;
private:
    Rng rng_; float phase_ = 0.0f, slapT_ = 1.0f, slapAmp_ = 0.0f, slapPhase_ = 0.0f, frac_ = 0.0f, whine_ = 0.0f;
    float whine2_ = 0.0f, tail_ = 0.0f; int blade_ = 0;
    OnePole lpA_, lpB_, hpJet_; Svf slapBand_;
};

// Tank: V12 diesel (lumpy per-cylinder firing at 6 x crank rate, 650 rpm idle rising with speed, saturated),
// turbo whine, exhaust rumble; tracks: link clanks through metallic resonances at speed / 0.16 m, road-wheel
// rumble and intermittent squeal.
class TankEngineGen : public ContinuousGen {
public:
    explicit TankEngineGen(uint32_t seed);
    void render(float* out, int n, float speed, float pitch) override;
    void setLoad(float load) override { loadTarget_ = load; }
private:
    float load_ = 0.0f, loadTarget_ = 0.0f;
    Rng rng_; float fire_ = 0.0f, rev_ = 0.0f, speed_ = 0.0f, turbo_ = 0.0f, link_ = 0.0f, clank_ = 0.0f, clankAmp_ = 0.0f;
    float squealEnv_ = 0.0f, cylAmp_[12] = {}; int cyl_ = 0;
    OnePole lpPulse_, lpNoise_, thud_, rumble_; Svf res1_, res2_, squeal_;
};

// Round in flight: rocket motor roar (param 0) or shell whistle (param 1).
class RocketGen : public ContinuousGen {
public:
    explicit RocketGen(uint32_t seed);
    void render(float* out, int n, float kind, float pitch) override;
private:
    Rng rng_; Svf band_; OnePole lpA_, lpB_;
};

// ---- v7 vehicles (SynthVehicles.cpp) ----
// Intruder rover: electric drive whine (pitch follows wheel speed), gear mesh, PWM hiss and tyre crunch.
class RoverWhineGen : public ContinuousGen {
public:
    explicit RoverWhineGen(uint32_t seed);
    void render(float* out, int n, float speed, float pitch) override;
    void setLoad(float load) override { loadTarget_ = load; }
private:
    Rng rng_; float phase_ = 0.0f, gear_ = 0.0f, speed_ = 0.0f, servo_ = 0.0f, servoT_ = 1.0f; bool primed_ = false;
    float load_ = 0.0f, loadTarget_ = 0.0f;
    Svf pwm_, crunch_; OnePole crunchLp_;
};

// Bunker diesel generator: 1500 rpm 6-cylinder firing (75 Hz) + 50 Hz alternator hum + radiator fan + rattle.
class GeneratorGen : public ContinuousGen {
public:
    explicit GeneratorGen(uint32_t seed);
    void render(float* out, int n, float load, float pitch) override;
private:
    Rng rng_; float fire_ = 0.0f, hum_ = 0.0f, fan_ = 0.0f, cyl_ = 0.0f; OnePole lpFire_, lpNoise_, fanLp_; Svf rattle_;
};

// ---- v7 foley and UI cues (SynthFoley.cpp) ----
// One footstep (heel + toe) on a surface; 'variant' picks one of several takes (deterministic).
std::vector<float> synthFootstep(Surface s, uint32_t variant);
// Radio transmission: squelch open, garbled formant "voice" through a 300-3000 Hz band, hiss, squelch tail and a
// roger beep. 0.9 .. 2.0 s depending on the seed.
std::vector<float> synthRadioChatter(uint32_t seed);

enum class UiCue { PovJump, PickerOpen, PickerClose, PickerMove, Possess, Release, HitMarker, Alert, FreeCam };
constexpr int kUiCueCount = 9;
const char* uiCueName(UiCue c);
// v7 small arms: the possessed soldier's rifle (muzzle blast + supersonic crack + bolt action), one of 4 takes, and a
// bullet impact on a surface (dirt puff / gravel spray / concrete crack / metal ping, sometimes a ricochet whine).
std::vector<float> synthRifleShot(uint32_t take);
std::vector<float> synthImpact(Surface s, uint32_t take);
std::vector<float> synthUiCue(UiCue c);

}  // namespace ts::audio
