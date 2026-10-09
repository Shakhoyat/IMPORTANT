// v10 M3 capture modes for the report's construction figures (driven by App::captureMode):
//   --capture DIR --only parts:<Object>  every distinct part kind (part name without side / index suffix + primitive)
//        of one instance of the object, alone, in its own local rotation R (no parent transform), at its modelled
//        scale (S_rest), normalised so its bounding sphere fills ~70 % of a 512 x 512 tile; 10:30 daylight, neutral
//        light-grey background. DIR/parts_<slug>/NN_<part-slug>.png + index.json (number, part, primitive, count, S cm).
//   --only build:<Object>  the object with only the first 12 / 35 / 65 / 100 % of one rig's parts in hierarchy
//        (depth-first) order visible, the hero camera of the iso_* shots fitted to the whole object (same view for all
//        stages): DIR/build_<slug>_<pct>.png (1600 x 1200) + build_<slug>.json.
//   --only prims  each of the 16 library primitives alone (solid shaded + a copy with its triangle edges overlaid,
//        hidden-line removed against a CPU depth buffer of the same mesh), same camera for all:
//        DIR/prims/prim_<NN>_<name>.png, prim_<NN>_<name>_wire.png (800 x 800) + prims.json (V / T).
//   --only shapeview:<design>[|az=35|el=25|ksub=0.5|kpart=0.3|inst=1|cut=<name>;<name>*|w=1800|h=1350]
//        (app/CaptureShapes.cpp) one report design (src/report/Designs.h) alone, in the export's t = 0 pose, from a 3/4
//        view relative to its front: DIR/shapeview/<slug>_model.png (real renderer, materials), <slug>_prims.png (every
//        part Lambert-shaded in its primitive's report colour, triangle edges, pulled apart from the design centre),
//        <slug>.json (legend counts, drawn parts, crop boxes, projected sub-assembly centres). With --cutaway the parts
//        named by cut= are left out (enclosed designs).
//   --only lineup:<design>+<design>+...  the designs side by side at one scale (orthographic, metres per pixel in the
//        JSON): DIR/shapeview/lineup_<slug>_{model,prims}.png + .json.
//   --only tile:<object>|<part name>  one part tile as in parts:<object> (DIR/tiles/<slug>.png + .json).
// Several can be combined in one --only list (comma separated).
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "rendering/Renderer.h"
#include "scene/Scene.h"

namespace ts::partscap {

struct Ctx {
    Scene* scene = nullptr;
    Renderer* renderer = nullptr;
    std::string dir;
    Quality quality = Quality::Ultra;
    Vec3 sun{0.4f, 0.8f, 0.4f};       // direction towards the sun (picks the front-lit side, as the hero shots do)
    std::ostream* log = nullptr;
    bool cutaway = false;             // --cutaway: shapeview leaves out the parts its cut= list names
};

// True when every name of an --only list is a parts: / build: / prims request.
bool handles(const std::vector<std::string>& names);
// Runs the requests in order; false if one failed (unknown object, write error).
bool run(Ctx& c, const std::vector<std::string>& names);

bool parts(Ctx& c, const std::string& object);
bool build(Ctx& c, const std::string& object);
bool prims(Ctx& c);
bool shapeview(Ctx& c, const std::string& spec);
bool lineup(Ctx& c, const std::string& spec);
bool tile(Ctx& c, const std::string& spec);

}  // namespace ts::partscap
