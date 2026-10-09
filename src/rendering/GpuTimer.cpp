#include "rendering/GpuTimer.h"

#include <chrono>
#include <cstdlib>

namespace ts {

GpuTimer::~GpuTimer() {
    for (Pass& p : passes_)
        for (auto& q : p.query)
            if (!q.empty()) glDeleteQueries(static_cast<GLsizei>(q.size()), q.data());
}

namespace {

double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void GpuTimer::startSegment(int index) {
    current_ = index;
    if (syncTiming_) {
        glFinish();
        syncStart_ = wallSeconds();
        return;
    }
    const int slot = frame_ % kLatency;
    Pass& p = passes_[index];
    if (p.frameOf[slot] != frame_) {   // first segment of this frame in the slot (unread older results are dropped)
        p.frameOf[slot] = frame_;
        p.used[slot] = 0;
    }
    if (p.used[slot] >= static_cast<int>(p.query[slot].size())) {
        GLuint q = 0;
        glGenQueries(1, &q);
        p.query[slot].push_back(q);
    }
    glBeginQuery(GL_TIME_ELAPSED, p.query[slot][static_cast<size_t>(p.used[slot])]);
    ++p.used[slot];
}

void GpuTimer::stopSegment() {
    if (current_ < 0) return;
    if (syncTiming_) {
        glFinish();
        passes_[current_].syncMs += 1000.0 * (wallSeconds() - syncStart_);
    } else {
        glEndQuery(GL_TIME_ELAPSED);
    }
    current_ = -1;
}

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
    }
    if (current_ >= 0) {   // nested: suspend the outer pass
        stack_.push_back(current_);
        stopSegment();
    }
    startSegment(index);
}

void GpuTimer::end() {
    if (current_ < 0) return;
    stopSegment();
    if (!stack_.empty()) {
        const int outer = stack_.back();
        stack_.pop_back();
        startSegment(outer);
    }
}

void GpuTimer::endFrame() {
    if (syncTiming_) {
        for (size_t i = 0; i < passes_.size(); ++i) {
            Pass& p = passes_[i];
            if (p.syncMs <= 0.0) continue;
            Entry& e = entries_[i];
            e.ms = e.samples == 0 ? p.syncMs : 0.9 * e.ms + 0.1 * p.syncMs;
            e.sumMs += p.syncMs;
            ++e.samples;
            p.syncMs = 0.0;
        }
        ++frame_;
        return;
    }
    ++frame_;
    const int slot = frame_ % kLatency;  // the oldest slot, about to be reused
    for (size_t i = 0; i < passes_.size(); ++i) {
        Pass& p = passes_[i];
        if (p.used[slot] == 0) continue;
        bool ready = true;
        for (int k = 0; k < p.used[slot] && ready; ++k) {
            GLint r = 0;
            glGetQueryObjectiv(p.query[slot][static_cast<size_t>(k)], GL_QUERY_RESULT_AVAILABLE, &r);
            ready = r != 0;
        }
        if (!ready) continue;
        double ms = 0.0;
        for (int k = 0; k < p.used[slot]; ++k) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(p.query[slot][static_cast<size_t>(k)], GL_QUERY_RESULT, &ns);
            ms += static_cast<double>(ns) * 1e-6;
        }
        p.used[slot] = 0;
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
