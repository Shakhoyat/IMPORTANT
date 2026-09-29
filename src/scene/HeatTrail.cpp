#include "scene/HeatTrail.h"

#include <algorithm>

namespace ts {

namespace {

constexpr float kCell = 2.0f * HeatTrail::kHalfExtent / static_cast<float>(HeatTrail::kSize);

}  // namespace

void HeatTrail::deposit(const Vec3& p, float radius, float amount) {
    if (amount <= 0.0f) return;
    const float cx = (p.x + kHalfExtent) / kCell - 0.5f, cz = (p.z + kHalfExtent) / kCell - 0.5f;
    const float rc = std::max(radius / kCell, 0.5f);
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - rc))), x1 = std::min(kSize - 1, static_cast<int>(std::ceil(cx + rc)));
    const int z0 = std::max(0, static_cast<int>(std::floor(cz - rc))), z1 = std::min(kSize - 1, static_cast<int>(std::ceil(cz + rc)));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
            const float w = 1.0f - d / (rc + 0.5f);
            if (w <= 0.0f) continue;
            float& c = cells_[static_cast<size_t>(z) * kSize + x];
            c = std::min(1.0f, c + amount * w);
        }
    }
    ++version_;
}

void HeatTrail::decay(float dt, float tau) {
    const float k = std::exp(-dt / std::max(tau, 1e-3f));
    for (float& c : cells_) c = c < 1e-4f ? 0.0f : c * k;
    ++version_;
}

float HeatTrail::sample(float x, float z) const {
    const float u = (x + kHalfExtent) / kCell - 0.5f, v = (z + kHalfExtent) / kCell - 0.5f;
    if (u < 0.0f || v < 0.0f || u > kSize - 1.0f || v > kSize - 1.0f) return 0.0f;
    const int i = std::min(static_cast<int>(u), kSize - 2), j = std::min(static_cast<int>(v), kSize - 2);
    const float fu = u - i, fv = v - j;
    auto at = [this](int a, int b) { return cells_[static_cast<size_t>(b) * kSize + a]; };
    return lerpf(lerpf(at(i, j), at(i + 1, j), fu), lerpf(at(i, j + 1), at(i + 1, j + 1), fu), fv);
}

float HeatTrail::maxValue() const { return *std::max_element(cells_.begin(), cells_.end()); }

}  // namespace ts
