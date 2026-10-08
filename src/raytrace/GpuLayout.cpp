#include "raytrace/GpuLayout.h"

#include <algorithm>

namespace ts::rt {

GpuMaterial packMaterial(const RtMaterial& m) {
    GpuMaterial g{};
    g.albedo[0] = m.albedo.x;
    g.albedo[1] = m.albedo.y;
    g.albedo[2] = m.albedo.z;
    g.roughness = m.roughness;
    g.emission[0] = m.emission.x;
    g.emission[1] = m.emission.y;
    g.emission[2] = m.emission.z;
    g.metalness = m.metalness;
    g.transmit[0] = m.tint.x * m.transmission;
    g.transmit[1] = m.tint.y * m.transmission;
    g.transmit[2] = m.tint.z * m.transmission;
    g.ior = m.transmission > 0.0f ? std::max(m.ior, 1.0f) : std::min(m.specular, 0.99f);
    return g;
}

void packScene(const RtScene& s, GpuScene& out) {
    out.prims.resize(s.prims.size());
    out.materials.resize(s.prims.size());
    for (size_t i = 0; i < s.prims.size(); ++i) {
        const RtPrim& p = s.prims[i];
        GpuPrim& g = out.prims[i];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) g.inv[4 * r + c] = p.inv.at(r, c);
        g.shape = static_cast<int>(p.shape);
        g.flags = p.flags;
        g.node = p.node;
        out.materials[i] = packMaterial(s.materials[i]);
    }
    const std::vector<BvhNode>& nodes = s.bvh().nodes();
    out.nodes.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const BvhNode& n = nodes[i];
        GpuNode& g = out.nodes[i];
        g.lo[0] = n.box.lo.x;
        g.lo[1] = n.box.lo.y;
        g.lo[2] = n.box.lo.z;
        g.hi[0] = n.box.hi.x;
        g.hi[1] = n.box.hi.y;
        g.hi[2] = n.box.hi.z;
        g.leftFirst = n.leftFirst;
        g.count = n.count;
    }
    out.lights.resize(s.lights.size());
    for (size_t i = 0; i < s.lights.size(); ++i) {
        const RtLight& l = s.lights[i];
        GpuLight& g = out.lights[i];
        g.position[0] = l.position.x;
        g.position[1] = l.position.y;
        g.position[2] = l.position.z;
        g.type = static_cast<int>(l.type);
        g.direction[0] = l.direction.x;
        g.direction[1] = l.direction.y;
        g.direction[2] = l.direction.z;
        g.radius = l.radius;
        g.color[0] = l.color.x;
        g.color[1] = l.color.y;
        g.color[2] = l.color.z;
        g.range = l.range;
        g.linear = l.linear;
        g.quadratic = l.quadratic;
        g.cosInner = l.cosInner;
        g.cosOuter = l.cosOuter;
    }
    out.terrain = packMaterial(s.terrainMaterial());
}

}  // namespace ts::rt
