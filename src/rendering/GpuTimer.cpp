#include "rendering/GpuTimer.h"

namespace ts {

GpuTimer::~GpuTimer() {
    for (Pass& p : passes_) glDeleteQueries(kLatency, p.query);
}

void GpuTimer::begin(const char* pass) {
    int index = -1;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].name == pass) index = static_cast<int>(i);
    if (index < 0) {
        index = static_cast<int>(entries_.size());
        entries_.push_back({pass});
        passes_.emplace_back();
        glGenQueries(kLatency, passes_.back().query);
    }
    const int slot = frame_ % kLatency;
    glBeginQuery(GL_TIME_ELAPSED, passes_[index].query[slot]);
    passes_[index].issued[slot] = true;
    current_ = index;
}

void GpuTimer::end() {
    if (current_ < 0) return;
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
