// Per-pass GPU timings with GL_TIME_ELAPSED queries (core in OpenGL 3.3).
// Results are read three frames later so the CPU never waits for the GPU; values are smoothed.
#pragma once

#include <glad/gl.h>

#include <string>
#include <vector>

namespace ts {

class GpuTimer {
public:
    ~GpuTimer();
    // v9 M6: passes may nest; an inner pass's time is excluded from the outer one (the outer pass is suspended and
    // resumed as another query segment), so the per-pass times still add up to the frame.
    void begin(const char* pass);
    void end();
    void endFrame();               // collect finished queries
    struct Entry {
        std::string name;
        double ms = 0.0;       // exponentially smoothed
        double sumMs = 0.0;    // accumulated since reset (for benchmarks)
        int samples = 0;
    };
    const std::vector<Entry>& entries() const { return entries_; }
    void resetTotals();

private:
    static constexpr int kLatency = 3;
    // v7 WP-Q: TS_GPU_SYNC_TIMING=1 replaces the queries by glFinish-bracketed wall clock (profiling only: it
    // serialises CPU and GPU). Mesa d3d12 time-elapsed queries include idle gaps between command lists.
    bool syncTiming_ = false, syncChecked_ = false;
    double syncStart_ = 0.0;
    struct Pass {
        std::vector<GLuint> query[kLatency];   // query segments of this pass in frame slot k
        int used[kLatency] = {0, 0, 0};
        int frameOf[kLatency] = {-1, -1, -1};
        double syncMs = 0.0;                   // TS_GPU_SYNC_TIMING: segments of the current frame
    };
    void startSegment(int index);
    void stopSegment();
    std::vector<Pass> passes_;
    std::vector<Entry> entries_;
    std::vector<int> stack_;                    // suspended outer passes
    int current_ = -1;
    int frame_ = 0;
};

}  // namespace ts
