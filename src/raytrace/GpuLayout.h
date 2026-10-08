// std430 records of the GPU ray tracer (rendering/RayTracerGL + ShadersRT.cpp declare the same structs in GLSL).
// Packing is GL-free so the tests can check the layout and the values.
#pragma once

#include <vector>

#include "raytrace/RtScene.h"

namespace ts::rt {

struct GpuPrim {        // 64 bytes: world -> local affine rows, shape, flags
    float inv[12];      // row r: inv[4r..4r+2] = linear part, inv[4r+3] = translation
    int shape = 0;
    unsigned flags = 0;
    int node = -1;
    int pad = 0;
};
struct GpuMaterial {    // 48 bytes
    float albedo[3];
    float roughness;
    float emission[3];
    float metalness;
    float transmit[3];  // tint * transmission
    float ior;          // >= 1: glass with this index; < 1: opaque, the value is the dielectric F0
};
struct GpuNode {        // 32 bytes
    float lo[3];
    int leftFirst;
    float hi[3];
    int count;
};
struct GpuLight {       // 64 bytes
    float position[3];
    int type;
    float direction[3];
    float radius;
    float color[3];
    float range;
    float linear, quadratic, cosInner, cosOuter;
};

struct GpuScene {
    std::vector<GpuPrim> prims;
    std::vector<GpuMaterial> materials;   // same index as prims
    std::vector<GpuNode> nodes;
    std::vector<GpuLight> lights;
    GpuMaterial terrain{};
};

GpuMaterial packMaterial(const RtMaterial& m);
// Full pack (after a build) or only the per-frame parts (prims, materials, node boxes, lights) after a refit.
void packScene(const RtScene& s, GpuScene& out);

}  // namespace ts::rt
