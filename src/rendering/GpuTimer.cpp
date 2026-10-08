#include "rendering/GpuTimer.h"

#include <chrono>
#include <cstdlib>

namespace ts {

GpuTimer::~GpuTimer() {
    for (Pass& p : passes_) glDeleteQueries(kLatency, p.query);
}

namespace {

double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void GpuTimer::begin(const char* pass) {
    if (!syncChecked_) {
        const char* v = std::getenv("TS_GPU_SYNC_TIMING");
        syncTiming_ = v && *v == '1';
        syncChecked_ = true;
    }
    int index = -1;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].name == pass) index = static_cast<int>(i);
    if (index < 0) {
        index = static_cast<int>(entries_.size());
        entries_.push_back({pass});
        passes_.emplace_back();
        glGenQueries(kLatency, passes_.back().query);
    }
    current_ = index;
    if (syncTiming_) {
        glFinish();
        syncStart_ = wallSeconds();
        return;
    }
    const int slot = frame_ % kLatency;
    glBeginQuery(GL_TIME_ELAPSED, passes_[index].query[slot]);
    passes_[index].issued[slot] = true;
}

void GpuTimer::end() {
    if (current_ < 0) return;
    if (syncTiming_) {
        glFinish();
        const double ms = 1000.0 * (wallSeconds() - syncStart_);
        Entry& e = entries_[current_];
        e.ms = e.samples == 0 ? ms : 0.9 * e.ms + 0.1 * ms;
        e.sumMs += ms;
        ++e.samples;
        current_ = -1;
        return;
    }
    glEndQuery(GL_TIME_ELAPSED);
    current_ = -1;
}

void GpuTimer::endFrame() {
    ++frame_;
    const int slot = frame_ % kLatency;  // the oldest slot, about to be reused
    for (size_t i = 0; i < passes_.size(); ++i) {
        Pass& p = passes_[i];
        if (!p.issued[slot]) continue;
        GLint ready = 0;
        glGetQueryObjectiv(p.query[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) continue;
        GLuint64 ns = 0;
        glGetQueryObjectui64v(p.query[slot], GL_QUERY_RESULT, &ns);
        p.issued[slot] = false;
        const double ms = static_cast<double>(ns) * 1e-6;
        Entry& e = entries_[i];
        e.ms = e.samples == 0 ? ms : 0.9 * e.ms + 0.1 * ms;
        e.sumMs += ms;
        ++e.samples;
    }
}

void GpuTimer::resetTotals() {
    for (Entry& e : entries_) {
        e.sumMs = 0.0;
        e.samples = 0;
    }
}

}  // namespace ts
