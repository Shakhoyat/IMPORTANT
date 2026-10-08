// Image stage of the v5 computer vision (per UAV, per frame, on its own 320 x 240 heat image):
//   1. adaptive threshold      t = max(fixed floor, Otsu level + 1); Otsu level = last level of the dark class
//   2. morphological despeckle remove isolated hot pixels (fewer than 2 hot 8-neighbours): sparks, grass glints
//   3. connected components    two-pass labelling with union-find (8-connectivity)
//   4. blob features           area, centroid, box, mean / peak heat, central moments -> orientation, elongation
//   5. target filter           area range + a hot core (engine block) + not a long thin streak (tracer, wire)
// The measurements go to the multi-target tracker (vision/MultiTracker.h).
// v6: detectAll() keeps every blob and classifies it (BlobClass): Target (exactly detectTargets' filter), Person
// (warm, no hot core, upright or compact: a soldier, never engaged), HotClutter (hot core but the wrong size /
// shape: fire, tracer, glint pair) and Clutter (any other warm blob: sun-heated wall, pipe).
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
    // v6 person class (detectAll only; detectTargets never looks at these)
    int personMinArea = 4;       // pixels
    int personMaxArea = 600;     // a soldier close to the camera
    float personMaxElongation = 5.0f;
    float personMinTiltDeg = 45.0f;  // major axis at least this far from the image x axis (upright figure)...
    float personCompact = 1.25f;     // ...unless the blob is compact (seen from above): elongation below this
};

enum class BlobClass { Target, Person, HotClutter, Clutter };
inline const char* blobClassName(BlobClass c) {
    switch (c) {
        case BlobClass::Target: return "target";
        case BlobClass::Person: return "person";
        case BlobClass::HotClutter: return "hot clutter";
        case BlobClass::Clutter: return "clutter";
    }
    return "?";
}

struct Measurement2D {
    float u = 0.0f, v = 0.0f;   // centroid, normalised [-1, 1], +u right, +v up
    float cx = 0.0f, cy = 0.0f; // centroid in pixels (row 0 = top)
    int area = 0;
    float meanHeat = 0.0f;      // 0..1
    uint8_t peak = 0;
    float orientationDeg = 0.0f;  // major axis angle from the image x axis
    float elongation = 1.0f;      // sqrt(lambda_max / lambda_min) of the second central moments
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    BlobClass cls = BlobClass::Target;  // set by detectAll (detectTargets returns targets only)
};

struct CvFrameStats {
    uint8_t threshold = 0;     // threshold actually used
    uint8_t otsu = 0;          // Otsu's threshold of the frame
    int hotPixels = 0;         // after thresholding
    int removedSpeckles = 0;   // pixels removed by the despeckle step
    int components = 0;        // connected components
    int targets = 0;           // components passing the target filter
    int persons = 0;           // v6 detectAll classes (0 after detectTargets)
    int hotClutter = 0;
    int clutter = 0;
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
// v6: the same image stage, every component kept with its class (cls). The Target blobs are exactly
// detectTargets' result, in the same order; stats also count persons / hot clutter / clutter.
std::vector<Measurement2D> detectAll(const std::vector<uint8_t>& image, int width, int height,
                                     const CvConfig& cfg = {}, CvFrameStats* stats = nullptr);
// The class of one measured blob (the rule detectAll applies).
BlobClass classifyBlob(const Measurement2D& m, const CvConfig& cfg = {});
// Only the blobs of class 'c' (e.g. the targets of a detectAll result, for the squad).
std::vector<Measurement2D> blobsOfClass(const std::vector<Measurement2D>& blobs, BlobClass c);

// Intermediate images of the image stage, for the lab figures (docs/v5_notes/v5-f2.md). Does not change detectTargets.
struct CvComponent {
    int label = 0, area = 0;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    bool accepted = false;  // passed the target filter (it is one of 'targets')
};
struct CvDebug {
    std::vector<uint8_t> mask;     // 1 = hot pixel after thresholding (before despeckle)
    std::vector<uint8_t> clean;    // after despeckle
    std::vector<uint8_t> removed;  // 1 = removed by despeckle (mask and not clean)
    std::vector<int> labels;       // connected components of 'clean' (0 = background)
    std::vector<CvComponent> components;
    std::vector<Measurement2D> targets;  // exactly what detectTargets returns
    CvFrameStats stats;
};
CvDebug detectTargetsDebug(const std::vector<uint8_t>& image, int width, int height, const CvConfig& cfg = {});

}  // namespace cv

}  // namespace ts
