// Image stage of the v5 computer vision (per UAV, per frame, on its own 320 x 240 heat image):
//   1. adaptive threshold      t = max(fixed floor, Otsu threshold of the frame's histogram)
//   2. morphological despeckle remove isolated hot pixels (fewer than 2 hot 8-neighbours): sparks, grass glints
//   3. connected components    two-pass labelling with union-find (8-connectivity)
//   4. blob features           area, centroid, box, mean / peak heat, central moments -> orientation, elongation
//   5. target filter           area range + a hot core (engine block) + not a long thin streak (tracer, wire)
// The measurements go to the multi-target tracker (vision/MultiTracker.h).
#pragma once

#include <cstdint>
#include <vector>

namespace ts {

struct CvConfig {
    uint8_t floor = 204;      // heat >= 0.80 always needed for a target pixel
    uint8_t core = 230;       // a target blob must contain a pixel >= 0.90 (hot engine)
    int minArea = 2;          // pixels
    int maxArea = 2500;       // pixels (a blob bigger than ~1/30 of the frame is a fire, not a rover)
    float maxElongation = 6.0f;  // major / minor axis ratio; tracers and hot wires are thin streaks
    bool despeckle = true;
};

struct Measurement2D {
    float u = 0.0f, v = 0.0f;   // centroid, normalised [-1, 1], +u right, +v up
    float cx = 0.0f, cy = 0.0f; // centroid in pixels (row 0 = top)
    int area = 0;
    float meanHeat = 0.0f;      // 0..1
    uint8_t peak = 0;
    float orientationDeg = 0.0f;  // major axis angle from the image x axis
    float elongation = 1.0f;      // sqrt(lambda_max / lambda_min) of the second central moments
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
};

struct CvFrameStats {
    uint8_t threshold = 0;     // threshold actually used
    uint8_t otsu = 0;          // Otsu's threshold of the frame
    int hotPixels = 0;         // after thresholding
    int removedSpeckles = 0;   // pixels removed by the despeckle step
    int components = 0;        // connected components
    int targets = 0;           // components passing the target filter
};

namespace cv {

// Otsu's method: the level t that maximises the between-class variance of the 256-bin histogram
// (dark class = levels <= t, bright class = levels > t).
uint8_t otsuThreshold(const std::vector<uint8_t>& image);
// Removes mask pixels with fewer than 'minNeighbours' set 8-neighbours. Returns the number removed.
int despeckle(std::vector<uint8_t>& mask, int width, int height, int minNeighbours = 2);
// Two-pass connected-component labelling (8-connectivity, union-find with path compression).
// labels[i] = 0 for background, 1..count for components. Returns count.
int labelComponents(const std::vector<uint8_t>& mask, int width, int height, std::vector<int>& labels);
// The whole image stage.
std::vector<Measurement2D> detectTargets(const std::vector<uint8_t>& image, int width, int height,
                                         const CvConfig& cfg = {}, CvFrameStats* stats = nullptr);

}  // namespace cv

}  // namespace ts
