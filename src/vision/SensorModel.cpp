#include "vision/SensorModel.h"

#include <algorithm>
#include <cmath>

namespace ts {

namespace {

constexpr uint64_t kTemporalStream = 0x7e3a1c55ULL;  // PCG32 stream ids: temporal noise / fixed pattern / scenes
constexpr uint64_t kPatternStream = 0x5eedf1d0ULL;
constexpr uint64_t kSceneStream = 0x51f7a9e3ULL;
constexpr float kTwoPi = 6.28318531f;

// One Box-Muller pair from two uniforms of 'r' (u1 in (0, 1] so the log is finite).
void boxMuller(Pcg32& r, float& a, float& b) {
    const float u1 = 1.0f - r.uniform(), u2 = r.uniform();
    const float rad = std::sqrt(-2.0f * std::log(u1));
    a = rad * std::cos(kTwoPi * u2);
    b = rad * std::sin(kTwoPi * u2);
}

}  // namespace

SensorModel::SensorModel(uint32_t seed, const SensorNoiseConfig& cfg)
    : seed_(seed), rng_(seed, kTemporalStream), cfg_(cfg) {}

float SensorModel::uniform() { return rng_.uniform(); }

float SensorModel::gaussian() {
    if (spare_) {
        spare_ = false;
        return spareValue_;
    }
    float a, b;
    boxMuller(rng_, a, b);
    spare_ = true;
    spareValue_ = b;
    return a;
}

void SensorModel::buildPattern(int w, int h) {
    // The fixed pattern is a property of the detector: its own stream, independent of how many frames were taken.
    // Stored with unit variance; apply() scales it by the configured sigmas and the NUC residual.
    Pcg32 r(seed_, kPatternStream);
    w_ = w;
    h_ = h;
    const size_t n = static_cast<size_t>(w) * h;
    gain_.resize(n);
    offset_.resize(n);
    float a, b;
    for (size_t i = 0; i < n; ++i) {
        boxMuller(r, a, b);
        gain_[i] = a;
        offset_[i] = b;
    }
    column_.resize(static_cast<size_t>(w));
    for (int x = 0; x < w; ++x) {
        boxMuller(r, a, b);
        column_[x] = a;
    }
    // Stuck-hot pixels, isolated from each other (a real camera maps out clusters; single ones get through and
    // are what the despeckle step removes).
    stuck_.clear();
    const int want = static_cast<int>(std::lround(cfg_.stuckFrac * static_cast<float>(n)));
    std::vector<uint8_t> taken(n, 0);
    for (int tries = 0; static_cast<int>(stuck_.size()) < want && tries < want * 50; ++tries) {
        const int x = static_cast<int>(r.next() % static_cast<uint32_t>(w));
        const int y = static_cast<int>(r.next() % static_cast<uint32_t>(h));
        bool free = true;
        for (int dy = -1; dy <= 1 && free; ++dy)
            for (int dx = -1; dx <= 1 && free; ++dx) {
                const int xx = x + dx, yy = y + dy;
                if (xx >= 0 && yy >= 0 && xx < w && yy < h && taken[static_cast<size_t>(yy) * w + xx]) free = false;
            }
        if (!free) continue;
        taken[static_cast<size_t>(y) * w + x] = 1;
        stuck_.push_back(y * w + x);
    }
    std::sort(stuck_.begin(), stuck_.end());
}

void SensorModel::apply(std::vector<uint8_t>& img, int w, int h) {
    if (w <= 0 || h <= 0 || img.size() < static_cast<size_t>(w) * h) return;
    if (w != w_ || h != h_ || gain_.empty()) buildPattern(w, h);
    const size_t n = static_cast<size_t>(w) * h;
    // Optics: 3x3 binomial PSF, separable [1 2 1] / 4 rows then columns, edges clamped.
    blur_.resize(n);
    if (cfg_.psf) {
        std::vector<float> rows(n);
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = &img[static_cast<size_t>(y) * w];
            float* dst = &rows[static_cast<size_t>(y) * w];
            for (int x = 0; x < w; ++x) {
                const int l = std::max(0, x - 1), r = std::min(w - 1, x + 1);
                dst[x] = 0.25f * src[l] + 0.5f * src[x] + 0.25f * src[r];
            }
        }
        for (int y = 0; y < h; ++y) {
            const float* up = &rows[static_cast<size_t>(std::max(0, y - 1)) * w];
            const float* mid = &rows[static_cast<size_t>(y) * w];
            const float* dn = &rows[static_cast<size_t>(std::min(h - 1, y + 1)) * w];
            float* dst = &blur_[static_cast<size_t>(y) * w];
            for (int x = 0; x < w; ++x) dst[x] = 0.25f * up[x] + 0.5f * mid[x] + 0.25f * dn[x];
        }
    } else {
        for (size_t i = 0; i < n; ++i) blur_[i] = img[i];
    }
    // Detector: per-pixel gain and offset, column stripes (fixed pattern x NUC residual), temporal noise, Q.
    const float gs = cfg_.fpnGainSigma * fpnScale_, os = cfg_.fpnOffsetSigma * fpnScale_,
                cs = cfg_.columnSigma * fpnScale_, netd = cfg_.netdDn;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y) * w + x;
            float v = (1.0f + gs * gain_[i]) * blur_[i] + os * offset_[i] + cs * column_[x];
            if (netd > 0.0f) v += netd * gaussian();
            img[i] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, std::nearbyint(v))));
        }
    }
    for (int i : stuck_) img[i] = 255;
    if (mapped_) {
        // Bad-pixel replacement: a mapped pixel gets the mean of its 4 neighbours (stuck pixels are isolated, so
        // the neighbours are good). Without it a stuck pixel on a warm soldier gives him a "hot core" (= target).
        for (int i : stuck_) {
            const int x = i % w, y = i / w;
            int sum = 0, n = 0;
            if (x > 0) { sum += img[i - 1]; ++n; }
            if (x + 1 < w) { sum += img[i + 1]; ++n; }
            if (y > 0) { sum += img[i - w]; ++n; }
            if (y + 1 < h) { sum += img[i + w]; ++n; }
            img[i] = static_cast<uint8_t>((sum + n / 2) / std::max(1, n));
        }
    }
    ++frames_;
}

bool SensorModel::dropFrame() {
    const bool drop = uniform() < cfg_.dropProb;
    if (drop) ++drops_;
    return drop;
}

void SensorModel::gimbalJitter(float& yawDeg, float& pitchDeg) {
    yawDeg = cfg_.jitterDeg * gaussian();
    pitchDeg = cfg_.jitterDeg * gaussian();
}

void SensorModel::nuc() {
    fpnScale_ = cfg_.nucResidual;
    mapped_ = true;  // the shutter frame shows every stuck pixel: all of them are mapped out
}

Mat4 jitterCamera(const Mat4& camera, float yawDeg, float pitchDeg) {
    return camera * rotateY(radians(yawDeg)) * rotateX(radians(pitchDeg));
}

std::vector<uint8_t> makeSyntheticFrame(int w, int h, uint32_t seed, bool rover, float roverX, float roverY,
                                        bool soldier, float soldierX, float soldierY, float roverScale) {
    Pcg32 r(seed, kSceneStream);
    std::vector<float> f(static_cast<size_t>(w) * h);
    // Night / day ground: 0.10 .. 0.20, warmer towards the horizon (top rows), plus a smooth texture (three
    // random plane waves: furrows, tracks) and a fine per-pixel grain (grass, gravel).
    float ph[3], kx[3], ky[3];
    for (int k = 0; k < 3; ++k) {
        ph[k] = kTwoPi * r.uniform();
        const float ang = kTwoPi * r.uniform(), wl = 12.0f + 40.0f * r.uniform();
        kx[k] = std::cos(ang) * kTwoPi / wl;
        ky[k] = std::sin(ang) * kTwoPi / wl;
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float g = 0.10f + 0.10f * (1.0f - static_cast<float>(y) / static_cast<float>(h));
            for (int k = 0; k < 3; ++k) g += 0.012f * std::sin(kx[k] * x + ky[k] * y + ph[k]);
            g += 0.02f * (r.uniform() - 0.5f);
            f[static_cast<size_t>(y) * w + x] = g;
        }
    auto put = [&](int x, int y, float v) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        float& p = f[static_cast<size_t>(y) * w + x];
        p = std::max(p, v);
    };
    // Sunlit patch (a sun-heated roof or rock face): ~0.62, well above the ground but below the 0.80 CV floor.
    {
        const int pw = std::max(4, w * 18 / 100), phh = std::max(4, h * 15 / 100);
        const int px = static_cast<int>(0.05f * w + r.uniform() * (0.90f * w - pw));
        const int py = static_cast<int>(0.05f * h + r.uniform() * (0.90f * h - phh));
        for (int y = py; y < py + phh; ++y)
            for (int x = px; x < px + pw; ++x) {
                const bool edge = x == px || y == py || x == px + pw - 1 || y == py + phh - 1;
                put(x, y, (edge ? 0.50f : 0.62f) + 0.01f * (r.uniform() - 0.5f));
            }
    }
    // Rover: elliptic body 0.86 with a hot engine core peaking at 0.97, soft warm halo (as tools/figdata dumpCv).
    if (rover) {
        const float rx = 6.5f * roverScale, ry = 3.5f * roverScale, ang = 1.2f * (r.uniform() - 0.5f);
        const float c = std::cos(ang), s = std::sin(ang), body = 0.86f, core = 0.97f;
        const int reach = static_cast<int>(std::ceil(1.6f * rx)) + 1;
        for (int y = static_cast<int>(roverY) - reach; y <= static_cast<int>(roverY) + reach; ++y)
            for (int x = static_cast<int>(roverX) - reach; x <= static_cast<int>(roverX) + reach; ++x) {
                const float dx = x - roverX, dy = y - roverY;
                const float u = (dx * c + dy * s) / rx, v = (-dx * s + dy * c) / ry;
                const float r2 = u * u + v * v;
                if (r2 < 1.0f) put(x, y, body + (core - body) * std::max(0.0f, 1.0f - 2.2f * r2));
                else if (r2 < 2.2f) put(x, y, 0.12f + 0.4f * (body - 0.12f) * (2.2f - r2) / 1.2f);
            }
    }
    // Soldier: upright figure, warm skin / uniform 0.80 .. 0.82, no hot core; 5 px wide, 13 px tall.
    if (soldier) {
        const float rx = 2.6f, ry = 6.5f;
        for (int y = static_cast<int>(soldierY) - 10; y <= static_cast<int>(soldierY) + 10; ++y)
            for (int x = static_cast<int>(soldierX) - 5; x <= static_cast<int>(soldierX) + 5; ++x) {
                const float u = (x - soldierX) / rx, v = (y - soldierY) / ry;
                const float r2 = u * u + v * v;
                if (r2 < 1.0f) put(x, y, 0.80f + 0.02f * (1.0f - r2));
                else if (r2 < 1.8f) put(x, y, 0.15f + 0.45f * (1.8f - r2) / 0.8f);
            }
    }
    std::vector<uint8_t> img(f.size());
    for (size_t i = 0; i < f.size(); ++i)
        img[i] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, std::nearbyint(f[i] * 255.0f))));
    return img;
}

}  // namespace ts
