#include "raytrace/Heightfield.h"

#include <algorithm>
#include <cmath>

#include "objects/BaseLayout.h"
#include "objects/Terrain.h"

namespace ts::rt {

void Heightfield::buildTerrain(int n, float radius) {
    // Holes: outside the disc mesh, and the entrance trench footprint dropped by createTerrainDisc().
    build(n, radius, [](float x, float z) { return terrainHeight(x, z); },
          [radius](float x, float z) {
              if (x * x + z * z > radius * radius) return true;
              return std::fabs(x) < layout::kRampHalfWidth + 0.35f && z > layout::kBlastDoorZ - 0.3f && z < layout::kRampTopZ;
          });
}

void Heightfield::finish() {
    const int cells = n_ - 1;
    nb_ = (cells + kBlock - 1) / kBlock;
    blocks_.assign(static_cast<size_t>(nb_) * nb_ * 2, 0.0f);
    bounds_ = Aabb{};
    for (int bj = 0; bj < nb_; ++bj)
        for (int bi = 0; bi < nb_; ++bi) {
            float lo = 1e30f, hi = -1e30f;
            for (int j = bj * kBlock; j <= std::min((bj + 1) * kBlock, cells); ++j)
                for (int i = bi * kBlock; i <= std::min((bi + 1) * kBlock, cells); ++i) {
                    lo = std::min(lo, heightAt(i, j));
                    hi = std::max(hi, heightAt(i, j));
                }
            blocks_[2 * (static_cast<size_t>(bj) * nb_ + bi)] = lo;
            blocks_[2 * (static_cast<size_t>(bj) * nb_ + bi) + 1] = hi;
            bounds_.grow(Vec3{-radius_ + cell_ * bi * kBlock, lo, -radius_ + cell_ * bj * kBlock});
            bounds_.grow(Vec3{-radius_ + cell_ * std::min((bi + 1) * kBlock, cells), hi,
                              -radius_ + cell_ * std::min((bj + 1) * kBlock, cells)});
        }
    bounds_.lo.y -= 0.01f;
    bounds_.hi.y += 0.01f;
}

namespace {

// Moller-Trumbore; returns t and barycentrics (u, v) of b and c.
bool triangle(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c, float& t, float& u, float& v) {
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = cross(d, e2);
    const float det = dot(e1, p);
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    const Vec3 s = o - a;
    u = dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const Vec3 q = cross(s, e1);
    v = dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = dot(e2, q) * inv;
    return true;
}

}  // namespace

bool Heightfield::cell(int ci, int cj, const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) const {
    ++lastCells;
    if (hole(ci, cj)) return false;
    auto vtx = [&](int i, int j) {
        return Vec3{-radius_ + cell_ * i, heightAt(i, j), -radius_ + cell_ * j};
    };
    auto nrm = [&](int i, int j) {
        const float* tx = &texels_[4 * (static_cast<size_t>(j) * n_ + i)];
        return Vec3{tx[1], std::sqrt(std::max(0.0f, 1.0f - tx[1] * tx[1] - tx[2] * tx[2])), tx[2]};
    };
    const Vec3 v00 = vtx(ci, cj), v10 = vtx(ci + 1, cj), v01 = vtx(ci, cj + 1), v11 = vtx(ci + 1, cj + 1);
    float best = tmax;
    bool found = false;
    float tt, u, v;
    if (triangle(o, d, v00, v10, v11, tt, u, v) && tt > tmin && tt < best) {
        best = tt;
        n = normalize(nrm(ci, cj) * (1.0f - u - v) + nrm(ci + 1, cj) * u + nrm(ci + 1, cj + 1) * v);
        found = true;
    }
    if (triangle(o, d, v00, v11, v01, tt, u, v) && tt > tmin && tt < best) {
        best = tt;
        n = normalize(nrm(ci, cj) * (1.0f - u - v) + nrm(ci + 1, cj + 1) * u + nrm(ci, cj + 1) * v);
        found = true;
    }
    if (found) t = best;
    return found;
}

bool Heightfield::intersect(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) const {
    lastCells = 0;
    if (empty()) return false;
    const Vec3 invD{1.0f / (std::fabs(d.x) > 1e-12f ? d.x : 1e-12f), 1.0f / (std::fabs(d.y) > 1e-12f ? d.y : 1e-12f),
                    1.0f / (std::fabs(d.z) > 1e-12f ? d.z : 1e-12f)};
    float ta;
    if (!bounds_.hit(o, invD, tmin, tmax, ta)) return false;
    // Exit distance of the bounds
    const float tx = std::max((bounds_.lo.x - o.x) * invD.x, (bounds_.hi.x - o.x) * invD.x);
    const float ty = std::max((bounds_.lo.y - o.y) * invD.y, (bounds_.hi.y - o.y) * invD.y);
    const float tz = std::max((bounds_.lo.z - o.z) * invD.z, (bounds_.hi.z - o.z) * invD.z);
    const float tb = std::min({tx, ty, tz, tmax});
    const int cells = n_ - 1;
    const float bsize = cell_ * kBlock;

    // Coarse DDA over blocks
    const Vec3 p0 = o + d * ta;
    int bi = std::clamp(static_cast<int>(std::floor((p0.x + radius_) / bsize)), 0, nb_ - 1);
    int bj = std::clamp(static_cast<int>(std::floor((p0.z + radius_) / bsize)), 0, nb_ - 1);
    const int sx = d.x > 0.0f ? 1 : -1, sz = d.z > 0.0f ? 1 : -1;
    const float dtx = std::fabs(bsize * invD.x), dtz = std::fabs(bsize * invD.z);
    float nextX = (-radius_ + bsize * (bi + (sx > 0 ? 1 : 0)) - o.x) * invD.x;
    float nextZ = (-radius_ + bsize * (bj + (sz > 0 ? 1 : 0)) - o.z) * invD.z;
    float tcur = ta;
    while (tcur <= tb && bi >= 0 && bj >= 0 && bi < nb_ && bj < nb_) {
        const float tnext = std::min({nextX, nextZ, tb});
        const float y0 = o.y + d.y * tcur, y1 = o.y + d.y * tnext;
        const float lo = blocks_[2 * (static_cast<size_t>(bj) * nb_ + bi)], hi = blocks_[2 * (static_cast<size_t>(bj) * nb_ + bi) + 1];
        if (std::min(y0, y1) <= hi + 1e-3f && std::max(y0, y1) >= lo - 1e-3f) {
            // Fine DDA over the cells of this block, inside [tcur, tnext]
            const int i0 = bi * kBlock, i1 = std::min(i0 + kBlock, cells) - 1;
            const int j0 = bj * kBlock, j1 = std::min(j0 + kBlock, cells) - 1;
            const Vec3 q = o + d * tcur;
            int ci = std::clamp(static_cast<int>(std::floor((q.x + radius_) / cell_)), i0, i1);
            int cj = std::clamp(static_cast<int>(std::floor((q.z + radius_) / cell_)), j0, j1);
            const float ctx = std::fabs(cell_ * invD.x), ctz = std::fabs(cell_ * invD.z);
            float cx = (-radius_ + cell_ * (ci + (sx > 0 ? 1 : 0)) - o.x) * invD.x;
            float cz = (-radius_ + cell_ * (cj + (sz > 0 ? 1 : 0)) - o.z) * invD.z;
            while (ci >= i0 && ci <= i1 && cj >= j0 && cj <= j1) {
                if (cell(ci, cj, o, d, tmin, tmax, t, n)) return true;
                if (cx < cz) {
                    if (cx > tnext + 1e-4f) break;
                    ci += sx;
                    cx += ctx;
                } else {
                    if (cz > tnext + 1e-4f) break;
                    cj += sz;
                    cz += ctz;
                }
            }
        }
        if (nextX < nextZ) {
            tcur = nextX;
            bi += sx;
            nextX += dtx;
        } else {
            tcur = nextZ;
            bj += sz;
            nextZ += dtz;
        }
    }
    return false;
}

}  // namespace ts::rt
