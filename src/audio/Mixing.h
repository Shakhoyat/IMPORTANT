// v7 master-bus processing: a small stereo room reverb (Freeverb-style parallel damped combs + series all-passes)
// whose character blends from "open camp" (short, dark, low level) to "underground bunker" (long, bright concrete,
// high level), a look-ahead peak limiter and a soft-knee clipper that can never exceed the ceiling.
#pragma once

#include <vector>

namespace ts::audio {

class RoomReverb {
public:
    RoomReverb();
    // 0 = open air preset, 1 = bunker preset; the parameters glide there over ~0.4 s.
    void setBunker(float amount);
    float bunker() const { return cur_; }
    // Adds the reverb of the mono 'in' (n samples) into interleaved stereo 'out'.
    void process(const float* in, float* out, int n);
    void reset();

private:
    struct Comb { std::vector<float> buf; int idx = 0; float store = 0.0f; };
    struct AllPass { std::vector<float> buf; int idx = 0; };
    static constexpr int kCombs = 6, kAllPasses = 3;
    Comb combL_[kCombs], combR_[kCombs];
    AllPass apL_[kAllPasses], apR_[kAllPasses];
    std::vector<float> pre_;
    int preIdx_ = 0, preLen_ = 0;
    float target_ = 0.0f, cur_ = 0.0f;
};

// Peak limiter with 1 ms look-ahead: gain attack 0.5 ms, release 150 ms, then a soft-knee clipper (linear below
// knee, tanh into the ceiling above it).
class PeakLimiter {
public:
    PeakLimiter(float threshold = 0.75f, float knee = 0.6f, float ceiling = 0.88f);
    void process(float* interleaved, int frames);
    float gain() const { return g_; }               // current gain reduction factor (1 = none)
    float minGain() const { return minG_; }          // smallest gain since the last resetStats()
    void resetStats() { minG_ = 1.0f; }
    static float softClip(float x, float knee, float ceiling);
private:
    static constexpr int kLook = 48;
    float thr_, knee_, ceil_;
    float g_ = 1.0f, minG_ = 1.0f;
    float bufL_[kLook] = {}, bufR_[kLook] = {};
    int idx_ = 0;
};

}  // namespace ts::audio
