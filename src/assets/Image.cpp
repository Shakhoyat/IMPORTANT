#include "assets/Image.h"

#include <algorithm>
#include <cmath>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_PSD
#define STBI_NO_GIF
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ts::asset {

namespace {

bool fromStb(unsigned char* px, int w, int h, Image& out) {
    if (!px) return false;
    out.w = w;
    out.h = h;
    out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return true;
}

}  // namespace

bool loadImage(const std::string& path, Image& out) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);   // before reading w, h (argument order is unspecified)
    return fromStb(px, w, h, out);
}

bool loadImageMemory(const uint8_t* data, size_t size, Image& out) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    return fromStb(px, w, h, out);
}

Image resizeBox(const Image& src, int w, int h) {
    Image dst;
    dst.w = std::max(1, w);
    dst.h = std::max(1, h);
    dst.rgba.resize(static_cast<size_t>(dst.w) * dst.h * 4);
    const float sx = static_cast<float>(src.w) / dst.w, sy = static_cast<float>(src.h) / dst.h;
    for (int y = 0; y < dst.h; ++y) {
        const int y0 = static_cast<int>(y * sy), y1 = std::max(y0 + 1, static_cast<int>((y + 1) * sy));
        for (int x = 0; x < dst.w; ++x) {
            const int x0 = static_cast<int>(x * sx), x1 = std::max(x0 + 1, static_cast<int>((x + 1) * sx));
            float acc[4] = {0, 0, 0, 0}, wsum = 0.0f;
            for (int yy = y0; yy < std::min(y1, src.h); ++yy)
                for (int xx = x0; xx < std::min(x1, src.w); ++xx) {
                    const uint8_t* p = src.at(xx, yy);
                    // colour weighted by alpha (no dark fringes from transparent texels), alpha plain average
                    const float a = p[3] / 255.0f + 1e-3f;
                    for (int c = 0; c < 3; ++c) acc[c] += p[c] * a;
                    acc[3] += p[3];
                    wsum += a;
                }
            const int n = std::max(1, (std::min(y1, src.h) - y0) * (std::min(x1, src.w) - x0));
            uint8_t* d = dst.at(x, y);
            for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>(std::clamp(acc[c] / std::max(wsum, 1e-6f) + 0.5f, 0.0f, 255.0f));
            d[3] = static_cast<uint8_t>(std::clamp(acc[3] / n + 0.5f, 0.0f, 255.0f));
        }
    }
    return dst;
}

Image downsample(const Image& src) { return resizeBox(src, std::max(1, src.w / 2), std::max(1, src.h / 2)); }

void applyAlphaMask(Image& img, const Image& maskIn) {
    const Image mask = (maskIn.w == img.w && maskIn.h == img.h) ? maskIn : resizeBox(maskIn, img.w, img.h);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* m = mask.at(x, y);
            img.at(x, y)[3] = static_cast<uint8_t>((m[0] * 54 + m[1] * 183 + m[2] * 19) >> 8);
        }
}

float alphaCoverage(const Image& img, float cutoff) {
    if (img.empty()) return 0.0f;
    const int t = static_cast<int>(std::ceil(cutoff * 255.0f));
    size_t n = 0;
    for (size_t i = 3; i < img.rgba.size(); i += 4) n += img.rgba[i] >= t ? 1 : 0;
    return static_cast<float>(n) / static_cast<float>(img.w * img.h);
}

float preserveCoverage(Image& img, float cutoff, float target) {
    float lo = 0.0f, hi = 8.0f, best = 1.0f;
    std::vector<uint8_t> alpha(img.rgba.size() / 4);
    for (size_t i = 0; i < alpha.size(); ++i) alpha[i] = img.rgba[4 * i + 3];
    auto coverageAt = [&](float s) {
        const float t = cutoff * 255.0f;
        size_t n = 0;
        for (uint8_t a : alpha) n += a * s >= t ? 1 : 0;
        return static_cast<float>(n) / static_cast<float>(alpha.size());
    };
    for (int it = 0; it < 18; ++it) {
        const float mid = 0.5f * (lo + hi);
        if (coverageAt(mid) < target) lo = mid;
        else hi = mid;
    }
    // coverage is a step function of the scale: take the side of the step closer to the target
    best = std::fabs(coverageAt(hi) - target) < std::fabs(coverageAt(lo) - target) ? hi : lo;
    for (size_t i = 0; i < alpha.size(); ++i)
        img.rgba[4 * i + 3] = static_cast<uint8_t>(std::min(255.0f, alpha[i] * best + 0.5f));
    return best;
}

std::vector<Image> buildMips(const Image& src, float cutoff) {
    std::vector<Image> mips{src};
    const float target = cutoff > 0.0f ? alphaCoverage(src, cutoff) : 0.0f;
    while (mips.back().w > 1 || mips.back().h > 1) {
        Image next = downsample(mips.back());
        if (cutoff > 0.0f) preserveCoverage(next, cutoff, target);
        mips.push_back(std::move(next));
    }
    return mips;
}

void dilateColour(Image& img, int passes) {
    std::vector<uint8_t> known(static_cast<size_t>(img.w) * img.h);
    for (size_t i = 0; i < known.size(); ++i) known[i] = img.rgba[4 * i + 3] > 8 ? 1 : 0;
    for (int p = 0; p < passes; ++p) {
        std::vector<uint8_t> next = known;
        bool changed = false;
        for (int y = 0; y < img.h; ++y)
            for (int x = 0; x < img.w; ++x) {
                const size_t i = static_cast<size_t>(y) * img.w + x;
                if (known[i]) continue;
                int acc[3] = {0, 0, 0}, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= img.w || yy >= img.h) continue;
                        const size_t j = static_cast<size_t>(yy) * img.w + xx;
                        if (!known[j]) continue;
                        for (int c = 0; c < 3; ++c) acc[c] += img.rgba[4 * j + c];
                        ++n;
                    }
                if (!n) continue;
                for (int c = 0; c < 3; ++c) img.rgba[4 * i + c] = static_cast<uint8_t>(acc[c] / n);
                next[i] = 1;
                changed = true;
            }
        known.swap(next);
        if (!changed) break;
    }
}

std::vector<BladeRect> findBladeRects(const Image& img, int probe, float minHeight, float minAspect) {
    std::vector<BladeRect> out;
    if (img.empty()) return out;
    const Image small = resizeBox(img, probe, probe);
    const int n = probe * probe;
    std::vector<int> label(static_cast<size_t>(n), -1);
    std::vector<int> stack;
    int next = 0;
    for (int start = 0; start < n; ++start) {
        if (label[static_cast<size_t>(start)] >= 0 || small.rgba[static_cast<size_t>(start) * 4 + 3] < 128) continue;
        int x0 = probe, y0 = probe, x1 = -1, y1 = -1;
        stack.assign(1, start);
        label[static_cast<size_t>(start)] = next;
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            const int x = p % probe, y = p / probe;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= probe || yy >= probe) continue;
                    const int q = yy * probe + xx;
                    if (label[static_cast<size_t>(q)] >= 0 || small.rgba[static_cast<size_t>(q) * 4 + 3] < 128) continue;
                    label[static_cast<size_t>(q)] = next;
                    stack.push_back(q);
                }
        }
        ++next;
        const float w = static_cast<float>(x1 - x0 + 1) / probe, h = static_cast<float>(y1 - y0 + 1) / probe;
        if (h >= minHeight && h >= minAspect * w)
            out.push_back({static_cast<float>(x0) / probe, static_cast<float>(y0) / probe, w, h});
    }
    std::sort(out.begin(), out.end(), [](const BladeRect& a, const BladeRect& b) { return a.u < b.u; });
    return out;
}

}  // namespace ts::asset
