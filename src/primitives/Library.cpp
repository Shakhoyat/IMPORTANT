#include "primitives/Library.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "primitives/Annulus.h"
#include "primitives/Capsule.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/GrassBlade.h"
#include "primitives/Helix.h"
#include "primitives/Hemisphere.h"
#include "primitives/Pyramid.h"
#include "primitives/Sphere.h"
#include "primitives/TaperedBox.h"
#include "primitives/Torus.h"
#include "primitives/Wedge.h"

namespace ts {

namespace {

std::string fmtS(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmtS(const char* f, ...) {
    char buf[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof(buf), f, a);
    va_end(a);
    return buf;
}

}  // namespace

MeshData sceneMesh(PrimitiveType t) {
    using namespace lib;
    switch (t) {
        case PrimitiveType::Cube: return createCube();
        case PrimitiveType::Sphere: return createSphere(kSphereSlices, kSphereStacks);
        case PrimitiveType::Cylinder: return createCylinder(kCylinderSlices);
        case PrimitiveType::BeamCone: return createCylinder(kCylinderSlices, kBeamTop, kBeamBottom, false);
        case PrimitiveType::TerrainDisc: return createTerrainDisc();
        case PrimitiveType::Cone: return createCylinder(kCylinderSlices, 0.5f, 0.0f, true);
        case PrimitiveType::Hemisphere: return createHemisphere(kHemiSlices, kHemiStacks);
        case PrimitiveType::Torus: return createTorus(kTorusR, kTorusTube, kTorusU, kTorusV);
        case PrimitiveType::Wedge: return createWedge();
        case PrimitiveType::TaperedBox: return createTaperedBox(kTaperTopX, kTaperTopZ);
        case PrimitiveType::Pyramid: return createPyramid();
        case PrimitiveType::Capsule: return createCapsule(kCapsuleSlices, kCapsuleCapStacks, kCapsuleCapHeight);
        case PrimitiveType::Annulus: return createAnnulus(kAnnulusInner, kAnnulusSegments);
        case PrimitiveType::Helix: return createHelix(kHelixR, kHelixTube, kHelixTurns, kHelixSegments, kHelixSides);
        case PrimitiveType::GrassBlade: return createGrassBlade();
        case PrimitiveType::None: break;
    }
    return {};
}

bool isCuboidScale(const Vec3& s) {
    const float lo = std::fmin(s.x, std::fmin(s.y, s.z)), hi = std::fmax(s.x, std::fmax(s.y, s.z));
    return hi - lo > 1e-3f * std::fmax(std::fabs(hi), 1e-6f);
}

std::vector<LibraryEntry> primitiveLibrary() {
    using namespace lib;
    using P = PrimitiveType;
    std::vector<LibraryEntry> v;
    auto add = [&](const char* name, P type, std::string call, std::string params, const char* vf, const char* tf,
                   std::string parametric, const char* axis, bool flat) {
        LibraryEntry e;
        e.number = static_cast<int>(v.size()) + 1;
        e.name = name;
        e.type = type;
        e.call = std::move(call);
        e.params = std::move(params);
        e.vFormula = vf;
        e.tFormula = tf;
        e.parametric = std::move(parametric);
        e.axis = axis;
        e.flat = flat;
        e.mesh = sceneMesh(type);
        v.push_back(std::move(e));
        return &v.back();
    };
    add("cube", P::Cube, "createCube()", "{}", "V = 6 faces x 4 corners", "T = 6 faces x 2", "",
        "centred, x, y, z in [-0.5, 0.5]", true);
    add("cuboid", P::Cube, "createCube() scaled by S = (a, b, c), a, b, c not all equal", "{\"S\": \"non-uniform\"}",
        "V = 6 faces x 4 corners (the cube mesh)", "T = 6 faces x 2", "", "the cube mesh, S per part", true)
        ->cuboid = true;
    add("sphere", P::Sphere, fmtS("createSphere(%d, %d)", kSphereSlices, kSphereStacks),
        fmtS("{\"slices\": %d, \"stacks\": %d, \"r\": 0.5}", kSphereSlices, kSphereStacks), "V = (k - 1) s + 2", "T = 2 s (k - 1)",
        "p = 0.5 (sin(theta) cos(phi), cos(theta), sin(theta) sin(phi)), theta = pi i / k, phi = 2 pi j / s; north half "
        "generated, south half by reflection y -> -y",
        "poles on Y", false);
    add("hemisphere", P::Hemisphere, fmtS("createHemisphere(%d, %d)", kHemiSlices, kHemiStacks),
        fmtS("{\"slices\": %d, \"stacks\": %d, \"semi_axes\": [0.5, 1, 0.5]}", kHemiSlices, kHemiStacks), "V = s (k + 1) + 2",
        "T = 2 s k",
        "p = (0.5 sin(theta) cos(phi), -0.5 + cos(theta), -0.5 sin(theta) sin(phi)), theta = (pi / 2) i / k, "
        "phi = 2 pi j / s; base disc y = -0.5",
        "pole +Y, base disc at y = -0.5", false);
    add("cylinder", P::Cylinder, fmtS("createCylinder(%d)", kCylinderSlices),
        fmtS("{\"slices\": %d, \"r_bottom\": 0.5, \"r_top\": 0.5, \"capped\": true}", kCylinderSlices), "V = 4 s + 2", "T = 4 s",
        "p = (r cos(phi), r sin(phi), +-0.5), phi = 2 pi j / s; caps: centre + own ring (normal +-Z)", "axis Z",
        false);
    add("frustum", P::BeamCone, fmtS("createCylinder(%d, %g, %g, false)", kCylinderSlices, kBeamTop, kBeamBottom),
        fmtS("{\"slices\": %d, \"r_bottom\": %g, \"r_top\": %g, \"capped\": false}", kCylinderSlices, kBeamTop, kBeamBottom),
        "V = 2 s", "T = 2 s",
        "p = (r(z) cos(phi), r(z) sin(phi), z), r(-0.5) = r_bottom, r(+0.5) = r_top; side normal (cos, sin, r_bottom - r_top)",
        "axis Z, open ends", false);
    add("cone", P::Cone, fmtS("createCylinder(%d, 0.5, 0, true)", kCylinderSlices),
        fmtS("{\"slices\": %d, \"r_bottom\": 0.5, \"r_top\": 0, \"capped\": true}", kCylinderSlices), "V = 3 s + 1", "T = 2 s",
        "cylinder generator with r_top = 0: apex (0, 0, 0.5), base disc z = -0.5", "axis Z, apex +Z", false);
    add("torus", P::Torus, fmtS("createTorus(%g, %g, %d, %d)", kTorusR, kTorusTube, kTorusU, kTorusV),
        fmtS("{\"R\": %g, \"r\": %g, \"u\": %d, \"v\": %d}", kTorusR, kTorusTube, kTorusU, kTorusV), "V = u v", "T = 2 u v",
        "p = ((R + r cos v) cos u, (R + r cos v) sin u, r sin v), n = (cos v cos u, cos v sin u, sin v)",
        "axis Z, R + r = 0.5, z in [-r, r]", false);
    add("wedge", P::Wedge, "createWedge()", "{}", "V = 3 quads x 4 + 2 triangles x 3 = 18", "T = 3 x 2 + 2 x 1 = 8", "",
        "triangle (y, z) = (-,-) (-,+) (+,-) extruded along X; slope faces +Y +Z", true);
    add("tapered box", P::TaperedBox, fmtS("createTaperedBox(%g, %g)", kTaperTopX, kTaperTopZ),
        fmtS("{\"bottom\": [1, 1], \"top\": [%g, %g]}", kTaperTopX, kTaperTopZ), "V = 6 faces x 4 = 24", "T = 6 x 2 = 12", "",
        "bottom 1 x 1 at y = -0.5, top at y = +0.5", true);
    add("pyramid", P::Pyramid, "createPyramid()", "{\"base\": [1, 1], \"apex\": [0, 0.5, 0]}", "V = 4 + 4 sides x 3 = 16",
        "T = 2 + 4 = 6", "", "base 1 x 1 at y = -0.5, apex +Y", true);
    add("capsule", P::Capsule, fmtS("createCapsule(%d, %d, %g)", kCapsuleSlices, kCapsuleCapStacks, kCapsuleCapHeight),
        fmtS("{\"slices\": %d, \"cap_stacks\": %d, \"cap_height\": %g}", kCapsuleSlices, kCapsuleCapStacks, kCapsuleCapHeight),
        "V = 2 c s + 2", "T = 4 c s",
        "cylinder r = 0.5, |z| <= 0.5 - h, + half-ellipsoid caps p = (0.5 sin(theta) cos(phi), 0.5 sin(theta) sin(phi), "
        "+-(0.5 - h + h cos(theta))), theta = (pi / 2) i / c",
        "axis Z", false);
    add("annulus", P::Annulus, fmtS("createAnnulus(%g, %d)", kAnnulusInner, kAnnulusSegments),
        fmtS("{\"r_inner\": %g, \"r_outer\": 0.5, \"segments\": %d, \"two_sided\": true}", kAnnulusInner, kAnnulusSegments),
        "V = 4 n", "T = 4 n", "outer (0.5 cos(phi), 0.5 sin(phi), 0), inner (r_i cos(phi), r_i sin(phi), 0), phi = 2 pi j / n; "
        "front face +Z and back face -Z",
        "XY plane (z = 0), normal +-Z", false);
    add("helix", P::Helix,
        fmtS("createHelix(%g, %g, %g, %d, %d)", kHelixR, kHelixTube, kHelixTurns, kHelixSegments, kHelixSides),
        fmtS("{\"R\": %g, \"r\": %g, \"turns\": %g, \"segments\": %d, \"sides\": %d, \"L\": %g}", kHelixR, kHelixTube, kHelixTurns,
             kHelixSegments, kHelixSides, 1.0f - 2.0f * kHelixTube),
        "V = (n + 1) m + 2 (m + 1)", "T = 2 n m + 2 m",
        "c(t) = (R cos(2 pi k t), R sin(2 pi k t), L (t - 0.5)), L = 1 - 2 r; tube c(t) + r (cos(psi) N + sin(psi) B), "
        "N = (cos, sin, 0), B = T x N",
        "axis Z", false);
    add("grass blade", P::GrassBlade, "createGrassBlade()", "{\"rows\": [0, 0.38, 0.72], \"width\": 1.25, \"fold\": 0.55}",
        "V = 2 x 3 rows + tip = 7", "T = 2 x 2 segments + tip = 5",
        "strip x = +-0.625 at y = t - 0.5 (t = 0, 0.38, 0.72), tip (0, 0.5, 0); bent and placed per instance in the grass "
        "vertex shader",
        "Y up, plane z = 0 (1.25 wide)", false);
    {
        LibraryEntry* e = add("terrain disc", P::TerrainDisc,
                              fmtS("createTerrainDisc(%d, %d, %g)", kTerrainRings, kTerrainSlices, layout::kBaseRadius), "",
                              "V = 1 + k s", "T = s (2 k - 1) - D (D = triangles dropped inside the entrance trench)",
                              "polar grid r = R i / k, phi = 2 pi j / s, y = terrainHeight(x, z), n = terrain normal",
                              "world units (radius R), Y up", false);
        const long full = static_cast<long>(kTerrainSlices) * (2L * kTerrainRings - 1);
        e->params = fmtS("{\"rings\": %d, \"slices\": %d, \"radius\": %g, \"trench_dropped\": %ld}", kTerrainRings, kTerrainSlices,
                         layout::kBaseRadius, full - static_cast<long>(e->mesh.triangleCount()));
    }
    return v;
}

}  // namespace ts
