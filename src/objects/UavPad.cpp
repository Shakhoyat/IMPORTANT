#include "objects/UavPad.h"

#include <cmath>
#include <string>

#include "objects/Builder.h"
#include "objects/PropKit.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

void flushPart(SceneNode* n) {
    n->obstacle = false;
    n->occluder = false;
    n->explodable = false;
}

}  // namespace

UavPadRig buildUavPads() {
    using P = PrimitiveType;
    Builder b("UAV pads", "UPAD", ShadingModel::Gouraud);
    UavPadRig rig;
    rig.root = b.root("UAV pads", {0, 0, 0}, "Landing pads of the two patrol UAVs (docked by day)");
    const Material slab = mats::concrete();
    const Material ring = makeMaterial({0.60f, 0.46f, 0.05f}, 0.27f, Surface::Paint, 0.55f, 0.0f);
    const Material inner = makeMaterial({0.30f, 0.30f, 0.29f}, 0.29f, Surface::Concrete, 0.80f, 0.0f);
    const Material rail = makeMaterial({0.20f, 0.21f, 0.20f}, 0.28f, Surface::Metal, 0.45f, 0.70f);
    const Material lamp = indicator({0.25f, 1.0f, 0.35f}, 1.2f);
    for (int i = 0; i < kUavPadCount; ++i) {
        const float x = kUavPadX[i], z = kUavPadZ[i], g = terrainHeight(x, z);
        SceneNode* site = b.group(rig.root.get(), "UAV pad " + std::to_string(i + 1), {x, g, z}, {0, kUavPadYawDeg, 0},
                                  "Pad of UAV-" + std::to_string(i + 1));
        flushPart(b.part(site, "Pad slab", P::Cylinder, {1.3f, 1.3f, kUavPadSlab}, {90, 0, 0}, {0, kUavPadSlab * 0.5f, 0}, slab,
                         "Flattened cylinder, 1.3 m diameter"));
        flushPart(b.part(site, "Pad ring", P::Annulus, {1.18f, 1.18f, 0.004f}, {90, 0, 0}, {0, kUavPadSlab + 0.002f, 0}, ring,
                         "Painted yellow touchdown ring"));
        flushPart(b.part(site, "Pad centre", P::Cylinder, {1.02f, 1.02f, 0.004f}, {90, 0, 0}, {0, kUavPadSlab + 0.004f, 0}, inner,
                         "Darker centre inside the ring"));
        for (int s = -1; s <= 1; s += 2)
            flushPart(b.part(site, "Landing rail", P::Cube, {0.035f, kUavPadRail, 0.32f}, {0, 0, 0},
                             {0.06f * static_cast<float>(s), kUavPadSlab + kUavPadRail * 0.5f, 0}, rail,
                             "Low rail under a skid (the gimbal hangs clear of the slab)"));
        for (int k = 0; k < 4; ++k) {
            const float a = 1.5707963f * static_cast<float>(k) + 0.7853982f;
            SceneNode* l = b.part(site, "Pad light", P::Sphere, {0.05f, 0.03f, 0.05f}, {0, 0, 0},
                                  {0.6f * std::cos(a), kUavPadSlab + 0.012f, 0.6f * std::sin(a)}, lamp, "Green pad edge light");
            flushPart(l);
            rig.lights[i][k] = l;
        }
        rig.touchdown[i] = {x, g + kUavPadSlab + kUavPadRail + kUavGearDrop, z};
    }
    return rig;
}

}  // namespace ts
