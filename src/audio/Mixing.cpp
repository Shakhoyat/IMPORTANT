#include "audio/Mixing.h"

#include <algorithm>
#include <cmath>

#include "audio/Dsp.h"

namespace ts::audio {

namespace {
// Freeverb tunings (44.1 kHz) scaled to 48 kHz; the right channel is offset by 23 samples for decorrelation.
constexpr int kCombTune[6] = {1116, 1188, 1277, 1356, 1422, 1557};
constexpr int kApTune[3] = {556, 441, 341};
constexpr int kSpread = 23;
int scaled(int n) { return static_cast<int>(std::lround(n * (48000.0 / 44100.0))); }

struct Preset { float feedback, damp, wet, input; };
constexpr Preset kOpen{0.60f, 0.62f, 0.55f, 0.020f};     // RT ~0.35 s, dark: a hint of space off the structures
constexpr Preset kBunker{0.865f, 0.18f, 1.00f, 0.024f};  // RT ~1.4 s, bright concrete
}  // namespace

RoomReverb::RoomReverb() {
    for (int i = 0; i < kCombs; ++i) {
        combL_[i].buf.assign(static_cast<size_t>(scaled(kCombTune[i])), 0.0f);
        combR_[i].buf.assign(static_cast<size_t>(scaled(kCombTune[i] + kSpread)), 0.0f);
    }
    for (int i = 0; i < kAllPasses; ++i) {
        apL_[i].buf.assign(static_cast<size_t>(scaled(kApTune[i])), 0.0f);
        apR_[i].buf.assign(static_cast<size_t>(scaled(kApTune[i] + kSpread)), 0.0f);
    }
    preLen_ = kSampleRate * 12 / 1000;   // 12 ms pre-delay
    pre_.assign(static_cast<size_t>(preLen_), 0.0f);
}

void RoomReverb::reset() {
    for (int i = 0; i < kCombs; ++i) {
        std::fill(combL_[i].buf.begin(), combL_[i].buf.end(), 0.0f);
        std::fill(combR_[i].buf.begin(), combR_[i].buf.end(), 0.0f);
        combL_[i].store = combR_[i].store = 0.0f;
    }
    for (int i = 0; i < kAllPasses; ++i) {
        std::fill(apL_[i].buf.begin(), apL_[i].buf.end(), 0.0f);
        std::fill(apR_[i].buf.begin(), apR_[i].buf.end(), 0.0f);
    }
    std::fill(pre_.begin(), pre_.end(), 0.0f);
}

void RoomReverb::setBunker(float amount) { target_ = clampf(amount, 0.0f, 1.0f); }

void RoomReverb::process(const float* in, float* out, int n) {
    // Glide the preset once per block (~0.4 s time constant).
    cur_ += (target_ - cur_) * (1.0f - std::exp(-static_cast<float>(n) / (0.4f * kSampleRate)));
    const float fb = lerpf(kOpen.feedback, kBunker.feedback, cur_);
    const float damp = lerpf(kOpen.damp, kBunker.damp, cur_);
    const float wet = lerpf(kOpen.wet, kBunker.wet, cur_);
    const float ing = lerpf(kOpen.input, kBunker.input, cur_);
    for (int i = 0; i < n; ++i) {
        const float x = pre_[static_cast<size_t>(preIdx_)] * ing;
        pre_[static_cast<size_t>(preIdx_)] = std::isfinite(in[i]) ? in[i] : 0.0f;
        if (++preIdx_ >= preLen_) preIdx_ = 0;
        float l = 0.0f, r = 0.0f;
        for (int c = 0; c < kCombs; ++c) {
            for (int ch = 0; ch < 2; ++ch) {
                Comb& cb = ch == 0 ? combL_[c] : combR_[c];
                float& y = cb.buf[static_cast<size_t>(cb.idx)];
                const float o = y;
                cb.store = o * (1.0f - damp) + cb.store * damp;
                y = x + cb.store * fb;
                if (++cb.idx >= static_cast<int>(cb.buf.size())) cb.idx = 0;
                (ch == 0 ? l : r) += o;
            }
        }
        for (int a = 0; a < kAllPasses; ++a) {
            for (int ch = 0; ch < 2; ++ch) {
                AllPass& ap = ch == 0 ? apL_[a] : apR_[a];
                float& v = ch == 0 ? l : r;
                float& b = ap.buf[static_cast<size_t>(ap.idx)];
                const float o = b - v;
                b = v + b * 0.5f;
                v = o;
                if (++ap.idx >= static_cast<int>(ap.buf.size())) ap.idx = 0;
            }
        }
        out[2 * i] += l * wet;
        out[2 * i + 1] += r * wet;
    }
    // flush denormals in the long feedback paths
    for (int c = 0; c < kCombs; ++c) {
        if (std::fabs(combL_[c].store) < 1e-20f) combL_[c].store = 0.0f;
        if (std::fabs(combR_[c].store) < 1e-20f) combR_[c].store = 0.0f;
    }
}

PeakLimiter::PeakLimiter(float threshold, float knee, float ceiling) : thr_(threshold), knee_(knee), ceil_(ceiling) {}

float PeakLimiter::softClip(float x, float knee, float ceiling) {
    const float a = std::fabs(x);
    if (a <= knee) return x;
    const float span = ceiling - knee;
    const float y = knee + span * std::tanh((a - knee) / span);
    return x < 0.0f ? -y : y;
}

void PeakLimiter::process(float* io, int frames) {
    const float att = 1.0f - std::exp(-1.0f / (0.0005f * kSampleRate));
    const float rel = 1.0f - std::exp(-1.0f / (0.150f * kSampleRate));
    for (int i = 0; i < frames; ++i) {
        float l = io[2 * i], r = io[2 * i + 1];
        if (!std::isfinite(l)) l = 0.0f;
        if (!std::isfinite(r)) r = 0.0f;
        const float peak = std::max(std::fabs(l), std::fabs(r));
        const float want = peak > thr_ ? thr_ / peak : 1.0f;
        g_ += (want - g_) * (want < g_ ? att : rel);
        minG_ = std::min(minG_, g_);
        const float dl = bufL_[idx_], dr = bufR_[idx_];
        bufL_[idx_] = l;
        bufR_[idx_] = r;
        if (++idx_ >= kLook) idx_ = 0;
        io[2 * i] = softClip(dl * g_, knee_, ceil_);
        io[2 * i + 1] = softClip(dr * g_, knee_, ceil_);
    }
}

}  // namespace ts::audio
