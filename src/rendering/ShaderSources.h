// GLSL sources, kept in C++ raw strings so the program runs from any working directory.
#pragma once

#include <string>

namespace ts::glsl {

extern const char* kFlatVert;     // Flat: geometry shader lights each triangle once at its centroid
extern const char* kFlatGeom;
extern const char* kFlatFrag;
extern const char* kGouraudVert;  // Gouraud: lighting per vertex, colour interpolated
extern const char* kGouraudFrag;
extern const char* kPhongVert;    // Phong: normal interpolated, lighting per fragment
extern const char* kPhongFrag;
extern const char* kBeamVert;     // additive light cone
extern const char* kBeamFrag;
extern const char* kLineVert;     // wireframe / axes / planner overlay
extern const char* kLineFrag;
extern const char* kDepthVert;    // shadow maps: depth only
extern const char* kDepthFrag;
extern const char* kPrepassVert;  // depth + view-space normal (SSAO, volumetric fog)
extern const char* kPrepassFrag;

// ShadersPost.cpp
extern const char* kSkyVert;      // night sky (full-screen triangle at the far plane)
extern const char* kSkyFrag;      // + palette
extern const char* kFullscreenVert;
extern const char* kSsaoFrag;     // + palette
extern const char* kBlurFrag;
extern const char* kFogFrag;      // + palette
extern const char* kBloomDownFrag;
extern const char* kBloomUpFrag;

// ShadersPostFx.cpp (v3 chain)
extern const char* kCombineFrag;     // fog + screen-space reflections      (+ palette)
extern const char* kTaaFrag;         // temporal anti-aliasing               (+ palette)
extern const char* kReduceFrag;      // luminance / heat statistics
extern const char* kAdaptFrag;       // temporal adaptation of the statistics
extern const char* kFlareFrag;       // lens ghosts + halo
extern const char* kStreakFrag;      // anamorphic streak
extern const char* kDofFrag;         // bokeh depth of field (half resolution) (+ palette)
extern const char* kDofCombineFrag;  //                                        (+ palette)
extern const char* kCompositeFrag;   // grading + filmic curve / thermal sensor (+ palette)
extern const char* kHudVert;      // 2D overlay (NDC), optional texture
extern const char* kHudFrag;

// Source assembly: palette only (sky, HUD); + lighting (vertex/geometry stages); + output (fragment stage).
std::string withPalette(const char* body);
std::string withLighting(const char* body);
std::string withLightingAndOutput(const char* body);

// v7 WP-Q (ShadersInstanced.cpp): instanced scene-part programs; per-instance data in a buffer texture.
enum class ShadingKind { Flat, Gouraud, Phong };
struct InstancedSources {
    std::string vert, geom, frag;   // geom empty except Flat
};
InstancedSources instancedSources(ShadingKind kind);
extern const char* kDepthVertI;     // depth only, 4 texels per instance (+ kDepthFrag)
extern const char* kPrepassVertI;   // pre-pass, 7 texels per instance (+ kPrepassFrag)

}  // namespace ts::glsl
