// Scene graph -> ray-traceable scene. Every visible leaf becomes one analytic primitive with its exact world matrix
// (the raster meshes are only tessellations of these shapes), the terrain disc becomes the height field, the light rig
// and the practical lamps become RtLights with the raster renderer's colours. The BVH is built once and refitted every
// frame (moving vehicles, UAVs, doors, debris); it is rebuilt when the leaf set changes or a refit has stretched the
// boxes to more than kRebuildRatio times the SAH cost of the fresh build.
#pragma once

#include <cstdint>
#include <vector>

#include "raytrace/RtScene.h"

namespace ts {
class Scene;
struct SceneNode;
struct Material;
}

namespace ts::rt {

// The few RenderSettings the light rig depends on (raytrace/ does not depend on the renderer).
struct LightOptions {
    bool moonOn = true, beaconOn = true, spotOn = true, practicalsOn = true;
    float rain = 0.0f;
    Vec3 camera{0, 2, 10};   // practical lamps are ranked by distance to it (16 kept, as in the raster)
};

// Material of a leaf as the tracers see it. emissiveFactor = the raster's uEmissive for this frame.
RtMaterial materialFromScene(const Material& m, float emissiveFactor);

class SceneBridge {
public:
    static constexpr float kRebuildRatio = 1.6f;
    static constexpr int kHeightfieldSamples = 289;   // 0.25 m cells over the 72 m terrain disc

    // Updates 'out' to the scene's current state. Returns true when the BVH was (re)built this call.
    bool sync(const Scene& scene, const LightOptions& lo, RtScene& out);
    int rebuilds() const { return rebuilds_; }
    float lastRefitRatio() const { return ratio_; }
    const Heightfield& heightfield() const { return hf_; }
    // Leaf behind a primitive (ray debugger, picking); nullptr for the terrain or a synthetic primitive.
    const SceneNode* leafOf(const RtScene& rs, int prim) const;
    void invalidate() { leaves_.clear(); }
    // v10 M3: world matrix of the analytic shape that traces a leaf (the leaf's own world matrix, except the hemisphere:
    // its parent ellipsoid). Every prim's world equals tracedWorld(leaf) after a sync.
    static Mat4 tracedWorld(const SceneNode& n);

private:
    void rebuild(const Scene& scene, RtScene& out);
    void lightsFrom(const Scene& scene, const LightOptions& lo, RtScene& out) const;
    float emissiveFactor(const Scene& scene, const SceneNode& n, float rain) const;
    std::vector<const SceneNode*> leaves_;   // the scene's leaf list at the last build
    std::vector<const SceneNode*> nodeOf_;   // per prim index (build order) -> leaf
    Heightfield hf_;
    int rebuilds_ = 0;
    float ratio_ = 1.0f;
};

}  // namespace ts::rt
