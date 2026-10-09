// v9 M6 / M7: GLSL of the asset vegetation (VegetationRenderer), the textured blade grass and the splatted terrain
// (TerrainRenderer). Assembled with glsl::withLighting / withLightingAndOutput so they share the scene's lights,
// shadows, fog and thermal output.
#pragma once

namespace ts::glsl::veg {

extern const char* kWindCommon;     // instance transform + hierarchical wind (vertex stage)
extern const char* kMeshVert;       // vegetation meshes (+ kWindCommon)
extern const char* kMeshFrag;       // + lighting + output
extern const char* kDepthVert;      // shadow maps (+ kWindCommon)
extern const char* kDepthFrag;
extern const char* kPrepassVert;    // depth + view normal (+ kWindCommon)
extern const char* kPrepassFrag;
extern const char* kImpostorVert;   // octahedral impostor billboards
extern const char* kImpostorFrag;   // + lighting + output
extern const char* kImpostorPrepassFrag;
extern const char* kBakeVert;       // impostor baking (object space, no wind)
extern const char* kBakeFrag;
extern const char* kFoliageLight;   // shared: two-sided foliage lighting with translucency (fragment stage)

}  // namespace ts::glsl::veg
