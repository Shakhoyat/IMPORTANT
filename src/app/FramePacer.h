// v9 M1: frame limiter for the interactive loop (instead of a blocking vsync swap). Deadline based: each frame is due
// one period after the previous deadline, so short and long frames even out; a frame later than a whole period resets
// the schedule (no burst of catch-up frames). Sleeps on a high-resolution timer (Windows 10+) or nanosleep and spins
// only the last half millisecond.
#pragma once

namespace ts {

class FramePacer {
public:
    // Waits until the next frame is due at 'hz' frames per second (hz <= 0: returns at once, no pacing).
    void wait(float hz);

private:
    double next_ = -1.0;
};

double pacerNow();                       // monotonic seconds
void preciseSleepUntil(double deadline); // in pacerNow() seconds

}  // namespace ts
