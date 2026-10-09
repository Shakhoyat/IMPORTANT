// v9 M1: the UAV image processing (sensor model -> CV blob stage -> v4 single-target tracker) on a worker thread.
//
// The main thread renders the two UAV cameras at the simulation tick, reads the images back after the main frame has
// been submitted and hands them over (submit). The worker runs the per-image CPU work while the main thread presents
// the frame and starts the next one. The results go back into the world at a FIXED point: the start of the next
// simulation tick (join), exactly where the single-threaded version applied them. So a run with the worker is
// bit-identical to one without (same order of random draws in each SensorModel, same measurements before the same
// tick); only the wall time changes. Threaded = false runs the same code inline (TS_VISION_THREAD=0, tests).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "math/Math3D.h"
#include "vision/CvPipeline.h"
#include "vision/SensorModel.h"
#include "vision/ThermalTracker.h"

namespace ts {

struct VisionJob {
    static constexpr int kUavs = 2;
    int width = 0, height = 0;
    bool paused = false;                  // world paused: raw images to the feed, no sensor model / CV / tracker
    bool rendered[kUavs] = {false, false};   // per UAV: an image arrived (false = dropped frame)
    std::vector<uint8_t> image[kUavs];    // ideal heat images (read back from the GPU)
    Mat4 reported[kUavs];                 // gimbal pose the measurements are geolocated with
};

struct VisionResult {
    static constexpr int kUavs = VisionJob::kUavs;
    bool paused = false;
    bool valid[kUavs] = {false, false};             // this UAV's image was processed
    std::vector<uint8_t> image[kUavs];              // after the sensor model (what the feed shows / the CV saw)
    std::vector<Measurement2D> targets[kUavs];      // Target-class blobs (to the squad tracker)
    std::vector<Measurement2D> persons[kUavs];      // Person-class blobs (HUD brackets)
    CvFrameStats stats[kUavs];
    Mat4 reported[kUavs];
    bool hasDetection = false;                      // the v4 single-target tracker ran (not paused)
    Detection detection;
};

// One job, start to end (the work the worker thread does). 'lastImage0' carries UAV 0's latest processed image: the
// v4 tracker reads it every tick, also when UAV 0's frame was dropped (as the single-threaded loop did).
VisionResult processVision(VisionJob& job, std::vector<SensorModel>& sensors, ThermalTracker& tracker,
                           std::vector<uint8_t>& lastImage0);

class VisionWorker {
public:
    VisionWorker(std::vector<SensorModel>& sensors, ThermalTracker& tracker, bool threaded);
    ~VisionWorker();
    VisionWorker(const VisionWorker&) = delete;
    VisionWorker& operator=(const VisionWorker&) = delete;

    void submit(VisionJob job);   // a job must not be in flight (join first)
    bool inFlight() const { return inFlight_; }
    VisionResult join();          // waits for the job in flight (inFlight() must be true)
    bool threaded() const { return threaded_; }
    double lastWorkMs() const { return lastWorkMs_; }   // CPU time of the latest job (wall clock on its thread)

private:
    void run();
    std::vector<SensorModel>& sensors_;
    ThermalTracker& tracker_;
    std::vector<uint8_t> lastImage0_;
    bool threaded_;
    bool inFlight_ = false;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool hasJob_ = false, done_ = false, quit_ = false;
    VisionJob job_;
    VisionResult result_;
    double lastWorkMs_ = 0.0;
};

}  // namespace ts
