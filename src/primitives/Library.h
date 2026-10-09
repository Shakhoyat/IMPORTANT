// v10 M3: the primitive library of the report, 16 entries in report order (1 cube ... 16 terrain disc), and the one
// mesh per PrimitiveType that the renderer uploads (sceneMesh: generated once, reused by every part). The generator
// parameters used by the scene live here, so the renderer, the Frame Explorer, the occlusion / ray-tracing
// approximations, the export and the tests all read the same numbers.
#pragma once

#include <string>
#include <vector>

#include "math/Math3D.h"
#include "primitives/MeshData.h"
#include "scene/SceneNode.h"

namespace ts {

namespace lib {
// Scene parameter set of each generator (see the generator headers for the formulas).
constexpr int kSphereSlices = 36, kSphereStacks = 18;
constexpr int kCylinderSlices = 36;
constexpr float kBeamTop = 0.03f, kBeamBottom = 0.5f;          // light-beam frustum: createCylinder(36, 0.03, 0.5, false)
constexpr int kHemiSlices = 36, kHemiStacks = 9;
constexpr float kTorusR = 0.35f, kTorusTube = 0.15f;           // R + r = 0.5: outer diameter 1, thickness 2 r = 0.3
constexpr int kTorusU = 36, kTorusV = 16;
constexpr float kTaperTopX = 0.8f, kTaperTopZ = 0.8f;          // top face 0.8 x 0.8 of the 1 x 1 base
constexpr int kCapsuleSlices = 16, kCapsuleCapStacks = 3;
constexpr float kCapsuleCapHeight = 0.25f;                     // hemispherical caps for S.z / S.x = 2 (sandbags)
constexpr float kAnnulusInner = 0.43f;                         // inner / outer radius 0.86 (painted pad ring)
constexpr int kAnnulusSegments = 48;
constexpr float kHelixR = 0.475f, kHelixTube = 0.025f, kHelixTurns = 5.0f;   // concertina coil
constexpr int kHelixSegments = 120, kHelixSides = 6;
// S of a torus part with outer diameter d and thickness h (the unit torus is 2 r thick).
inline Vec3 torusScale(float d, float h) { return {d, d, h / (2.0f * kTorusTube)}; }
}  // namespace lib

// Every type that has a mesh (all but None), in enum order: the renderer and the Frame Explorer upload one each.
constexpr PrimitiveType kMeshTypes[] = {
    PrimitiveType::Cube,   PrimitiveType::Sphere,     PrimitiveType::Cylinder, PrimitiveType::BeamCone,
    PrimitiveType::TerrainDisc, PrimitiveType::Cone,  PrimitiveType::Hemisphere, PrimitiveType::Torus,
    PrimitiveType::Wedge,  PrimitiveType::TaperedBox, PrimitiveType::Pyramid,  PrimitiveType::Capsule,
    PrimitiveType::Annulus, PrimitiveType::Helix,     PrimitiveType::GrassBlade};

// The mesh the renderer draws for a part of type t (unit-cube primitive with the scene's parameters). The terrain disc
// is the real terrain mesh (world units); None returns an empty mesh.
MeshData sceneMesh(PrimitiveType t);

struct LibraryEntry {
    int number = 0;               // 1..16, report order
    std::string name;             // report name ("cube", "cuboid", "frustum", ...)
    PrimitiveType type = PrimitiveType::None;
    std::string call;             // generator call with the scene's parameters
    std::string params;           // JSON object of the parameters
    std::string vFormula, tFormula;   // closed-form vertex / triangle counts of the generator
    std::string parametric;       // parametric surface / construction (round primitives); empty for the flat ones
    std::string axis;             // orientation in the unit cube
    bool flat = false;            // flat-shaded polyhedron (corner table + face list exported)
    bool cuboid = false;          // the cube mesh with a non-uniform S (no generator of its own)
    MeshData mesh;
};

std::vector<LibraryEntry> primitiveLibrary();

// Cube vs cuboid: a cube-mesh part is a "cuboid" when its S is not uniform (relative difference > 1e-3).
bool isCuboidScale(const Vec3& s);

}  // namespace ts
