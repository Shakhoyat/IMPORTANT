// v9 M6: level-of-detail chains for the CC0 vegetation (pure CPU; meshoptimizer, MIT, third_party/meshoptimizer).
//
// A tree is two kinds of geometry:
//   solid parts (trunk, branches, rock)  -> meshopt_simplify: edge collapses ordered by the quadric error metric,
//                                           stopped at a triangle target or an error bound (metres, reported back);
//   foliage cards (twig / leaf / blade   -> card thinning: every card is a connected component of the triangle list;
//   cards with an alpha-tested texture)     a level keeps a deterministic random fraction 'keep' of the cards and grows
//                                           each kept card about its anchor by keep^-e (e = 0.5 keeps the total card
//                                           area, so the crown keeps its density and silhouette with fewer triangles).
//                                           Edge collapse cannot do this: removing a card is not an edge collapse.
// The chain ends in an octahedral impostor (Impostor.h, baked on the GPU at start-up).
//
// Wind weights (computeWindColors) go into COLOR_0 so the vertex shader can run the hierarchical wind:
//   R  branch weight: distance from the trunk axis / crown radius (branches swing more towards the tips)
//   G  flutter weight: distance from the card's anchor / card size (leaf / needle flutter)
//   B  phase: random per card (neighbouring cards do not move in lock-step)
//   A  crown occlusion: 1 at the outer envelope, darker inside the crown and towards the bottom (baked ambient
//      occlusion, the crown shades its own interior)
#pragma once

#include <cstdint>
#include <vector>

#include "assets/Model.h"

namespace ts::asset {

// Connected components of a triangle list (union-find over shared vertex indices): one id per triangle, ids 0..count-1.
std::vector<uint32_t> triangleComponents(const std::vector<uint32_t>& idx, size_t vertexCount, uint32_t* count);

enum class Anchor { TrunkAxis, Lowest };   // card anchor: nearest the trunk axis (twigs) or lowest vertex (blades, leaves)

struct ThinStats {
    size_t cardsIn = 0, cardsKept = 0;
    float scale = 1.0f;
};
MeshPart thinCards(const MeshPart& part, float keep, float exponent, uint32_t seed, Anchor anchor, ThinStats* stats = nullptr);

// meshopt_simplify with the UVs in the error metric. 'ratio' = target triangles / input triangles, 'maxError' relative to
// the part's extent. *errorMetres (optional) = the achieved geometric error in metres.
// 'prune' (meshopt_SimplifyPrune) also removes disconnected pieces smaller than the error bound (rock crumbs).
MeshPart simplifyPart(const MeshPart& part, float ratio, float maxError, float* errorMetres = nullptr, bool prune = false);
// Drops unreferenced vertices and reorders the triangles for the post-transform vertex cache.
void compactPart(MeshPart& part);

void computeWindColors(Variant& v, const std::vector<bool>& foliageMaterial, Anchor anchor);

struct LodSettings {
    float keep[3] = {1.0f, 0.34f, 0.12f};       // foliage card fraction per level (relative to LOD 0)
    float thinExponent = 0.6f;                  // card growth keep^-e (> 0.5: the coarser levels fill out)
    float solidRatio[3] = {1.0f, 0.4f, 0.15f};  // solid part triangle ratio per level
    float solidError[3] = {0.0f, 0.01f, 0.03f}; // max relative error per level
    Anchor anchor = Anchor::TrunkAxis;
};
struct LodInfo {
    size_t triangles = 0;
    float solidErrorMetres = 0.0f;   // largest simplification error of the solid parts (0 at LOD 0)
    float cardScale = 1.0f;          // growth of the kept foliage cards
};
// Level 0 is 'lod0' itself (compacted); levels 1, 2 are derived from it.
std::vector<Variant> buildLodChain(const Variant& lod0, const std::vector<MaterialInfo>& materials, const LodSettings& s,
                                   std::vector<LodInfo>* info = nullptr);

bool isFoliageMaterial(const MaterialInfo& m);   // alpha-tested (MASK / BLEND) materials are cards

}  // namespace ts::asset
