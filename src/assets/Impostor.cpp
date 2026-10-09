#include "assets/Impostor.h"

#include <algorithm>
#include <cmath>

namespace ts::asset {

void hemiOctEncode(float x, float y, float z, float& u, float& v) {
    y = std::max(y, 0.0f);
    const float s = std::fabs(x) + y + std::fabs(z);
    if (s <= 0.0f) {
        u = v = 0.5f;
        return;
    }
    x /= s;
    z /= s;
    u = 0.5f * (x + z) + 0.5f;
    v = 0.5f * (x - z) + 0.5f;
}

OctDir hemiOctDecode(float u, float v) {
    const float a = 2.0f * u - 1.0f, b = 2.0f * v - 1.0f;   // a = x + z, b = x - z
    const float x = 0.5f * (a + b), z = 0.5f * (a - b);
    const float y = std::max(1.0f - std::fabs(x) - std::fabs(z), 0.0f);
    const float l = std::sqrt(x * x + y * y + z * z);
    return {x / l, y / l, z / l};
}

OctDir impostorCellDirection(int i, int j, int n) {
    return hemiOctDecode((static_cast<float>(i) + 0.5f) / static_cast<float>(n), (static_cast<float>(j) + 0.5f) / static_cast<float>(n));
}

ImpostorTaps impostorTaps(float x, float y, float z, int n) {
    float u, v;
    hemiOctEncode(x, y, z, u, v);
    const float gx = std::clamp(u * n - 0.5f, 0.0f, static_cast<float>(n - 1));
    const float gy = std::clamp(v * n - 0.5f, 0.0f, static_cast<float>(n - 1));
    const int i0 = std::min(static_cast<int>(gx), n - 1), j0 = std::min(static_cast<int>(gy), n - 1);
    const int i1 = std::min(i0 + 1, n - 1), j1 = std::min(j0 + 1, n - 1);
    const float fx = gx - static_cast<float>(i0), fy = gy - static_cast<float>(j0);
    ImpostorTaps t;
    const int cells[4][2] = {{i0, j0}, {i1, j0}, {i0, j1}, {i1, j1}};
    const float w[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
    for (int k = 0; k < 4; ++k) {
        t.cell[k][0] = cells[k][0];
        t.cell[k][1] = cells[k][1];
        t.weight[k] = w[k];
    }
    return t;
}

}  // namespace ts::asset
