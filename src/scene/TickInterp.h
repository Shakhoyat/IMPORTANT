// v9 M1: fixed simulation tick + render interpolation ("fix your timestep").
//
// The world advances in fixed 1/60 s ticks, whatever the display rate: the same seed and the same per-tick input give
// the same run (replays, tests), and physics / AI never see a long or uneven step. Rendering happens as often as the
// display allows; each frame shows the world part-way between the last two ticks (alpha = leftover time / tick), so
// motion is smooth at 100 / 144 Hz and never stutters when a frame is late.
//
//   FixedStepClock   accumulator: frame time in -> number of ticks to run + alpha for the render
//   blendTickWorld   part matrix between two ticks (element-wise for small motions, refuses cuts / spinning parts)
//   blendTickPose    camera pose between two ticks (lerp eye / fov, slerp orientation, refuses cuts)
#pragma once

#include "math/Math3D.h"
#include "scene/CameraRig.h"

namespace ts {

class FixedStepClock {
public:
    static constexpr float kTick = 1.0f / 60.0f;
    static constexpr double kTickSeconds = 1.0 / 60.0;   // the accumulator's (exact double) tick
    static constexpr int kMaxTicksPerFrame = 4;   // a long hitch drops the backlog instead of spiralling

    // Adds one frame's wall time; returns how many ticks to run now (0..kMaxTicksPerFrame).
    int advance(double frameSeconds);
    // Share of a tick left over after the ticks of this frame (0 <= alpha < 1): the render blend factor.
    float alpha() const { return static_cast<float>(accum_ / kTickSeconds); }
    long long ticks() const { return ticks_; }
    long long dropped() const { return dropped_; }   // ticks skipped because of hitches
    void reset() { accum_ = 0.0; ticks_ = 0; dropped_ = 0; }

private:
    double accum_ = 0.0;
    long long ticks_ = 0, dropped_ = 0;
};

// World matrix of a part between the previous tick (a) and the latest (b). Returns false (out = b) when the part
// jumped (> maxMove metres: teleport, respawn) or turned more than maxTurnDeg in one tick (fast rotors, cuts): blending
// such motion would shrink or smear the part.
bool blendTickWorld(const Mat4& a, const Mat4& b, float t, Mat4& out, float maxMove = 4.0f, float maxTurnDeg = 35.0f);

// Camera pose between two ticks. Returns false (out = b) on a cut (eye moved > maxMove or turned > maxTurnDeg).
bool blendTickPose(const RigPose& a, const RigPose& b, float t, RigPose& out, float maxMove = 4.0f, float maxTurnDeg = 75.0f);

}  // namespace ts
