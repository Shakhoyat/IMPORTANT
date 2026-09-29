// Line-of-sight tests against the scene's occluding parts.
// Each occluder keeps the inverse of its model matrix, so a segment can be taken back into the primitive's
// local space and tested against the exact unit shape (cube, sphere r = 0.5, cylinder r = 0.5 along Z,
// cone with base r = 0.5 at z = -0.5 and apex at z = +0.5).
#pragma once

#include <vector>

#include "scene/SceneNode.h"

namespace ts {

struct Box {
    Vec3 min, max;
};

struct Occluder {
    PrimitiveType type = PrimitiveType::Cube;
    Mat4 inverseWorld;
    Box bounds;  // world AABB: quick rejection and ground footprint
};

// World AABB of a leaf: the 8 corners of the unit cube (every primitive fits inside it) transformed by 'world'.
Box worldBounds(const Mat4& world);

// Collects all occluder leaves below 'node' (world matrices must be up to date).
void collectOccluders(const SceneNode& node, std::vector<Occluder>& out);

bool segmentHitsBox(const Vec3& a, const Vec3& b, const Box& box);   // slab test
bool segmentHitsPrimitive(const Vec3& a, const Vec3& b, const Occluder& o);
bool lineOfSightBlocked(const Vec3& a, const Vec3& b, const std::vector<Occluder>& occluders);

// Uniform 2D bucket grid over the ground plane (x, z) that makes line-of-sight queries cheap on the 20 m
// terrain: a segment only tests the occluders registered in the buckets its ground footprint crosses
// (2D DDA walk). The answer is identical to lineOfSightBlocked() over the same occluder list.
class OccluderGrid {
public:
    void build(const std::vector<Occluder>& occluders, float halfExtent, int cellsPerSide);
    bool blocked(const Vec3& a, const Vec3& b) const;
    const std::vector<Occluder>& occluders() const { return occluders_; }

private:
    bool testBucket(const std::vector<int>& bucket, const Vec3& a, const Vec3& b) const;

    std::vector<Occluder> occluders_;
    std::vector<std::vector<int>> buckets_;
    std::vector<int> outside_;  // occluders that reach outside the grid square: always tested
    mutable std::vector<unsigned> stamp_;
    mutable unsigned query_ = 0;
    float half_ = 1.0f, cell_ = 1.0f;
    int n_ = 0;
};

}  // namespace ts
