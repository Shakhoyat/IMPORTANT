#include "primitives/Helix.h"

#include <algorithm>
#include <cmath>

namespace ts {

MeshData createHelix(float R, float r, float turns, int segments, int sides) {
    segments = std::max(segments, 1);
    sides = std::max(sides, 3);
    r = std::clamp(r, 0.001f, 0.25f);
    R = std::clamp(R, 0.0f, 0.5f - r);
    const float L = 1.0f - 2.0f * r;
    MeshData m;
    m.name = "Helix";
    auto frame = [&](float t, Vec3& c, Vec3& T, Vec3& N, Vec3& B) {
        const float a = 2.0f * PI * turns * t;
        c = {R * std::cos(a), R * std::sin(a), L * (t - 0.5f)};
        T = normalize(Vec3{-2.0f * PI * turns * R * std::sin(a), 2.0f * PI * turns * R * std::cos(a), L});
        N = {std::cos(a), std::sin(a), 0.0f};
        B = cross(T, N);
    };
    for (int i = 0; i <= segments; ++i) {
        Vec3 c, T, N, B;
        frame(static_cast<float>(i) / static_cast<float>(segments), c, T, N, B);
        for (int j = 0; j < sides; ++j) {
            const float psi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(sides);
            const Vec3 d = N * std::cos(psi) + B * std::sin(psi);
            m.vertices.push_back({c + d * r, d});
        }
    }
    auto at = [sides](int i, int j) { return static_cast<uint32_t>(i * sides + (j % sides)); };
    // dp/dt x dp/dpsi = T x B = -N points inwards, so the quad (i, j), (i+1, j), (i+1, j+1), (i, j+1) is wound the other
    // way: (i, j) -> (i, j+1) -> (i+1, j+1) is counter-clockwise seen from outside the tube.
    for (int i = 0; i < segments; ++i)
        for (int j = 0; j < sides; ++j)
            m.indices.insert(m.indices.end(), {at(i, j), at(i, j + 1), at(i + 1, j + 1), at(i, j), at(i + 1, j + 1), at(i + 1, j)});
    // End discs: the ring copied with the tangent as the normal; N x B = T, so increasing psi is CCW seen along +T.
    for (int end = 0; end < 2; ++end) {
        Vec3 c, T, N, B;
        frame(end == 0 ? 0.0f : 1.0f, c, T, N, B);
        const Vec3 n = end == 0 ? T * -1.0f : T;
        const uint32_t centre = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({c, n});
        for (int j = 0; j < sides; ++j) {
            const float psi = 2.0f * PI * static_cast<float>(j) / static_cast<float>(sides);
            m.vertices.push_back({c + (N * std::cos(psi) + B * std::sin(psi)) * r, n});
        }
        for (int j = 0; j < sides; ++j) {
            const uint32_t a = centre + 1 + static_cast<uint32_t>(j), b = centre + 1 + static_cast<uint32_t>((j + 1) % sides);
            if (end == 1) m.indices.insert(m.indices.end(), {centre, a, b});
            else m.indices.insert(m.indices.end(), {centre, b, a});
        }
    }
    return m;
}

}  // namespace ts
