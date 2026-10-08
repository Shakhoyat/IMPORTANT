// Ambient bed (stereo, not spatialised). v5: gusting wind + cricket chirp trains + distant generator hum (night).
// v7: the bed follows the time of day (setTimeOfDay): day birds (koel "ku-OO" calls, myna chatter, sparrow chips,
// warblers; a dawn chorus around 06:00) and cicada swells by day; crickets, an owl and a tokay gecko at night; the
// wind gusts harder by day and the camp generator is louder at night. Underground (setUnderground) the bed is
// muffled through the earth and replaced by the bunker's room tone (ventilation rush + mains hum).
#pragma once

#include <cstdint>

#include "audio/Dsp.h"

namespace ts::audio {

class AmbientGen {
public:
    explicit AmbientGen(uint32_t seed = 99);
    // Adds the bed into interleaved stereo 'out' (n frames) times gain.
    void render(float* out, int n, float gain);
    // hour 0..24, daylight 0 (night) .. 1 (full day). Default 23:00, night (the v5 bed). Layers cross-fade over ~2 s.
    void setTimeOfDay(float hour, float daylight);
    // 0 = above ground, 1 = inside the bunker (glides over ~0.3 s).
    void setUnderground(float amount);
    // Current layer weights (inspection / tests).
    float dayWeight() const { return wDay_; }
    float undergroundWeight() const { return ug_; }

private:
    struct Cricket { float freq, phase, period, t, amp, panL, panR, onT, offT; bool on; uint32_t rs; };
    struct Note { float start, dur, f0, f1, amp, vibHz, vibDepth; };
    enum class Species { Koel, Myna, Sparrow, Warbler, Owl, Gecko };
    struct Bird {
        Rng rng{1};
        Note notes[16];
        int count = 0;
        bool active = false;
        float t = 0.0f, wait = 0.0f, length = 0.0f, phase = 0.0f, vib = 0.0f;
        float panL = 0.7f, panR = 0.7f, amp = 0.0f, lpAlpha = 1.0f, h2 = 0.0f, h3 = 0.0f;
        Species species = Species::Sparrow;
        OnePole lp;
    };
    struct Cicada { Svf band; float swellT = 0.0f, swellLen = 10.0f, pulse = 0.0f, rate = 160.0f, freq = 5200.0f;
                    float panL = 0.7f, panR = 0.7f; Rng rng{1}; };

    void startCall(Bird& b);

    Rng rng_; Svf windL_, windR_; float t_ = 0.0f, genPhase_ = 0.0f; OnePole genLp_;
    Cricket crickets_[7];
    Bird birds_[4];
    Cicada cicadas_[2];
    float hour_ = 23.0f, daylight_ = 0.0f, ugTarget_ = 0.0f;
    float wDay_ = 0.0f, wDawn_ = 0.0f, ug_ = 0.0f;
    OnePole ugL1_, ugL2_, ugR1_, ugR2_, ventLp1_;
    Svf vent_, windLpL_, windLpR_;
    float humPhase_ = 0.0f;
};

}  // namespace ts::audio
