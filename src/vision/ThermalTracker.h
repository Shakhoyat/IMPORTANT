// Computer-vision target detection on the UAV's thermal camera image.
// Pipeline per frame: hysteresis threshold (blob pixels >= 0.80, blob must contain a core >= 0.90)
// -> connected-component labelling (8-neighbour flood fill) -> blob features (area, centroid, box)
// -> data association with the previous track.
#pragma once

#include <cstdint>
#include <vector>

namespace ts {

struct Blob {
    int area = 0;
    float cx = 0.0f, cy = 0.0f;        // centroid in pixels (row 0 = top)
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    float meanHeat = 0.0f;             // 0..1
    uint8_t peak = 0;                  // hottest pixel
};

struct Detection {
    bool found = false;
    float u = 0.0f, v = 0.0f;          // centroid in normalised image coords, [-1,1], +u right, +v up
    float boxMinU = 0.0f, boxMinV = 0.0f, boxMaxU = 0.0f, boxMaxV = 0.0f;
    int area = 0;
    int blobCount = 0;                 // hot blobs seen this frame (before association)
};

struct TrackerConfig {
    uint8_t threshold = 204;  // heat >= 0.80: pixel belongs to a warm blob
    uint8_t core = 230;       // heat >= 0.90: a blob needs a hot core (engine) to count as a target
    int minArea = 2;          // pixels; smaller blobs are treated as noise
    float gate = 0.45f;       // max centroid jump (normalised units) to keep the same track
};

class ThermalTracker {
public:
    explicit ThermalTracker(TrackerConfig cfg = {}) : cfg_(cfg) {}

    // image: width*height heat values, row 0 = top of the image.
    Detection process(const std::vector<uint8_t>& image, int width, int height);

    // Exposed for unit tests.
    static std::vector<Blob> findBlobs(const std::vector<uint8_t>& image, int width, int height, uint8_t threshold,
                                       int minArea);

    bool hasTrack() const { return hasTrack_; }
    void reset() { hasTrack_ = false; }

private:
    TrackerConfig cfg_;
    bool hasTrack_ = false;
    float lastU_ = 0.0f, lastV_ = 0.0f;
};

}  // namespace ts
