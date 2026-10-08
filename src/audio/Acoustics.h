// v7 camp acoustics (pure functions, no state): where the listener / a source is (bunker reverb zone), what a foot
// lands on (surface classes for footsteps), and the discrete echoes of loud reports off the camp structures
// (image-source taps off the HESCO perimeter wall and the treeline / hills behind it).
#pragma once

#include "math/Math3D.h"

namespace ts::audio {

constexpr float kSpeedOfSound = 343.0f;   // m/s

// 0 = open air, 1 = inside the underground bunker rooms; the entrance ramp trench fades from 0.5 at the blast door
// to 0 at ground level.
float bunkerAmount(const Vec3& p);
inline bool insideBunker(const Vec3& p) { return bunkerAmount(p) > 0.75f; }

enum class Surface { Dirt, Gravel, Concrete, Metal, Grass };
constexpr int kSurfaceCount = 5;
const char* surfaceName(Surface s);
// Ground class under a foot at p (world): bunker / ramp / helipad concrete, raised decks metal, road / apron gravel,
// inner compound packed dirt, outside the base grass.
Surface surfaceAt(const Vec3& p);

// One discrete reflection of a source (image-source method). delay = (|L - S'| - |L - S|) / c.
struct EchoTap {
    float delaySec = 0.0f;
    float gain = 0.0f;       // reflectivity x sqrt(direct / path) (game-legible spreading, < 1)
    float cutoffHz = 20000.0f;
    Vec3 image;              // image source position (for panning)
};
constexpr float kPerimeterWallRadius = 34.0f;   // HESCO wall (layout::kPerimeterRadius)
constexpr float kTreelineRadius = 72.0f;        // backdrop treeline / low hills
// Fills up to 2 taps (wall, treeline); returns the count. No taps when either end is inside the bunker.
int echoTaps(const Vec3& listener, const Vec3& source, EchoTap taps[2]);

// Arrival delay of a direct sound (s): the first 20 m are free (keeps near events frame-tight), capped at 1 s.
float propagationDelay(float distance);

}  // namespace ts::audio
