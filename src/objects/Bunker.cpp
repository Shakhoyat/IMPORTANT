#include "objects/Bunker.h"

#include "objects/Builder.h"
#include "objects/PropKit.h"

namespace ts {

BunkerRig buildBunker() {
    using P = PrimitiveType;
    Builder b("Command Bunker", "BNK", ShadingModel::Flat);
    BunkerRig rig;
    rig.root = b.root("Command Bunker", {0, 0, 0}, "Outpost at the centre of the patrol area");
    SceneNode* r = rig.root.get();

    //                       colour                  heat   emis  spec  shininess
    const Material concrete = withPbr({{0.46f, 0.45f, 0.42f}, 0.34f, 0.0f, 0.04f, 8.0f}, Surface::Concrete, 0.85f, 0.0f);
    const Material darkConcrete = withPbr({{0.33f, 0.33f, 0.31f}, 0.30f, 0.0f, 0.04f, 8.0f}, Surface::Concrete, 0.88f, 0.0f);
    const Material steel = withPbr({{0.36f, 0.38f, 0.37f}, 0.40f, 0.0f, 0.30f, 32.0f}, Surface::Metal, 0.45f, 0.75f);
    Material warmSlit{{1.00f, 0.38f, 0.10f}, 0.58f, 0.22f, 0.0f, 4.0f};  // lit interior seen through the slit
    warmSlit.surface = Surface::Glass;
    warmSlit.roughness = 0.3f;
    Material doorLamp = asLight(Material{{1.00f, 0.60f, 0.26f}, 0.55f, 1.0f, 0.3f, 32.0f}, 2.2f, 7.0f);
    doorLamp.surface = Surface::Glass;
    doorLamp.roughness = 0.1f;
    const Material door = withPbr({{0.25f, 0.27f, 0.22f}, 0.55f, 0.0f, 0.15f, 16.0f}, Surface::Paint, 0.45f, 0.6f);
    const Material vent = withPbr({{0.30f, 0.31f, 0.30f}, 0.66f, 0.0f, 0.20f, 16.0f}, Surface::Metal, 0.5f, 0.7f);
    const Material sand = withPbr({{0.52f, 0.47f, 0.36f}, 0.27f, 0.0f, 0.02f, 4.0f}, Surface::Sand, 0.95f, 0.0f);
    const Material beacon = withPbr({{1.00f, 0.18f, 0.08f}, 0.60f, 1.0f, 0.5f, 64.0f}, Surface::Glass, 0.1f, 0.0f);
    const Material generator = withPbr({{0.28f, 0.33f, 0.24f}, 0.72f, 0.0f, 0.25f, 24.0f}, Surface::Paint, 0.5f, 0.5f);

    // Main structure (cubes). Scale = size in metres, translation = centre.
    SceneNode* n = b.part(r, "Foundation slab", P::Cube, {3.8f, 0.2f, 3.0f}, {0, 0, 0}, {0, 0.1f, 0}, darkConcrete, "Base plate");
    n->occluder = true;
    n = b.part(r, "Main block", P::Cube, {3.2f, 1.6f, 2.4f}, {0, 0, 0}, {0, 1.0f, 0}, concrete, "Bunker body");
    n->occluder = true;
    b.part(r, "Roof slab", P::Cube, {3.5f, 0.18f, 2.7f}, {0, 0, 0}, {0, 1.89f, 0}, darkConcrete, "Overhanging roof");
    n = b.part(r, "Entrance vestibule", P::Cube, {1.0f, 1.2f, 0.7f}, {0, 0, 0}, {0, 0.8f, 1.55f}, concrete, "Entrance block");
    n->occluder = true;
    b.part(r, "Door", P::Cube, {0.6f, 0.95f, 0.04f}, {0, 0, 0}, {0, 0.68f, 1.91f}, door, "Steel door (warm interior)");
    b.part(r, "Door lamp housing", P::Cube, {0.16f, 0.08f, 0.14f}, {0, 0, 0}, {0, 1.46f, 1.95f}, steel, "Lamp shade");
    b.part(r, "Door lamp", P::Sphere, {0.09f, 0.09f, 0.09f}, {0, 0, 0}, {0, 1.39f, 1.97f}, doorLamp,
           "Sodium lamp over the door (practical light)");
    b.part(r, "Firing slit L", P::Cube, {0.6f, 0.08f, 0.04f}, {0, 0, 0}, {-0.95f, 1.45f, 1.2f}, warmSlit, "Observation slit");
    b.part(r, "Firing slit R", P::Cube, {0.6f, 0.08f, 0.04f}, {0, 0, 0}, {0.95f, 1.45f, 1.2f}, warmSlit, "Observation slit");
    b.part(r, "Side slit W", P::Cube, {0.04f, 0.08f, 0.6f}, {0, 0, 0}, {-1.6f, 1.45f, 0}, warmSlit, "Observation slit");
    b.part(r, "Side slit E", P::Cube, {0.04f, 0.08f, 0.6f}, {0, 0, 0}, {1.6f, 1.45f, 0}, warmSlit, "Observation slit");

    // Observation turret: vertical cylinder (axis Z rotated 90 deg about X) + flattened sphere dome.
    b.part(r, "Turret ring", P::Cylinder, {1.1f, 1.1f, 0.45f}, {90, 0, 0}, {-0.8f, 2.2f, -0.3f}, concrete, "Turret base");
    b.part(r, "Turret dome", P::Sphere, {1.0f, 0.5f, 1.0f}, {0, 0, 0}, {-0.8f, 2.43f, -0.3f}, steel, "Armoured dome");
    b.part(r, "Turret slit", P::Cube, {0.45f, 0.06f, 0.05f}, {0, 0, 0}, {-0.8f, 2.28f, 0.24f}, warmSlit, "Dome viewing slit");

    // Roof ventilation: cube housings with cylindrical fan caps (warm exhaust air).
    b.part(r, "Vent housing A", P::Cube, {0.4f, 0.25f, 0.4f}, {0, 0, 0}, {0.65f, 2.1f, -0.6f}, vent, "Ventilation unit");
    b.part(r, "Vent housing B", P::Cube, {0.4f, 0.25f, 0.4f}, {0, 0, 0}, {1.2f, 2.1f, -0.6f}, vent, "Ventilation unit");
    rig.fanCaps[0] = b.part(r, "Fan cap A", P::Cylinder, {0.3f, 0.3f, 0.04f}, {90, 0, 0}, {0.65f, 2.245f, -0.6f}, vent, "Fan outlet");
    rig.fanCaps[1] = b.part(r, "Fan cap B", P::Cylinder, {0.3f, 0.3f, 0.04f}, {90, 0, 0}, {1.2f, 2.245f, -0.6f}, vent, "Fan outlet");
    rig.fanCaps[0]->animation = "Fan spins about its axis";
    rig.fanCaps[1]->animation = "Fan spins about its axis";

    // Antenna mast with the beacon (point light) on top.
    b.part(r, "Antenna mast", P::Cylinder, {0.06f, 0.06f, 1.6f}, {90, 0, 0}, {1.3f, 2.78f, 0.8f}, steel, "Radio mast");
    b.part(r, "Beacon housing", P::Cylinder, {0.16f, 0.16f, 0.08f}, {90, 0, 0}, {1.3f, 3.6f, 0.8f}, steel, "Lamp socket");
    rig.beaconBulb = b.part(r, "Beacon bulb", P::Sphere, {0.14f, 0.14f, 0.14f}, {0, 0, 0}, {1.3f, 3.7f, 0.8f}, beacon,
                            "Point light source L1");
    rig.beaconBulb->animation = "Emission pulses at 1 Hz";

    // Sandbag walls either side of the entrance: cylinders lying along X, flattened in Y.
    const float bagX[3] = {0.95f, 1.45f, 1.2f};
    const float bagY[3] = {0.09f, 0.09f, 0.25f};
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < 3; ++i) {
            n = b.part(r, side < 0 ? "Sandbag W" : "Sandbag E", P::Cylinder, {0.22f, 0.18f, 0.5f}, {0, 90, 0},
                       {side * bagX[i], bagY[i], 2.05f}, sand, "Sandbag barrier");
            n->occluder = true;
        }
    }

    // Diesel generator next to the bunker (warm engine block + exhaust pipe).
    n = b.part(r, "Generator block", P::Cube, {0.6f, 0.45f, 0.4f}, {0, 0, 0}, {2.35f, 0.225f, -0.6f}, generator, "Power generator");
    n->occluder = true;
    b.part(r, "Generator exhaust", P::Cylinder, {0.06f, 0.06f, 0.35f}, {90, 0, 0}, {2.55f, 0.6f, -0.7f}, vent, "Exhaust pipe");
    return rig;
}

}  // namespace ts
