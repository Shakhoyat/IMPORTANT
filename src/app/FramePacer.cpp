#include "app/FramePacer.h"

#include <chrono>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace ts {

double pacerNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void preciseSleepUntil(double deadline) {
    constexpr double kSpin = 0.0005;   // the last 0.5 ms: yield-spin (sleep granularity)
    double left = deadline - pacerNow();
    if (left <= 0.0) return;
#if defined(_WIN32)
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (left > kSpin) {
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>((left - kSpin) * 1e7);   // relative, 100 ns units
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
        } else if (left > 0.002) {
            Sleep(static_cast<DWORD>((left - 0.002) * 1000.0));   // old Windows: coarse sleep, spin the rest
        }
    }
#else
    if (left > kSpin) std::this_thread::sleep_for(std::chrono::duration<double>(left - kSpin));
#endif
    while (pacerNow() < deadline) std::this_thread::yield();
}

void FramePacer::wait(float hz) {
    if (hz <= 0.0f) {
        next_ = -1.0;
        return;
    }
    const double period = 1.0 / hz, now = pacerNow();
    if (next_ < 0.0) next_ = now;
    next_ += period;
    if (next_ <= now) {                          // this frame took longer than a period: no wait
        if (now - next_ > period) next_ = now;   // more than a frame behind: restart the schedule from now
        return;
    }
    preciseSleepUntil(next_);
}

}  // namespace ts
