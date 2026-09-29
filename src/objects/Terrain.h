// Terrain: a 20 m radius disc of ground around the command bunker.
// Geometrically it is the cylinder's cap (centre + rings of points at phi_j = 2*pi*j/slices)
// subdivided into concentric rings and displaced in y by a height function h(x, z).
#pragma once

#include <memory>

#include "primitives/MeshData.h"
#include "scene/SceneNode.h"

namespace ts {

constexpr float kTerrainRadius = 20.0f;
constexpr int kTerrainRings = 72;     // radial step 0.28 m
constexpr int kTerrainSlices = 256;   // angular step 1.41 degrees (0.49 m at the rim)

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

// Vertices = 1 + rings * slices, triangles = slices * (2 * rings - 1).
MeshData createTerrainDisc(int rings = kTerrainRings, int slices = kTerrainSlices, float radius = kTerrainRadius);

std::unique_ptr<SceneNode> buildTerrain();

}  // namespace ts
