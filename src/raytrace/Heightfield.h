// The terrain as a ray-traceable height field: a regular grid of terrainHeight() samples over the terrain disc
// (each cell = two triangles, smooth normals interpolated from terrainNormal()), cells outside the disc or inside
// the bunker entrance trench are holes, exactly like the raster mesh. Traversal is a two-level 2D DDA
// (Amanatides & Woo): 8 x 8-cell blocks with their height range are skipped when the ray passes above them, then
// the cells of a block are walked in ray order and their two triangles tested (Moller-Trumbore).
// The same grid is uploaded to the GPU tracer, so the CPU and GPU see the identical surface.
#pragma once

#include <vector>

#include "raytrace/Intersect.h"

namespace ts::rt {

class Heightfield {
public:
    static constexpr int kBlock = 8;   // cells per block side

    // n x n samples over [-radius, radius]^2 (n - 1 cells per side) using the real terrain functions.
    void buildTerrain(int n, float radius);
    // Generic build (tests): samples from a callback and an optional hole predicate on cell centres.
    template <typename HeightFn, typename HoleFn>
    void build(int n, float radius, HeightFn height, HoleFn hole);

    bool empty() const { return n_ < 2; }
    int samples() const { return n_; }
    float radius() const { return radius_; }
    float cellSize() const { return cell_; }
    const Aabb& bounds() const { return bounds_; }
    // Per-sample (height, normal.x, normal.z, hole flag of the cell whose lower corner this is): the GPU texture.
    const std::vector<float>& texels() const { return texels_; }
    // Per-block (min height, max height): the GPU block texture ((n - 1) / kBlock rounded up per side).
    const std::vector<float>& blockTexels() const { return blocks_; }
    int blocksPerSide() const { return nb_; }

    float heightAt(int i, int j) const { return texels_[4 * (static_cast<size_t>(j) * n_ + i)]; }
    bool hole(int ci, int cj) const { return texels_[4 * (static_cast<size_t>(cj) * n_ + ci) + 3] > 0.5f; }

    // Closest hit in (tmin, tmax): t and the smooth normal (always the upper side, flipped later by the caller).
    bool intersect(const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) const;
    // Cells visited by the last intersect() call (statistics for the report: DDA efficiency).
    mutable int lastCells = 0;

private:
    bool cell(int ci, int cj, const Vec3& o, const Vec3& d, float tmin, float tmax, float& t, Vec3& n) const;
    void finish();
    int n_ = 0, nb_ = 0;
    float radius_ = 0.0f, cell_ = 1.0f;
    std::vector<float> texels_, blocks_;
    Aabb bounds_;
};

template <typename HeightFn, typename HoleFn>
void Heightfield::build(int n, float radius, HeightFn height, HoleFn hole) {
    n_ = n;
    radius_ = radius;
    cell_ = 2.0f * radius / static_cast<float>(n - 1);
    texels_.assign(static_cast<size_t>(n) * n * 4, 0.0f);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float x = -radius + cell_ * i, z = -radius + cell_ * j;
            const float e = 0.05f;
            const float h = height(x, z);
            const Vec3 nn = normalize(Vec3{-(height(x + e, z) - height(x - e, z)) / (2 * e), 1.0f,
                                           -(height(x, z + e) - height(x, z - e)) / (2 * e)});
            float* t = &texels_[4 * (static_cast<size_t>(j) * n + i)];
            t[0] = h;
            t[1] = nn.x;
            t[2] = nn.z;
            t[3] = (i < n - 1 && j < n - 1 && hole(x + 0.5f * cell_, z + 0.5f * cell_)) ? 1.0f : 0.0f;
            if (i == n - 1 || j == n - 1) t[3] = 1.0f;   // no cell beyond the last sample
        }
    finish();
}

}  // namespace ts::rt
