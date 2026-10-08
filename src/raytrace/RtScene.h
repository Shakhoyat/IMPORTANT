// A ray-traceable scene: analytic primitives (BVH order), their materials, the terrain height field, the lights and
// the sky. Built by SceneBridge from the ThermalSense scene graph, or by hand (tests, figures).
#pragma once

#include <vector>

#include "raytrace/Bvh.h"
#include "raytrace/Heightfield.h"
#include "raytrace/RtTypes.h"

namespace ts::rt {

struct TraceStats {
    long long rays = 0, nodeVisits = 0, primTests = 0;
};

class RtScene {
public:
    // --- construction ---
    void clear();
    int addPrim(Shape s, const Mat4& world, const RtMaterial& m, uint32_t flags = 0, int node = -1, const char* name = "");
    // Builds the SAH BVH and reorders prims / materials into leaf order (indices returned by addPrim are invalidated;
    // RtPrim::node keeps the link to the source).
    void build();
    // After prims[i].world / flags changed: new inverses and bounds, BVH refit. Returns the SAH cost ratio
    // (current / at build): the bridge rebuilds when the refit has degraded the tree too much.
    float refit();
    void setTerrain(const Heightfield* hf, const RtMaterial& m) { terrain_ = hf; terrainMat_ = m; }

    std::vector<RtPrim> prims;
    std::vector<RtMaterial> materials;   // one per prim (same index)
    std::vector<RtLight> lights;
    RtEnv env;

    const Bvh& bvh() const { return bvh_; }
    const Heightfield* terrain() const { return terrain_; }
    const RtMaterial& terrainMaterial() const { return terrainMat_; }
    static constexpr int kTerrainPrim = 1 << 30;   // Hit::prim of the terrain

    // --- queries ---
    bool intersect(const Ray& r, Hit& h, TraceStats* st = nullptr) const;
    // Light transport between two points along r (up to r.tmax): 0 when an opaque surface blocks it, the product of
    // the glass tints when only glass is in the way (Whitted-style transparent shadows), 1 when free. Primitives with
    // any of the 'skip' flags do not occlude.
    Vec3 transmittance(const Ray& r, uint32_t skip = kNoShadow | kLightProxy, TraceStats* st = nullptr) const;
    const RtMaterial& material(const Hit& h) const { return h.prim == kTerrainPrim ? terrainMat_ : materials[h.prim]; }
    uint32_t flags(const Hit& h) const { return h.prim == kTerrainPrim ? 0u : prims[h.prim].flags; }
    const char* name(const Hit& h) const;
    // Brute force (no BVH): the tests compare it with intersect().
    bool intersectBrute(const Ray& r, Hit& h) const;

private:
    bool primHit(int i, const Ray& r, float tmax, float& t, Vec3& nLocal) const;
    void finishHit(const Ray& r, Hit& h, const Vec3& nWorld) const;
    Bvh bvh_;
    float buildCost_ = 1.0f;
    std::vector<Aabb> bounds_;
    const Heightfield* terrain_ = nullptr;
    RtMaterial terrainMat_;
};

}  // namespace ts::rt
