// v9 M6: 8-bit RGBA images (stb_image, public domain, include/stb_image.h) and the mip chain used for the alpha-tested
// foliage. Pure CPU.
//
// Coverage-preserving mipmaps (Castano 2010, "Computing alpha mipmaps"): a plain box filter averages the alpha of thin
// needles / blades towards 0, so past a few mip levels the alpha test removes them and distant foliage thins out and
// shimmers as the mip level changes with distance. Each level's alpha is instead scaled so the fraction of texels that
// pass the cut-off equals that fraction at level 0 (binary search on the scale).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ts::asset {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // rows top to bottom (as stored in the file)
    bool empty() const { return rgba.empty(); }
    uint8_t* at(int x, int y) { return &rgba[(static_cast<size_t>(y) * w + x) * 4]; }
    const uint8_t* at(int x, int y) const { return &rgba[(static_cast<size_t>(y) * w + x) * 4]; }
};

bool loadImage(const std::string& path, Image& out);                 // any stb format, forced to RGBA
bool loadImageMemory(const uint8_t* data, size_t size, Image& out);
// Replaces the alpha channel by the luminance of 'mask' (resampled if the sizes differ).
void applyAlphaMask(Image& img, const Image& mask);
Image downsample(const Image& src);                                  // 2x2 box (sizes rounded down, min 1)
Image resizeBox(const Image& src, int w, int h);                     // area average (downscale only)
// Fraction of texels with alpha >= cutoff (0..1).
float alphaCoverage(const Image& img, float cutoff);
// Scales the alpha of 'img' so alphaCoverage(img, cutoff) is as close as possible to 'target'. Returns the scale.
float preserveCoverage(Image& img, float cutoff, float target);
// Full mip chain (level 0 = src). With cutoff > 0 every level keeps the coverage of level 0.
std::vector<Image> buildMips(const Image& src, float cutoff);
// Pushes colour into fully transparent texels from their opaque neighbours ('passes' rings), so filtering and mip
// averaging never bleed the black / white background of the source into the leaf edges (dark or bright halos).
void dilateColour(Image& img, int passes);

// v9 M6 grass atlas: bounding rectangles (normalised u0, v0, width, height; v down) of the tall, separate blades of an
// alpha atlas (grass_medium_02: single blades side by side). Connected components of alpha >= 0.5 on a 'probe'-sized
// copy; kept when taller than 'minHeight' (fraction of the image) and at least 'minAspect' times taller than wide.
struct BladeRect {
    float u, v, w, h;
};
std::vector<BladeRect> findBladeRects(const Image& img, int probe = 256, float minHeight = 0.3f, float minAspect = 2.5f);

}  // namespace ts::asset
