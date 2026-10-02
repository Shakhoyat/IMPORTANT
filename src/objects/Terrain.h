// Terrain: a 36 m radius disc of ground (layout::kBaseRadius) around the underground command bunker. Inside
// r = 20 m (kTerrainRadius, the compound) it has hills, creek, track and berm; outside it is the flat rim level.
// Geometrically it is the cylinder's cap (centre + rings of points at phi_j = 2*pi*j/slices)
// subdivided into concentric rings and displaced in y by a height function h(x, z).
#pragma once

#include <memory>

#include "objects/BaseLayout.h"
#include "primitives/MeshData.h"
#include "scene/SceneNode.h"

namespace ts {

constexpr float kTerrainRadius = 20.0f;
constexpr int kTerrainRings = 130;    // radial step 0.277 m over the 36 m mesh radius
constexpr int kTerrainSlices = 384;   // angular step 0.94 degrees (0.59 m at r = 36 m, 0.33 m at r = 20 m)

// Landscape features (all smooth, continuous):
//   levelled pad (h = 0) under the bunker at the origin, rolling hills, a dry creek bed crossing the south
//   (-Z) half, a vehicle track depression from the north gate (+Z) to the bunker door, a raised berm inside
//   the perimeter fence, and a constant rim height at r = 20 m where the backdrop's far ground starts.
float terrainHeight(float x, float z);
Vec3 terrainNormal(float x, float z);

float trackCentreX(float z);     // x of the vehicle track centre line (the track runs along +Z)
float creekCentreZ(float x);     // z of the creek bed centre line (the creek runs along X)
float terrainRimHeight();        // height of the terrain at r = kTerrainRadius (same all around)
constexpr float kTrackHalfWidth = 1.1f;
constexpr float kCreekHalfWidth = 1.2f;
constexpr float kBermRadius = 17.4f;
constexpr float kFenceRadius = 19.0f;

// Vertices = 1 + rings * slices; triangles = slices * (2 * rings - 1) minus the ones whose centroid lies in the
// entrance trench footprint (|x| < kRampHalfWidth + 0.35, kBlastDoorZ - 0.3 < z < kRampTopZ): the trench hole.
// The terrain also stays >= kBunkerRoofTopY + 0.15 above every underground room (smooth mound).
MeshData createTerrainDisc(int rings = kTerrainRings, int slices = kTerrainSlices, float radius = layout::kBaseRadius);

std::unique_ptr<SceneNode> buildTerrain();

}  // namespace ts
