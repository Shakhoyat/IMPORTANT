#include "objects/VegetationLayout.h"

#include <algorithm>
#include <map>

namespace ts {

uint32_t vegHash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

namespace {

struct Group {
    const SceneNode* node = nullptr;
    std::vector<const SceneNode*> leaves;
};

uint32_t seedOf(const Vec3& p) {
    return vegHash(static_cast<uint32_t>(static_cast<int>(p.x * 97.0f)) * 73856093u ^ static_cast<uint32_t>(static_cast<int>(p.z * 89.0f)) * 19349663u);
}

// Yaw (degrees) of a group's local +X axis about the world Y axis.
float yawOf(const Mat4& m) { return degrees(std::atan2(-m.at(2, 0), m.at(0, 0))); }

// Axis-aligned bounds of the unit primitives (cube / sphere / cylinder / cone, all 1 m across) of a leaf set.
void leafBounds(const std::vector<const SceneNode*>& leaves, Vec3& lo, Vec3& hi) {
    lo = {1e9f, 1e9f, 1e9f};
    hi = {-1e9f, -1e9f, -1e9f};
    for (const SceneNode* n : leaves) {
        const Vec3 c = transformPoint(n->world, {0, 0, 0});
        // half extent of the rotated unit box along each world axis
        const float rx = 0.5f * std::fabs(n->world.at(0, 0)) + 0.5f * std::fabs(n->world.at(0, 1)) + 0.5f * std::fabs(n->world.at(0, 2));
        const float ry = 0.5f * std::fabs(n->world.at(1, 0)) + 0.5f * std::fabs(n->world.at(1, 1)) + 0.5f * std::fabs(n->world.at(1, 2));
        const float rz = 0.5f * std::fabs(n->world.at(2, 0)) + 0.5f * std::fabs(n->world.at(2, 1)) + 0.5f * std::fabs(n->world.at(2, 2));
        lo = {std::min(lo.x, c.x - rx), std::min(lo.y, c.y - ry), std::min(lo.z, c.z - rz)};
        hi = {std::max(hi.x, c.x + rx), std::max(hi.y, c.y + ry), std::max(hi.z, c.z + rz)};
    }
}

}  // namespace

VegLayout extractVegetation(const std::vector<const SceneNode*>& leaves) {
    VegLayout out;
    // Group the leaves by their tree / bush / boulder group (the leaf's parent).
    std::map<const SceneNode*, Group> groups;
    std::vector<const SceneNode*> order;
    for (const SceneNode* n : leaves) {
        const SceneNode* p = n->parent;
        if (!p) continue;
        const std::string& g = p->name;
        if (g != "Pine" && g != "Tree" && g != "Bush" && g != "Boulder" && g != "Treeline pine") continue;
        Group& grp = groups[p];
        if (!grp.node) {
            grp.node = p;
            order.push_back(p);
        }
        grp.leaves.push_back(n);
    }
    for (const SceneNode* p : order) {
        const Group& g = groups[p];
        const Vec3 base = transformPoint(p->world, {0, 0, 0});
        VegInstance v;
        v.x = base.x;
        v.y = base.y;
        v.z = base.z;
        v.yawDeg = yawOf(p->world);
        v.seed = seedOf(base);
        Vec3 lo, hi;
        leafBounds(g.leaves, lo, hi);
        if (p->name == "Pine" || p->name == "Treeline pine") {
            // Tree height: the highest cone apex (local +z of the cone, the cone stands apex up after its -90 deg tilt).
            float top = base.y;
            for (const SceneNode* n : g.leaves)
                if (n->primitive == PrimitiveType::Cone) top = std::max(top, transformPoint(n->world, {0, 0, 0.5f}).y);
            v.kind = p->name == "Pine" ? VegKind::CampConifer : VegKind::TallConifer;
            v.height = std::max(top - base.y, 1.0f);
            // The treeline pines are two stacked cones (6-10 m); real firs of that height are slimmer and taller:
            // the asset trees stand 1.6x higher (the walker collides with the crown base radius, unchanged).
            if (v.kind == VegKind::TallConifer) v.height *= 1.6f;
        } else if (p->name == "Tree") {
            v.kind = VegKind::Broadleaf;
            v.height = std::max(hi.y - base.y, 1.0f);
            v.width = std::max(hi.x - lo.x, hi.z - lo.z);
        } else if (p->name == "Bush") {
            v.kind = VegKind::Bush;
            v.height = std::max(hi.y - base.y, 0.3f);
            v.width = std::max(hi.x - lo.x, hi.z - lo.z);
        } else {
            v.kind = VegKind::Rock;
            v.height = std::max(hi.y - base.y, 0.2f);
            v.width = std::max(hi.x - lo.x, hi.z - lo.z);
        }
        out.instances.push_back(v);
        out.replaced.insert(out.replaced.end(), g.leaves.begin(), g.leaves.end());
    }
    return out;
}

}  // namespace ts
