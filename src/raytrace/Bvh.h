// Bounding volume hierarchy over primitive bounds, built with the surface area heuristic (binned SAH, 16 bins per
// axis; Wald 2007): a split of node N into L and R costs  C = C_trav + (A(L)/A(N)) n_L C_isect + (A(R)/A(N)) n_R C_isect,
// the cheapest of all bin boundaries on all three axes wins, and a node becomes a leaf when no split beats
// n C_isect. Flattened depth-first with the two children of a node stored next to each other, so a refit (moving
// objects) is one reverse sweep over the node array: children always come after their parent.
#pragma once

#include <vector>

#include "raytrace/Intersect.h"

namespace ts::rt {

struct BvhNode {
    Aabb box;
    int leftFirst = 0;   // interior: index of the left child (right = left + 1); leaf: first primitive
    int count = 0;       // > 0: leaf with 'count' primitives
    bool leaf() const { return count > 0; }
};

class Bvh {
public:
    static constexpr float kTraversalCost = 1.0f, kIntersectCost = 1.5f;
    static constexpr int kBins = 16, kMaxLeaf = 4;

    // Builds over 'bounds'. order() returns the primitive permutation: leaf ranges index into the REORDERED list,
    // i.e. reordered[k] = original[order()[k]].
    void build(const std::vector<Aabb>& bounds);
    // Median-split build (object median on the longest axis): only for the SAH comparison in the tests / report.
    void buildMedian(const std::vector<Aabb>& bounds);
    // New bounds for the reordered primitives (same order as after build): leaf boxes, then every interior box.
    void refit(const std::vector<Aabb>& reorderedBounds);
    // SAH cost of the current tree (relative to the root area); grows when a refit stretches boxes.
    float sahCost() const;

    const std::vector<BvhNode>& nodes() const { return nodes_; }
    const std::vector<int>& order() const { return order_; }
    bool empty() const { return nodes_.empty(); }
    int depth() const;
    int leafCount() const;

private:
    void split(int node, const std::vector<Aabb>& bounds, const std::vector<Vec3>& centres, bool median);
    std::vector<BvhNode> nodes_;
    std::vector<int> order_;
};

}  // namespace ts::rt
