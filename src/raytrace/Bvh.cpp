#include "raytrace/Bvh.h"

#include <algorithm>
#include <functional>

namespace ts::rt {

namespace {

float axisOf(const Vec3& v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }

}  // namespace

void Bvh::build(const std::vector<Aabb>& bounds) {
    nodes_.clear();
    order_.resize(bounds.size());
    for (size_t i = 0; i < bounds.size(); ++i) order_[i] = static_cast<int>(i);
    if (bounds.empty()) return;
    std::vector<Vec3> centres(bounds.size());
    for (size_t i = 0; i < bounds.size(); ++i) centres[i] = bounds[i].empty() ? Vec3{0, -1e4f, 0} : bounds[i].centre();
    nodes_.reserve(bounds.size() * 2);
    BvhNode root;
    root.leftFirst = 0;
    root.count = static_cast<int>(bounds.size());
    nodes_.push_back(root);
    split(0, bounds, centres, false);
}

void Bvh::buildMedian(const std::vector<Aabb>& bounds) {
    nodes_.clear();
    order_.resize(bounds.size());
    for (size_t i = 0; i < bounds.size(); ++i) order_[i] = static_cast<int>(i);
    if (bounds.empty()) return;
    std::vector<Vec3> centres(bounds.size());
    for (size_t i = 0; i < bounds.size(); ++i) centres[i] = bounds[i].centre();
    BvhNode root;
    root.count = static_cast<int>(bounds.size());
    nodes_.push_back(root);
    split(0, bounds, centres, true);
}

void Bvh::split(int ni, const std::vector<Aabb>& bounds, const std::vector<Vec3>& centres, bool median) {
    const int first = nodes_[ni].leftFirst, count = nodes_[ni].count;
    Aabb box, cbox;
    for (int k = first; k < first + count; ++k) {
        box.grow(bounds[order_[k]]);
        cbox.grow(centres[order_[k]]);
    }
    nodes_[ni].box = box;
    if (count <= 1) return;

    int bestAxis = -1, bestSplit = 0;
    float bestCost = 1e30f;
    const float leafCost = kIntersectCost * static_cast<float>(count);
    if (median) {
        const Vec3 e = cbox.hi - cbox.lo;
        bestAxis = e.x > e.y ? (e.x > e.z ? 0 : 2) : (e.y > e.z ? 1 : 2);
        if (count <= kMaxLeaf) return;
    } else {
        // Binned SAH: kBins buckets of centroids per axis, sweep the kBins - 1 boundaries from both sides.
        for (int a = 0; a < 3; ++a) {
            const float lo = axisOf(cbox.lo, a), hi = axisOf(cbox.hi, a);
            if (hi - lo < 1e-6f) continue;
            Aabb binBox[kBins];
            int binCount[kBins] = {};
            const float scaleBins = kBins / (hi - lo);
            for (int k = first; k < first + count; ++k) {
                const int b = std::min(kBins - 1, static_cast<int>((axisOf(centres[order_[k]], a) - lo) * scaleBins));
                binBox[b].grow(bounds[order_[k]]);
                ++binCount[b];
            }
            float rightArea[kBins] = {};
            int rightCount[kBins] = {};
            Aabb acc;
            int n = 0;
            for (int b = kBins - 1; b > 0; --b) {
                acc.grow(binBox[b]);
                n += binCount[b];
                rightArea[b] = acc.area();
                rightCount[b] = n;
            }
            acc = Aabb{};
            n = 0;
            const float parentArea = std::max(box.area(), 1e-12f);
            for (int b = 0; b < kBins - 1; ++b) {
                acc.grow(binBox[b]);
                n += binCount[b];
                if (n == 0 || rightCount[b + 1] == 0) continue;
                const float cost = kTraversalCost + kIntersectCost * (acc.area() * n + rightArea[b + 1] * rightCount[b + 1]) / parentArea;
                if (cost < bestCost) {
                    bestCost = cost;
                    bestAxis = a;
                    bestSplit = b;
                }
            }
        }
        if (bestAxis < 0) return;                                 // all centroids coincide: leaf
        if (bestCost >= leafCost && count <= 16) return;          // splitting does not pay
    }

    int mid;
    if (median) {
        mid = first + count / 2;
        std::nth_element(order_.begin() + first, order_.begin() + mid, order_.begin() + first + count,
                         [&](int x, int y) { return axisOf(centres[x], bestAxis) < axisOf(centres[y], bestAxis); });
    } else {
        const float lo = axisOf(cbox.lo, bestAxis), hi = axisOf(cbox.hi, bestAxis);
        const float scaleBins = kBins / (hi - lo);
        auto it = std::partition(order_.begin() + first, order_.begin() + first + count, [&](int p) {
            return std::min(kBins - 1, static_cast<int>((axisOf(centres[p], bestAxis) - lo) * scaleBins)) <= bestSplit;
        });
        mid = static_cast<int>(it - order_.begin());
        if (mid == first || mid == first + count) mid = first + count / 2;
    }
    const int left = static_cast<int>(nodes_.size());
    BvhNode l, r;
    l.leftFirst = first;
    l.count = mid - first;
    r.leftFirst = mid;
    r.count = first + count - mid;
    nodes_.push_back(l);
    nodes_.push_back(r);
    nodes_[ni].leftFirst = left;
    nodes_[ni].count = 0;
    split(left, bounds, centres, median);
    split(left + 1, bounds, centres, median);
}

void Bvh::refit(const std::vector<Aabb>& reordered) {
    for (int i = static_cast<int>(nodes_.size()) - 1; i >= 0; --i) {
        BvhNode& n = nodes_[i];
        Aabb b;
        if (n.leaf()) {
            for (int k = n.leftFirst; k < n.leftFirst + n.count; ++k) b.grow(reordered[k]);
        } else {
            b.grow(nodes_[n.leftFirst].box);
            b.grow(nodes_[n.leftFirst + 1].box);
        }
        n.box = b;
    }
}

float Bvh::sahCost() const {
    if (nodes_.empty()) return 0.0f;
    const float root = std::max(nodes_[0].box.area(), 1e-12f);
    float c = 0.0f;
    for (const BvhNode& n : nodes_)
        c += (n.leaf() ? kIntersectCost * n.count : kTraversalCost) * n.box.area() / root;
    return c;
}

int Bvh::depth() const {
    std::function<int(int)> d = [&](int i) -> int {
        const BvhNode& n = nodes_[i];
        return n.leaf() ? 1 : 1 + std::max(d(n.leftFirst), d(n.leftFirst + 1));
    };
    return nodes_.empty() ? 0 : d(0);
}

int Bvh::leafCount() const {
    int c = 0;
    for (const BvhNode& n : nodes_) c += n.leaf();
    return c;
}

}  // namespace ts::rt
