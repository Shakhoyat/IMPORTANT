#include "vision/VisionWorker.h"

#include <chrono>
#include <utility>

namespace ts {

VisionResult processVision(VisionJob& job, std::vector<SensorModel>& sensors, ThermalTracker& tracker,
                           std::vector<uint8_t>& lastImage0) {
    VisionResult r;
    r.paused = job.paused;
    for (int u = 0; u < VisionJob::kUavs; ++u) {
        if (!job.rendered[u]) continue;
        r.valid[u] = true;
        r.reported[u] = job.reported[u];
        std::vector<uint8_t>& img = job.image[u];
        if (!job.paused && u < static_cast<int>(sensors.size())) {
            sensors[u].apply(img, job.width, job.height);
            const std::vector<Measurement2D> blobs = cv::detectAll(img, job.width, job.height, CvConfig{}, &r.stats[u]);
            r.targets[u] = cv::blobsOfClass(blobs, BlobClass::Target);
            r.persons[u] = cv::blobsOfClass(blobs, BlobClass::Person);
        }
        if (u == 0) lastImage0 = img;
        r.image[u] = std::move(img);
    }
    if (!job.paused && !lastImage0.empty()) {
        r.detection = tracker.process(lastImage0, job.width, job.height);
        r.hasDetection = true;
    }
    return r;
}

VisionWorker::VisionWorker(std::vector<SensorModel>& sensors, ThermalTracker& tracker, bool threaded)
    : sensors_(sensors), tracker_(tracker), threaded_(threaded) {
    if (threaded_) thread_ = std::thread([this] { run(); });
}

VisionWorker::~VisionWorker() {
    if (threaded_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }
}

void VisionWorker::submit(VisionJob job) {
    if (inFlight_) join();   // never two jobs: the sensors' random sequences must stay in tick order
    inFlight_ = true;
    if (!threaded_) {
        const auto t0 = std::chrono::steady_clock::now();
        result_ = processVision(job, sensors_, tracker_, lastImage0_);
        lastWorkMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = std::move(job);
        hasJob_ = true;
        done_ = false;
    }
    cv_.notify_all();
}

VisionResult VisionWorker::join() {
    inFlight_ = false;
    if (threaded_) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_; });
        done_ = false;
    }
    return std::move(result_);
}

void VisionWorker::run() {
    for (;;) {
        VisionJob job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return hasJob_ || quit_; });
            if (quit_) return;
            job = std::move(job_);
            hasJob_ = false;
        }
        const auto t0 = std::chrono::steady_clock::now();
        VisionResult r = processVision(job, sensors_, tracker_, lastImage0_);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(r);
            lastWorkMs_ = ms;
            done_ = true;
        }
        cv_.notify_all();
    }
}

}  // namespace ts
