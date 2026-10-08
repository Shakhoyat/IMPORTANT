#include "rendering/GroundMap.h"

#include <algorithm>
#include <cstdint>

#include "objects/BaseLayout.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

float hash2(int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

float noise2(float x, float z) {
    const int ix = static_cast<int>(std::floor(x)), iz = static_cast<int>(std::floor(z));
    float fx = x - ix, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    return lerpf(lerpf(hash2(ix, iz), hash2(ix + 1, iz), fx), lerpf(hash2(ix, iz + 1), hash2(ix + 1, iz + 1), fx), fz);
}

float smooth01(float e0, float e1, float x) {
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Crown {
    float x, z, r;
};

// Tree crowns (pines and round trees) of the playable area, read from the scene graph.
std::vector<Crown> crowns(const Scene& scene) {
    std::vector<Crown> out;
    for (const SceneNode* n : scene.leaves()) {
        if (n->object == "Backdrop") continue;
        if (n->name != "Pine crown" && n->name != "Canopy low") continue;
        const Vec3 c = transformPoint(n->world, {0, 0, 0});
        const float r = 0.5f * length(Vec3{n->world.at(0, 0), n->world.at(1, 0), n->world.at(2, 0)});
        out.push_back({c.x, c.z, r});
    }
    return out;
}

// Packed gravel / dirt mask (0..1) of the apron: roads, pads, vehicle parks and the ground around buildings.
float apronGravel(float x, float z) {
    using namespace layout;
    auto disc = [](float x0, float z0, float rad, float x1, float z1, float soft) {
        const float d = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
        return 1.0f - smooth01(rad, rad + soft, d);
    };
    auto rect = [&](float cx, float cz, float hx, float hz, float yawDeg, float soft) {
        const float yw = radians(yawDeg), s = std::sin(yw), c = std::cos(yw);
        const float dx = x - cx, dz = z - cz;
        const float u = std::fabs(dx * c - dz * s) - hx, v = std::fabs(dx * s + dz * c) - hz;
        const float d = std::sqrt(std::max(u, 0.0f) * std::max(u, 0.0f) + std::max(v, 0.0f) * std::max(v, 0.0f));
        return 1.0f - smooth01(0.0f, soft, d);
    };
    float g = 0.0f;
    if (z > 17.0f) g = std::max(g, rect(0.0f, 27.0f, kRoadHalfWidth + 0.8f, 10.0f, 0.0f, 1.2f));  // road + shoulders
    g = std::max(g, rect(0.0f, 36.0f, 8.0f, 4.0f, 0.0f, 2.0f));                                    // checkpoint
    g = std::max(g, disc(kHelipad.x, kHelipad.z, kHelipadRadius + 2.0f, x, z, 3.0f));
    g = std::max(g, rect(kRadarSite.x, kRadarSite.z, 4.0f, 4.0f, 0.0f, 3.0f));
    g = std::max(g, disc(kAaGunSite.x, kAaGunSite.z, 5.0f, x, z, 2.5f));
    g = std::max(g, rect(kMotorPool.x + 1.0f, kMotorPool.z + 1.0f, 9.0f, 8.0f, kTankParkYawDeg, 3.0f));
    g = std::max(g, rect(kFuelDepot.x + 3.0f, kFuelDepot.z, 8.0f, 5.0f, 0.0f, 3.0f));
    g = std::max(g, rect(kBarracks.x, kBarracks.z, 6.5f, 11.5f, 0.0f, 3.0f));
    // v8 barracks hall: trodden gravel around it and on the PT area north of the door
    g = std::max(g, rect(0.5f * (hall::kX0 + hall::kX1), 0.5f * (hall::kZ0 + hall::kZ1) + 1.2f, 0.5f * (hall::kX1 - hall::kX0) + 1.0f,
                         0.5f * (hall::kZ1 - hall::kZ0) + 2.2f, 0.0f, 2.5f));
    // Service track along the wall's inside and the inner-gate yard.
    const float r = std::sqrt(x * x + z * z);
    g = std::max(g, 0.7f * (1.0f - smooth01(0.7f, 2.2f, std::fabs(r - 31.0f))));
    return clampf(g, 0.0f, 1.0f);
}

}  // namespace

float GroundMap::extent() { return layout::kBaseRadius; }

GroundMap::~GroundMap() {
    if (tex_) glDeleteTextures(1, &tex_);
}

void GroundMap::build(const Scene& scene) {
    const std::vector<Crown> trees = crowns(scene);
    const float E = extent();
    texels_.assign(static_cast<size_t>(kSize) * kSize * 4, 0);
    for (int j = 0; j < kSize; ++j) {
        for (int i = 0; i < kSize; ++i) {
            const float x = -E + (static_cast<float>(i) + 0.5f) / kSize * 2.0f * E;
            const float z = -E + (static_cast<float>(j) + 0.5f) / kSize * 2.0f * E;
            const float r = std::sqrt(x * x + z * z);
            const bool disc = r <= layout::kBaseRadius;

            // Creek bed: wet core, damp banks (only where the creek actually crosses the disc).
            const float dc = std::fabs(z - creekCentreZ(x));
            float wet = r <= kTerrainRadius ? 1.0f - smooth01(kCreekHalfWidth * 0.55f, kCreekHalfWidth * 1.6f, dc) : 0.0f;
            wet = std::max(wet, 0.35f * (1.0f - smooth01(-0.25f, -0.05f, r <= kTerrainRadius ? terrainHeight(x, z) : 0.0f)));

            // Vehicle track from the north gate to the bunker door: soft-edged mask (the shader computes the
            // lateral position of the tyre ruts analytically from the same centre line).
            float track = 0.0f;
            if (disc) {
                const float d = std::fabs(x - trackCentreX(z));
                track = (1.0f - smooth01(kTrackHalfWidth * 0.75f, kTrackHalfWidth * 1.15f, d)) * smooth01(1.8f, 3.2f, z);
            }

            // Forest floor under the crowns.
            const float apron = smooth01(18.5f, 21.0f, r);
            const float gravel = apronGravel(x, z) * apron;
            // Damp hollows / mud puddles scattered over the apron (the shader turns wet > 0.3 into mud and ponds);
            // never on the road itself.
            {
                const float nb = noise2(x * 0.24f + 3.0f, z * 0.24f - 7.0f) * 0.7f + noise2(x * 0.9f, z * 0.9f) * 0.3f;
                const float blob = smooth01(0.66f, 0.74f, nb) * apron * (1.0f - 0.8f * gravel);
                wet = std::max(wet, 0.75f * blob);
            }
            track = std::max(track, gravel * 0.85f);
            float forest = 0.0f;
            for (const Crown& c : trees) {
                const float d = std::sqrt((x - c.x) * (x - c.x) + (z - c.z) * (z - c.z));
                forest = std::max(forest, 1.0f - smooth01(c.r * 0.7f, c.r * 1.35f, d));
            }

            // Meadow: patchy, none on the bunker pad, the track or the wet bed; thinner under trees.
            float meadow = 0.3f + 0.9f * noise2(x * 0.35f + 11.0f, z * 0.35f - 4.0f) + 0.25f * noise2(x * 1.3f, z * 1.3f);
            meadow *= smooth01(4.2f, 5.6f, r);
            meadow *= 1.0f - apron * 0.45f;                       // apron: patches only
            if (std::fabs(x) < 3.2f && z > 3.0f && z < 14.5f) meadow = 0.0f;  // trench strip
            if (x > layout::hall::kX0 - 0.4f && x < layout::hall::kX1 + 0.4f && z > layout::hall::kZ0 - 0.4f &&
                z < layout::hall::kZ1 + 0.9f)
                meadow = 0.0f;   // v8: no grass through the barracks hall's floor (nor on its door step)
            meadow *= 1.0f - 0.92f * track;
            meadow *= 1.0f - 0.85f * wet;
            meadow *= 1.0f - 0.75f * forest;
            if (r < kTerrainRadius && terrainNormal(x, z).y < 0.93f) meadow *= 0.55f;
            meadow *= 1.0f - smooth01(layout::kBaseRadius - 3.0f, E, r) * 0.6f;
            meadow = clampf(meadow, 0.0f, 1.0f);

            unsigned char* t = &texels_[(static_cast<size_t>(j) * kSize + i) * 4];
            t[0] = static_cast<unsigned char>(wet * 255.0f + 0.5f);
            t[1] = static_cast<unsigned char>(track * 255.0f + 0.5f);
            t[2] = static_cast<unsigned char>(forest * 255.0f + 0.5f);
            t[3] = static_cast<unsigned char>(meadow * 255.0f + 0.5f);
        }
    }
    if (!tex_) glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels_.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

float GroundMap::sample(int channel, float x, float z) const {
    if (texels_.empty()) return 0.0f;
    const float E = extent();
    const float u = clampf((x + E) / (2.0f * E) * kSize - 0.5f, 0.0f, kSize - 1.001f);
    const float v = clampf((z + E) / (2.0f * E) * kSize - 0.5f, 0.0f, kSize - 1.001f);
    const int i = static_cast<int>(u), j = static_cast<int>(v);
    const float fu = u - i, fv = v - j;
    auto at = [&](int a, int b) { return texels_[(static_cast<size_t>(b) * kSize + a) * 4 + channel] / 255.0f; };
    return lerpf(lerpf(at(i, j), at(i + 1, j), fu), lerpf(at(i, j + 1), at(i + 1, j + 1), fu), fv);
}

float GroundMap::meadow(float x, float z) const { return sample(3, x, z); }
float GroundMap::forest(float x, float z) const { return sample(2, x, z); }

}  // namespace ts
