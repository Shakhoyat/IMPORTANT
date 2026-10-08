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
    void begin(const char* pass);  // passes must not nest
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
        GLuint query[kLatency] = {0, 0, 0};
        bool issued[kLatency] = {false, false, false};
    };
    std::vector<Pass> passes_;
    std::vector<Entry> entries_;
    int current_ = -1;
    int frame_ = 0;
};

}  // namespace ts
