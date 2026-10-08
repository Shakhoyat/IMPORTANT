// v6 thermal sensor model (WP-D): what a real uncooled microbolometer adds to the ideal rendered heat image before
// the computer-vision pipeline sees it - optics blur (PSF), per-pixel fixed-pattern gain / offset and column
// stripes (partly removed by a non-uniformity correction, NUC), temporal noise (NETD), stuck pixels, gimbal
// pointing jitter and dropped frames. Seeded per run, so detections are not deterministic between runs.
//   y = Q( g (.) (PSF * x) + o + c + n ),  n ~ N(0, NETD^2)
// x: ideal heat image (DN, 0..255), PSF: 3x3 binomial kernel, g / o: per-pixel gain / offset, c: per-column offset,
// Q: round to the nearest DN and clamp to 0..255; stuck-hot pixels read 255 whatever the scene.
// Random numbers: PCG32 (O'Neill 2014) + Box-Muller. Two independent streams per sensor: the fixed pattern (drawn
// once per image size, a property of the physical sensor) and the temporal stream (noise, jitter, drops).
#pragma once

#include <cstdint>
#include <vector>

#include "math/Math3D.h"

namespace ts {

struct SensorNoiseConfig {
    float netdDn = 2.0f;          // temporal noise sigma, digital numbers (8-bit scale)
    float fpnGainSigma = 0.01f;   // per-pixel gain non-uniformity (fixed per sensor)
    float fpnOffsetSigma = 1.5f;  // per-pixel offset, DN
    float columnSigma = 0.8f;     // column stripes, DN
    bool psf = true;              // 3x3 binomial blur [1 2 1]^T [1 2 1] / 16
    float jitterDeg = 0.08f;      // gimbal pointing jitter sigma per frame
    float dropProb = 0.01f;       // probability a frame is lost
    float stuckFrac = 2e-4f;      // fraction of stuck-hot pixels
    float nucResidual = 0.25f;    // fraction of the fixed pattern left after a NUC
};

// PCG32 (XSH-RR output of a 64-bit LCG): small, fast, statistically good and reproducible on every platform.
struct Pcg32 {
    uint64_t state = 0, inc = 1;
    Pcg32(uint64_t seed = 1, uint64_t stream = 0) { this->seed(seed, stream); }
    void seed(uint64_t seed, uint64_t stream) {
        state = 0;
        inc = (stream << 1u) | 1u;
        next();
        state += seed;
        next();
    }
    uint32_t next() {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }
    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }  // [0, 1), 24 bits
};

class SensorModel {
public:
    explicit SensorModel(uint32_t seed = 1, const SensorNoiseConfig& cfg = {});
    // Ideal heat image in, sensor output out (in place). The first call (or a new size) draws the fixed pattern.
    void apply(std::vector<uint8_t>& img, int w, int h);
    // Per frame: true = this frame is lost (the CV sees nothing; the tracker coasts). Probability dropProb.
    bool dropFrame();
    // Per frame: gimbal pointing error (degrees, N(0, jitterDeg^2) each axis) to render the frame with.
    void gimbalJitter(float& yawDeg, float& pitchDeg);
    // Shutter calibration (two-point NUC): the fixed pattern shrinks to nucResidual of its raw size, and the
    // stuck pixels seen on the shutter are mapped out (bad-pixel replacement: mean of the 4 neighbours).
    // A new sensor is uncalibrated (full fixed pattern, stuck pixels at 255) until nuc() is called.
    void nuc();
    const SensorNoiseConfig& config() const { return cfg_; }
    SensorNoiseConfig& config() { return cfg_; }
    uint32_t seed() const { return seed_; }

    // Fixed pattern (for tests / figures; empty until the first apply()).
    const std::vector<float>& gainPattern() const { return gain_; }
    const std::vector<float>& offsetPattern() const { return offset_; }
    const std::vector<float>& columnPattern() const { return column_; }
    const std::vector<int>& stuckPixels() const { return stuck_; }
    float fixedPatternScale() const { return fpnScale_; }
    bool badPixelsMapped() const { return mapped_; }
    int framesApplied() const { return frames_; }
    int framesDropped() const { return drops_; }

private:
    void buildPattern(int w, int h);
    float uniform();    // [0, 1), temporal stream
    float gaussian();   // N(0, 1), temporal stream (Box-Muller, the second value is kept for the next call)
    uint32_t seed_ = 1;
    Pcg32 rng_;
    SensorNoiseConfig cfg_;
    int w_ = 0, h_ = 0;
    std::vector<float> gain_, offset_, column_;  // raw pattern: gain deviation (g - 1), offset DN, column DN
    std::vector<int> stuck_;
    std::vector<float> blur_;                    // scratch: PSF * x
    float fpnScale_ = 1.0f;
    bool mapped_ = false;     // stuck pixels replaced (after a NUC)
    bool spare_ = false;
    float spareValue_ = 0.0f;
    int frames_ = 0, drops_ = 0;
};

// Camera matrix of a jittered frame: the gimbal turned by (yaw, pitch) degrees about its own axes (camera local
// frame: looks along +Z, +Y up). Render with this, but report the unjittered matrix with the detections.
Mat4 jitterCamera(const Mat4& camera, float yawDeg, float pitchDeg);

// Synthetic test frame (shared by the unit tests and the figure-data tool): background texture, a sunlit patch
// (0.62 heat), optional rover blob (body 0.86 + hot core 0.97), optional soldier blob (warm 0.80-0.82, no core,
// upright: taller than wide). Positions in pixels; roverScale < 1 makes the rover smaller (farther away).
// Deterministic for a seed (the same frame for the same arguments).
std::vector<uint8_t> makeSyntheticFrame(int w, int h, uint32_t seed, bool rover, float roverX, float roverY,
                                        bool soldier, float soldierX, float soldierY, float roverScale = 1.0f);

}  // namespace ts
