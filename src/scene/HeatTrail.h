// Thermal footprints: heat that the rover leaves on the ground (friction under the tyres while driving,
// radiated engine heat under the hull while parked). A 256 x 256 grid over the 20 m disc (15.6 cm cells)
// that cools exponentially, T(t) = T0 exp(-t / tau). Pure CPU data, so it is unit-tested; the renderer
// uploads it as a texture and adds it to the ground's apparent temperature.
#pragma once

#include <cstdint>
#include <vector>

#include "math/Math3D.h"

namespace ts {

class HeatTrail {
public:
    static constexpr int kSize = 256;
    static constexpr float kHalfExtent = 20.0f;      // metres, covers the terrain disc
    static constexpr float kMaxExtraHeat = 0.28f;    // heat added at trail value 1 (ground 0.15 -> 0.43 < 0.80 blob threshold)

    HeatTrail() : cells_(static_cast<size_t>(kSize) * kSize, 0.0f) {}

    // Adds 'amount' (0..1 units) spread over a disc of 'radius' metres around p (linear falloff), clamped to 1.
    void deposit(const Vec3& p, float radius, float amount);
    // Exponential cooling with time constant tau (seconds).
    void decay(float dt, float tau);
    float sample(float x, float z) const;  // bilinear, 0 outside the grid
    float maxValue() const;
    const std::vector<float>& cells() const { return cells_; }
    uint32_t version() const { return version_; }  // changes whenever the data changes (texture upload)

private:
    std::vector<float> cells_;
    uint32_t version_ = 0;
};

}  // namespace ts
