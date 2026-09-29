#include "vision/ThermalTracker.h"

#include <cmath>

namespace ts {

std::vector<Blob> ThermalTracker::findBlobs(const std::vector<uint8_t>& image, int width, int height,
                                            uint8_t threshold, int minArea) {
    std::vector<Blob> blobs;
    if (width <= 0 || height <= 0 || image.size() < static_cast<size_t>(width) * height) return blobs;

    std::vector<uint8_t> visited(image.size(), 0);
    std::vector<int> stack;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int seed = y * width + x;
            if (visited[seed] || image[seed] < threshold) continue;

            // Flood fill one connected component (8-neighbourhood).
            Blob b;
            b.minX = b.maxX = x;
            b.minY = b.maxY = y;
            float sumX = 0.0f, sumY = 0.0f, sumHeat = 0.0f;
            stack.clear();
            stack.push_back(seed);
            visited[seed] = 1;
            while (!stack.empty()) {
                const int p = stack.back();
                stack.pop_back();
                const int px = p % width, py = p / width;
                ++b.area;
                sumX += static_cast<float>(px);
                sumY += static_cast<float>(py);
                sumHeat += static_cast<float>(image[p]) / 255.0f;
                if (image[p] > b.peak) b.peak = image[p];
                if (px < b.minX) b.minX = px;
                if (px > b.maxX) b.maxX = px;
                if (py < b.minY) b.minY = py;
                if (py > b.maxY) b.maxY = py;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = px + dx, ny = py + dy;
                        if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                        const int q = ny * width + nx;
                        if (visited[q] || image[q] < threshold) continue;
                        visited[q] = 1;
                        stack.push_back(q);
                    }
                }
            }
            if (b.area < minArea) continue;
            b.cx = sumX / static_cast<float>(b.area);
            b.cy = sumY / static_cast<float>(b.area);
            b.meanHeat = sumHeat / static_cast<float>(b.area);
            blobs.push_back(b);
        }
    }
    return blobs;
}

Detection ThermalTracker::process(const std::vector<uint8_t>& image, int width, int height) {
    Detection det;
    const std::vector<Blob> blobs = findBlobs(image, width, height, cfg_.threshold, cfg_.minArea);
    det.blobCount = static_cast<int>(blobs.size());

    auto toU = [width](float px) { return (px + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f; };
    auto toV = [height](float py) { return 1.0f - (py + 0.5f) / static_cast<float>(height) * 2.0f; };

    const Blob* best = nullptr;
    float bestScore = 0.0f;
    for (const Blob& b : blobs) {
        if (b.peak < cfg_.core) continue;  // warm but no hot core: generator, vents, sun-warmed rock...
        const float u = toU(b.cx), v = toV(b.cy);
        float score;
        if (hasTrack_) {
            // Keep following the same target: nearest blob inside the gate.
            const float d = std::sqrt((u - lastU_) * (u - lastU_) + (v - lastV_) * (v - lastV_));
            if (d > cfg_.gate) continue;
            score = 1.0f / (0.01f + d);
        } else {
            // New acquisition: biggest and hottest blob.
            score = static_cast<float>(b.area) * b.meanHeat;
        }
        if (!best || score > bestScore) {
            best = &b;
            bestScore = score;
        }
    }

    if (!best) {
        hasTrack_ = false;
        return det;
    }
    det.found = true;
    det.u = toU(best->cx);
    det.v = toV(best->cy);
    det.boxMinU = toU(static_cast<float>(best->minX) - 0.5f);
    det.boxMaxU = toU(static_cast<float>(best->maxX) + 0.5f);
    det.boxMaxV = toV(static_cast<float>(best->minY) - 0.5f);
    det.boxMinV = toV(static_cast<float>(best->maxY) + 0.5f);
    det.area = best->area;
    hasTrack_ = true;
    lastU_ = det.u;
    lastV_ = det.v;
    return det;
}

}  // namespace ts
