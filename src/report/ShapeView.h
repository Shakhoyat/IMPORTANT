// Report shape views, the CPU half (app/CaptureShapes.cpp renders the model panel on the GPU and writes the files):
// the request syntax, the parts of one design instance in the export pose, the pull-apart offsets, the camera fit and
// a small software rasteriser that draws every part Lambert-shaded in its primitive's report colour with its triangle
// edges and an outline per part. Pure CPU, so the tests check it.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "math/Math3D.h"
#include "primitives/MeshData.h"
#include "report/Designs.h"
#include "scene/SceneNode.h"

namespace ts::shape {

constexpr float kFov = 30.0f;     // shape-view lens (vertical field of view)
constexpr int kSS = 2;            // CPU raster supersampling per axis
constexpr float kPale = 0.55f;    // nested designs: key colour blended this far towards light grey

struct Spec {
    std::string design;
    float az = 35.0f, el = 25.0f;      // camera direction relative to the design's front (+Z of its root)
    float ksub = 0.5f, kpart = 0.3f;   // pull-apart factors: sub-assemblies, then parts inside them
    int inst = 1;                      // which instance (1 = first in scene order)
    std::vector<std::string> cut;      // cutaway: part / group names left out ("Roof*" = prefix)
    int w = 1800, h = 1350;
    float fill = 0.8f;                 // bounding sphere share of the frame height
    int stage = 0;                     // build stage: only the first ceil(stage N / 100) parts (hierarchy order), 0 = all
};
// "Name|az=..|el=..|ksub=..|kpart=..|inst=..|cut=a;b*|w=..|h=..|fill=..|stage=.."
Spec parseSpec(const std::string& text);
bool nameMatches(const std::string& name, const std::string& pattern);   // "Roof*" = prefix

struct DrawPart {
    const SceneNode* node = nullptr;
    Mat4 world;              // as drawn (hidden parts at their rest scale)
    PrimitiveType prim = PrimitiveType::None;
    bool cuboid = false;
    int nested = -1;         // owning instance when it is a nested design (drawn pale), else -1
    bool hidden = false;     // hidden in the t = 0 state (dagger)
    bool parked = false;     // hidden and stored away from the design (below ground): not drawn in this view
    bool cut = false;        // left out by the cutaway
    Vec3 offset{0, 0, 0};    // pull-apart offset (right panel)
};
inline bool drawn(const DrawPart& d) { return !d.parked && !d.cut; }

// Every leaf under the instance root (its own parts and those of nested designs), depth-first.
std::vector<DrawPart> gatherParts(const designs::Catalogue& cat, int inst, const std::vector<std::string>& cut, bool cutaway);

struct GroupInfo {
    const SceneNode* node = nullptr;
    Vec3 centre{0, 0, 0};
    Vec3 offset{0, 0, 0};
    int parts = 0;
    int level = 0;
};
// Pull-apart offsets (DrawPart::offset) and the groups of the instance with their centres and offsets.
std::map<const SceneNode*, GroupInfo> explode(std::vector<DrawPart>& parts, const SceneNode* root, float ksub, float kpart);

struct Cam {
    Mat4 view, proj;
    Vec3 eye, fwd, right, up;
    float nearZ = 0.01f, farZ = 1000.0f;   // the planes of proj (the renderer linearises depth with them)
    bool ortho = false;
    int w = 0, h = 0;
};
Cam perspectiveFit(const Vec3& centre, float radius, const Vec3& dir, int w, int h, float fill);
// Camera looking along -dir at the points: distance and aim chosen so their projection is centred and its larger
// extent (width or height) spans 'fill' of the frame.
Cam fitPoints(const std::vector<Vec3>& pts, const Vec3& dir, int w, int h, float fill);
bool project(const Cam& c, const Vec3& p, float& x, float& y);
Vec3 boxCentre(const std::vector<Vec3>& pts);
void cornersOf(const Mat4& w, std::vector<Vec3>& out);
// The 8 corners of a mesh's own bounding box under w (the terrain disc is in metres, not the unit cube).
void meshBoxCorners(const MeshData& m, const Mat4& w, std::vector<Vec3>& out);

struct Shaded {
    Mat4 world;
    MeshData const* mesh = nullptr;
    Vec3 color;        // sRGB 0..1
    int id = 0;        // part id for the outlines (1..)
    bool edges = true;
    bool fit = true;   // counts for the covered box (the crop); false for the light beam
};
struct Raster {
    int W = 0, H = 0;                 // supersampled size
    std::vector<float> depth;         // 1 / w (larger = nearer); 0 = empty
    std::vector<float> rgb;
    std::vector<int> id;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;   // covered box (supersampled pixels)
};
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGB, rows top to bottom
};
Raster makeRaster(int w, int h);
void rasterise(Raster& R, const Cam& cam, const std::vector<Shaded>& items, const Vec3& lightDir);
void drawEdges(Raster& R, const Cam& cam, const std::vector<Shaded>& items);
Image resolve(const Raster& R, const Vec3& bgTop, const Vec3& bgBottom);
Vec3 lightFor(const Cam& c);
Vec3 partColour(const DrawPart& d);       // key colour (sRGB), pale for nested designs
const MeshData& meshFor(PrimitiveType t);  // scene mesh, cached

}  // namespace ts::shape
