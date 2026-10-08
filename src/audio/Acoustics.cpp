#include "audio/Acoustics.h"

#include <algorithm>
#include <cmath>

#include "audio/Dsp.h"
#include "objects/BaseLayout.h"
#include "objects/Terrain.h"

namespace ts::audio {

namespace L = ts::layout;

float bunkerAmount(const Vec3& p) {
    if (!std::isfinite(p.x + p.y + p.z)) return 0.0f;
    const bool inPlan = p.x > L::kBunkerMinX - 0.3f && p.x < L::kBunkerMaxX + 0.3f && p.z > L::kBunkerMinZ - 0.3f &&
                        p.z < L::kBunkerMaxZ + 0.3f;
    if (inPlan && p.y < L::kBunkerCeilingY + 0.3f && p.y > L::kBunkerFloorY - 0.6f) return 1.0f;
    // Entrance trench: walls on both sides, open sky. Half the room's reverb at the door, none at the top.
    if (std::fabs(p.x) < L::kRampHalfWidth + 0.4f && p.z >= L::kBunkerMaxZ && p.z < L::kRampTopZ && p.y < 0.5f) {
        const float u = (p.z - L::kBunkerMaxZ) / (L::kRampTopZ - L::kBunkerMaxZ);
        return 0.5f * (1.0f - clampf(u, 0.0f, 1.0f));
    }
    return 0.0f;
}

const char* surfaceName(Surface s) {
    switch (s) {
        case Surface::Dirt: return "dirt";
        case Surface::Gravel: return "gravel";
        case Surface::Concrete: return "concrete";
        case Surface::Metal: return "metal";
        case Surface::Grass: return "grass";
    }
    return "dirt";
}

Surface surfaceAt(const Vec3& p) {
    if (bunkerAmount(p) > 0.0f) return Surface::Concrete;   // rooms and the ramp's concrete floor
    const float r = std::sqrt(p.x * p.x + p.z * p.z);
    const float ground = r < L::kBaseRadius ? terrainHeight(p.x, p.z) : L::kApronLevel;
    if (p.y > ground + 0.6f) return Surface::Metal;          // tower floors, vehicle decks, stairs
    const Vec3 hp = L::kHelipad;
    if (std::hypot(p.x - hp.x, p.z - hp.z) < L::kHelipadRadius) return Surface::Concrete;
    if (std::fabs(p.x) < L::kRoadHalfWidth && p.z > 18.0f) return Surface::Gravel;   // gate road
    if (r < 19.0f) return Surface::Dirt;
    if (r < L::kBaseRadius) return Surface::Gravel;
    return Surface::Grass;
}

namespace {
// Image of 's' mirrored across the vertical plane through 'q' with unit normal 'n' (horizontal).
Vec3 mirror(const Vec3& s, const Vec3& q, const Vec3& n) {
    const float d = dot(s - q, n);
    return s - n * (2.0f * d);
}
// Tap off a circular vertical reflector of radius R, locally a plane tangent at the horizontal direction of the
// listener / source midpoint (both inside the circle).
bool circleTap(const Vec3& l, const Vec3& s, float radius, float reflectivity, float cutoff, EchoTap& t) {
    Vec3 mid = (l + s) * 0.5f;
    mid.y = 0.0f;
    float m = length(mid);
    Vec3 n = m > 0.5f ? mid / m : Vec3{0.0f, 0.0f, 1.0f};   // at the centre: the gate side (north)
    const float rl = std::hypot(l.x, l.z), rs = std::hypot(s.x, s.z);
    if (rl >= radius - 0.5f || rs >= radius - 0.5f) return false;   // outside / on the reflector: no simple image
    const Vec3 q = n * radius;
    const Vec3 img = mirror(s, q, n);
    const float direct = std::max(length(s - l), 1.0f);
    const float path = length(img - l);
    if (path <= direct + 1.0f) return false;
    t.delaySec = (path - direct) / kSpeedOfSound;
    // Spreading relative to the direct sound, softened from 1/r to 1/sqrt(r) so the slap stays legible next to a close
    // report (game mix; the treeline's cutoff and the air absorption keep it distant-sounding).
    t.gain = reflectivity * std::sqrt(direct / path);
    t.cutoffHz = cutoff;
    t.image = img;
    return true;
}
}  // namespace

int echoTaps(const Vec3& listener, const Vec3& source, EchoTap taps[2]) {
    if (bunkerAmount(listener) > 0.75f || bunkerAmount(source) > 0.75f) return 0;
    int n = 0;
    // HESCO bastions: earth-filled, fairly absorbent, 2.2 m high -> a short bright slap.
    if (circleTap(listener, source, kPerimeterWallRadius, 0.45f, 4500.0f, taps[n])) ++n;
    // Treeline / hills: a late, dark rolling echo.
    if (circleTap(listener, source, kTreelineRadius, 0.38f, 1400.0f, taps[n])) ++n;
    return n;
}

float propagationDelay(float distance) { return clampf((distance - 20.0f) / kSpeedOfSound, 0.0f, 1.0f); }

}  // namespace ts::audio
