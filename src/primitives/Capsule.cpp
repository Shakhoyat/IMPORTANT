#include "primitives/Capsule.h"

#include <algorithm>
#include <cmath>

#include "primitives/Revolve.h"

namespace ts {

MeshData createCapsule(int slices, int capStacks, float capHeight) {
    if (slices < 3) slices = 3;
    if (capStacks < 1) capStacks = 1;
    const float h = std::clamp(capHeight, 0.01f, 0.5f), a = 0.5f, zc = 0.5f - h;
    std::vector<RevolveRing> rings;
    for (int k = 1; k <= capStacks; ++k) {  // top cap, pole -> equator
        const float theta = 0.5f * PI * static_cast<float>(k) / static_cast<float>(capStacks);
        const float s = std::sin(theta), c = std::cos(theta);
        rings.push_back({a * s, zc + h * c, s / a, c / h});
    }
    for (int k = capStacks; k >= 1; --k) {  // bottom cap, equator -> pole
        const float theta = 0.5f * PI * static_cast<float>(k) / static_cast<float>(capStacks);
        const float s = std::sin(theta), c = std::cos(theta);
        rings.push_back({a * s, -zc - h * c, s / a, -c / h});
    }
    MeshData m = revolve(rings, slices, RevolveEnd::Pole, 0.5f, RevolveEnd::Pole, -0.5f);
    m.name = "Capsule";
    return m;
}

}  // namespace ts
