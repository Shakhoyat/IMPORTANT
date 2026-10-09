// Report "shape views" (see CaptureParts.h): one design alone, left as the real renderer draws it, right made of
// primitives: every part flat-coloured by its primitive (report key, src/report/PrimitiveColors.h), Lambert-shaded,
// triangle edges on top, pulled apart from the design centre. The parts come from a freshly built Scene, so the pose
// is exactly the export's t = 0 state (ts --export-docs); the right panel is rasterised on the CPU so its colours are
// exactly the key's sRGB values.
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <tuple>

#include "app/CaptureParts.h"
#include "app/CapturePartsDetail.h"
#include "primitives/Library.h"
#include "rendering/LabStage.h"
#include "report/Designs.h"
#include "report/PrimitiveColors.h"
#include "report/ShapeView.h"
#include "scene/PartOrder.h"

namespace ts::partscap {

using namespace detail;
using namespace shape;

namespace {

CameraView toView(const Cam& c) {
    CameraView v;
    v.view = c.view;
    v.proj = c.proj;
    v.position = c.eye;
    v.nearZ = c.nearZ;
    v.farZ = c.farZ;
    return v;
}

// ------------------------------------------------------------------------------------------------ the export pose
// One Scene built exactly as --export-docs builds it, with its design catalogue and the export's node ids.
struct Fresh {
    Scene scene;
    designs::Catalogue cat;
    std::map<const SceneNode*, std::string> ids;
    Fresh() {
        scene.updateWorld();
        cat = designs::collect(scene.roots());
        std::map<std::string, int> seen;   // DocExport::exportId: repeats get "#2", "#3" in depth-first order
        std::function<void(const SceneNode&)> walk = [&](const SceneNode& n) {
            const int k = ++seen[n.id];
            ids[&n] = k == 1 ? n.id : n.id + "#" + std::to_string(k);
            for (const auto& ch : n.children) walk(*ch);
        };
        for (const SceneNode* r : scene.roots()) walk(*r);
    }
};

Fresh& fresh() {
    static std::unique_ptr<Fresh> f;
    if (!f) f = std::make_unique<Fresh>();
    return *f;
}

// Background of the model panel (its top and bottom corner rows), reused as the prims panel's gradient.
void cornerColours(const Image& img, Vec3& top, Vec3& bottom) {
    auto avg = [&](int y0) {
        Vec3 s{0, 0, 0};
        int n = 0;
        for (int y = y0; y < y0 + 8; ++y)
            for (int x : {2, 3, 4, 5, img.w - 6, img.w - 5, img.w - 4, img.w - 3}) {
                const size_t k = (static_cast<size_t>(y) * img.w + x) * 3;
                s = s + Vec3{img.px[k] / 255.0f, img.px[k + 1] / 255.0f, img.px[k + 2] / 255.0f};
                ++n;
            }
        return s * (1.0f / static_cast<float>(n));
    };
    top = avg(2);
    bottom = avg(img.h - 10);
}

std::string box4(int x0, int y0, int x1, int y1) {
    return "[" + std::to_string(x0) + ", " + std::to_string(y0) + ", " + std::to_string(x1) + ", " + std::to_string(y1) + "]";
}

// Crop box (final pixels) of a raster's covered area with a margin.
std::string cropOf(const Raster& R, float marginFrac) {
    if (R.x1 < 0) return box4(0, 0, R.W / kSS, R.H / kSS);
    const int w = R.W / kSS, h = R.H / kSS;
    const int x0 = R.x0 / kSS, x1 = R.x1 / kSS + 1, y0 = R.y0 / kSS, y1 = R.y1 / kSS + 1;
    const int m = static_cast<int>(marginFrac * static_cast<float>(std::max(x1 - x0, y1 - y0)));
    return box4(std::max(0, x0 - m), std::max(0, y0 - m), std::min(w, x1 + m), std::min(h, y1 + m));
}

// GPU model panel: the drawn parts through a lab stage (materials as in the scene, translucent parts as they are).
Image renderModel(Ctx& c, const std::vector<DrawPart>& parts, const Cam& cam, bool restOnly, bool shadows = true) {
    LabStage st;
    for (const DrawPart& d : parts) {
        if (!drawn(d) || d.prim == PrimitiveType::BeamCone || (restOnly && d.hidden)) continue;
        Material m = d.node->material;
        m.fogged = false;   // per-object distance fog and the far-backdrop dissolve are not gated by fogOn
        SceneNode* n = st.add(d.prim, m, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, d.node->shading, d.node->name);
        n->world = d.world;
        n->restScale = d.node->restScale;
    }
    RenderSettings s = baseSettings(c, LabStage::kObject);
    s.fogOn = false;
    s.practicalsOn = false;
    s.spotOn = false;
    // No post effects but anti-aliasing: the panel is compared part by part with the primitives panel.
    s.vignetteOn = false;
    s.grain = 0.0f;
    s.bloomOn = false;
    s.flareOn = false;
    s.aerialOn = false;
    s.dof = 0.0f;
    s.motionBlur = 0.0f;
    s.shadowsOn = shadows;
    st.install(*c.scene);
    c.renderer->labInvalidateCaches();
    Image img = renderImage(c, toView(cam), s, cam.w, cam.h);
    st.uninstall(*c.scene);
    c.renderer->labInvalidateCaches();
    return img;
}

std::string primKeyOf(const DrawPart& d) { return primkey::colorOf(d.prim, d.cuboid).key; }

}  // namespace

// ================================================================================================ camp
// The whole camp from the air, made of primitives: every top-level design instance that is not far backdrop / sky /
// terrain, each moved as a block by ksub (its centre - the camp centre); parts keep their place inside it (kpart).
bool campview(Ctx& c, const Spec& sp) {
    Fresh& f = fresh();
    const std::set<std::string> skip = {"Terrain disc", "Far ground", "Distant hills", "Far radio tower", "Treeline pine",
                                        "Sky clock (sun)", "Moon"};
    std::vector<DrawPart> parts;
    std::vector<std::pair<size_t, size_t>> ranges;   // per instance: [first, last) in parts
    std::map<std::string, int> legend;
    for (size_t i = 0; i < f.cat.instances.size(); ++i) {
        const designs::Instance& in = f.cat.instances[i];
        if (in.parent >= 0 || hiddenAtRest(*in.root)) continue;
        if (skip.count(f.cat.designs[static_cast<size_t>(in.design)].name)) continue;
        const size_t a = parts.size();
        for (DrawPart d : gatherParts(f.cat, static_cast<int>(i), {}, false)) {
            if (d.hidden) continue;   // the camp as it stands at t = 0
            d.nested = -1;
            parts.push_back(d);
            ++legend[primkey::colorOf(d.prim, d.cuboid).key];
        }
        ranges.emplace_back(a, parts.size());
    }
    std::vector<Vec3> all;
    for (const DrawPart& d : parts)
        if (d.prim != PrimitiveType::BeamCone) cornersOf(d.world, all);
    const Vec3 cc = boxCentre(all);
    for (const auto& r : ranges) {
        std::vector<Vec3> pts;
        for (size_t k = r.first; k < r.second; ++k)
            if (parts[k].prim != PrimitiveType::BeamCone) cornersOf(parts[k].world, pts);
        if (pts.empty()) continue;
        const Vec3 ci = boxCentre(pts);
        for (size_t k = r.first; k < r.second; ++k) {
            const Vec3 p = transformPoint(parts[k].world, {0, 0, 0});
            parts[k].offset = (ci - cc) * sp.ksub + (p - ci) * sp.kpart;
        }
    }
    const Vec3 dir = dirFrom(sp.az, sp.el);
    auto fitPts = [&](bool exploded) {
        std::vector<Vec3> pts;
        for (const DrawPart& d : parts)
            if (d.prim != PrimitiveType::BeamCone) cornersOf(exploded ? translate(d.offset) * d.world : d.world, pts);
        return pts;
    };
    const Cam cam = fitPoints(fitPts(sp.ksub > 0.0f || sp.kpart > 0.0f), dir, sp.w, sp.h, sp.fill);
    const std::string dir2 = c.dir + "/shapeview";
    std::error_code ec;
    std::filesystem::create_directories(dir2, ec);
    std::vector<Shaded> items;
    int id = 0;
    for (const DrawPart& d : parts) {
        Shaded s;
        s.mesh = &meshFor(d.prim);
        s.color = partColour(d);
        s.id = ++id;
        s.world = translate(d.offset) * d.world;
        s.fit = d.prim != PrimitiveType::BeamCone;
        items.push_back(s);
    }
    Raster R = makeRaster(sp.w, sp.h);
    rasterise(R, cam, items, lightFor(cam));
    drawEdges(R, cam, items);
    const Vec3 bgTop{0.925f, 0.925f, 0.93f}, bgBottom{0.88f, 0.88f, 0.89f};
    const bool ok = save(c, dir2 + "/camp_prims.png", resolve(R, bgTop, bgBottom));
    std::string lg = "{";
    int total = 0;
    for (const auto& kv : legend) {
        lg += std::string(lg.size() > 1 ? ", " : "") + "\"" + esc(kv.first) + "\": " + std::to_string(kv.second);
        total += kv.second;
    }
    std::ofstream(dir2 + "/camp.json") << "{\n  \"design\": \"@camp\", \"instances\": " << ranges.size() << ", \"parts\": " << total
                                       << ", \"ksub\": " << num(sp.ksub) << ", \"kpart\": " << num(sp.kpart) << ", \"camera\": {\"az_deg\": "
                                       << num(sp.az) << ", \"el_deg\": " << num(sp.el) << "}, \"size\": [" << sp.w << ", " << sp.h
                                       << "], \"prims\": \"camp_prims.png\", \"crop_prims\": " << cropOf(R, 0.02f)
                                       << ",\n  \"left_out\": [\"far backdrop, sky bodies, terrain disc\", \"parts hidden at t = 0\"],\n  \"legend\": " << lg << "}\n}\n";
    return ok;
}

// ================================================================================================ shapeview
bool shapeview(Ctx& c, const std::string& text) {
    const Spec sp = parseSpec(text);
    if (sp.design == "@camp") return campview(c, sp);
    Fresh& f = fresh();
    const designs::Design* ds = f.cat.find(sp.design);
    if (!ds || sp.inst < 1 || sp.inst > static_cast<int>(ds->instances.size())) {
        std::fprintf(stderr, "shapeview: no design '%s' (instance %d)\n", sp.design.c_str(), sp.inst);
        logLine(c, "shapeview: no design '" + sp.design + "'");
        return false;
    }
    const int inst = ds->instances[static_cast<size_t>(sp.inst - 1)];
    const designs::Instance& in = f.cat.instances[static_cast<size_t>(inst)];
    std::vector<DrawPart> parts = gatherParts(f.cat, inst, sp.cut, c.cutaway);
    const std::map<const SceneNode*, GroupInfo> groups = explode(parts, in.root, sp.ksub, sp.kpart);
    // A design whose own parts are all hidden at t = 0 (a missile in its launcher, an ejected casing) is drawn at its
    // rest scale in both panels.
    bool allHidden = true;
    for (const DrawPart& d : parts) allHidden = allHidden && (d.nested >= 0 || d.hidden);

    // Front of the design: +Z of its root (a one-part design looks along world +Z).
    const Mat4& rw = in.root->world;
    const float heading = in.root->isLeaf() ? 0.0f : degrees(std::atan2(rw.at(0, 2), rw.at(2, 2)));
    const Vec3 dir = dirFrom(heading + sp.az, sp.el);

    // Both panels fitted to their own geometry (mesh vertices; the corners of huge meshes): the projection centred,
    // its larger extent 'fill' of the frame. The light-beam frustum is drawn but not fitted (it reaches the ground).
    auto fitPts = [&](bool exploded) {
        std::vector<Vec3> pts;
        for (const DrawPart& d : parts) {
            if (!drawn(d) || d.prim == PrimitiveType::BeamCone || (!exploded && d.hidden && !allHidden)) continue;
            const Mat4 w = exploded ? translate(d.offset) * d.world : d.world;
            const MeshData& m = meshFor(d.prim);
            if (m.vertices.size() > 4000) meshBoxCorners(m, w, pts);
            else
                for (const Vertex& v : m.vertices) pts.push_back(transformPoint(w, v.position));
        }
        if (pts.empty()) cornersOf(in.root->world, pts);
        return pts;
    };
    const Cam camA = fitPoints(fitPts(false), dir, sp.w, sp.h, sp.fill);
    const Cam camB = fitPoints(fitPts(true), dir, sp.w, sp.h, sp.fill);

    const std::string dir2 = c.dir + "/shapeview";
    std::error_code ec;
    std::filesystem::create_directories(dir2, ec);
    const std::string sl = slug(sp.design) + (sp.inst > 1 ? "_" + std::to_string(sp.inst) : "") + (c.cutaway && !sp.cut.empty() ? "_cut" : "");
    if (sp.stage > 0) {
        // Build stage in the colour key: the first ceil(stage N / 100) parts in hierarchy order, the camera of the
        // whole design (so the four stages of a strip line up).
        const int total = static_cast<int>(parts.size()), shown = buildStageCount(total, sp.stage);
        std::vector<Shaded> items;
        int id = 0;
        for (const DrawPart& d : parts) {
            if (id >= shown) break;
            ++id;
            if (!drawn(d) || (d.hidden && !allHidden)) continue;
            Shaded s;
            s.mesh = &meshFor(d.prim);
            s.color = partColour(d);
            s.id = id;
            s.world = translate(d.offset) * d.world;
            s.fit = d.prim != PrimitiveType::BeamCone;
            items.push_back(s);
        }
        Raster R = makeRaster(sp.w, sp.h);
        rasterise(R, camB, items, lightFor(camB));
        drawEdges(R, camB, items);
        const std::string fn = sl + "_stage" + std::to_string(sp.stage);
        const bool ok = save(c, dir2 + "/" + fn + ".png", resolve(R, {0.925f, 0.925f, 0.93f}, {0.88f, 0.88f, 0.89f}));
        std::ofstream(dir2 + "/" + fn + ".json") << "{\"design\": \"" << esc(sp.design) << "\", \"stage_pct\": " << sp.stage
                                                 << ", \"parts\": " << shown << ", \"of\": " << total << ", \"last_part\": \""
                                                 << esc(parts[static_cast<size_t>(std::max(0, shown - 1))].node->name)
                                                 << "\", \"prims\": \"" << fn << ".png\"}\n";
        return ok;
    }

    // Left: the model as the renderer draws it (rest pose: parts hidden at t = 0 stay hidden), except a design whose
    // own parts are all hidden at t = 0 (see allHidden).
    const Image model = renderModel(c, parts, camA, !allHidden);
    bool ok = save(c, dir2 + "/" + sl + "_model.png", model);
    Vec3 bgTop, bgBottom;
    cornerColours(model, bgTop, bgBottom);

    // Right: made of primitives, pulled apart. Silhouette box of the left panel from the same CPU raster (unexploded).
    std::vector<Shaded> itemsA, itemsB;
    int id = 0;
    for (const DrawPart& d : parts) {
        ++id;
        if (!drawn(d)) continue;
        Shaded s;
        s.mesh = &meshFor(d.prim);
        s.color = partColour(d);
        s.id = id;
        s.edges = s.mesh->vertices.size() < 20000;
        s.fit = d.prim != PrimitiveType::BeamCone;
        if ((!d.hidden || allHidden) && d.prim != PrimitiveType::BeamCone) {
            s.world = d.world;
            itemsA.push_back(s);
        }
        s.world = translate(d.offset) * d.world;
        itemsB.push_back(s);
    }
    Raster RA = makeRaster(sp.w, sp.h);
    rasterise(RA, camA, itemsA, lightFor(camA));
    Raster RB = makeRaster(sp.w, sp.h);
    rasterise(RB, camB, itemsB, lightFor(camB));
    drawEdges(RB, camB, itemsB);
    const Image prims = resolve(RB, bgTop, bgBottom);
    ok = save(c, dir2 + "/" + sl + "_prims.png", prims) && ok;

    // JSON: legend (own parts by primitive, hidden ones included), nested designs, every part, groups with projected centres.
    std::map<std::string, int> legend, legendDrawn;
    std::map<std::string, int> nestedCount, nestedParts;
    std::map<std::string, int> removed;
    std::string pj;
    id = 0;
    for (const DrawPart& d : parts) {
        ++id;
        const std::string key = primKeyOf(d);
        std::string nestedName;
        if (d.nested >= 0) {
            nestedName = f.cat.designs[static_cast<size_t>(f.cat.instances[static_cast<size_t>(d.nested)].design)].name;
            ++nestedParts[nestedName];
        } else {
            ++legend[key];
            if (drawn(d)) ++legendDrawn[key];
        }
        if (d.cut) ++removed[d.node->name];
        float x = -1, y = -1;
        if (drawn(d)) project(camB, transformPoint(translate(d.offset) * d.world, {0, 0, 0}), x, y);
        pj += std::string(pj.empty() ? "" : ",\n") + "    {\"id\": \"" + esc(f.ids[d.node]) + "\", \"name\": \"" + esc(d.node->name) +
              "\", \"primitive\": \"" + esc(key) + "\", \"nested_design\": \"" + esc(nestedName) + "\", \"dagger\": " +
              (d.hidden ? "true" : "false") + ", \"parked\": " + (d.parked ? "true" : "false") + ", \"cut\": " +
              (d.cut ? "true" : "false") + ", \"px\": [" + num(x) + ", " + num(y) + "]}";
    }
    for (int nid : in.nested) ++nestedCount[f.cat.designs[static_cast<size_t>(f.cat.instances[static_cast<size_t>(nid)].design)].name];
    auto mapJson = [](const std::map<std::string, int>& m) {
        std::string s = "{";
        for (const auto& kv : m) s += std::string(s.size() > 1 ? ", " : "") + "\"" + esc(kv.first) + "\": " + std::to_string(kv.second);
        return s + "}";
    };
    std::string gj;
    for (const auto& kv : groups) {
        const GroupInfo& g = kv.second;
        float xa = -1, ya = -1, xb = -1, yb = -1;
        project(camA, g.centre, xa, ya);
        project(camB, g.centre + g.offset, xb, yb);
        const int nd = f.cat.instanceAt(*g.node);
        const bool nestedRoot = nd >= 0 && nd != inst && f.cat.instances[static_cast<size_t>(nd)].root == g.node;
        gj += std::string(gj.empty() ? "" : ",\n") + "    {\"id\": \"" + esc(f.ids[g.node]) + "\", \"name\": \"" + esc(g.node->name) +
              "\", \"parent_id\": \"" + esc(g.node->parent && g.node != in.root ? f.ids[g.node->parent] : std::string()) +
              "\", \"level\": " + std::to_string(g.level) + ", \"parts\": " + std::to_string(g.parts) + ", \"nested_root\": " +
              (nestedRoot ? "true" : "false") + ", \"px_model\": [" + num(xa) + ", " + num(ya) + "], \"px_prims\": [" + num(xb) +
              ", " + num(yb) + "]}";
    }
    std::string cutj;
    for (const std::string& s : sp.cut) cutj += std::string(cutj.empty() ? "" : ", ") + "\"" + esc(s) + "\"";
    const Vec3 pos{rw.at(0, 3), rw.at(1, 3), rw.at(2, 3)};
    std::string json = "{\n  \"design\": \"" + esc(sp.design) + "\", \"family\": \"" + esc(ds->family) + "\", \"instance\": " +
                       std::to_string(sp.inst) + ", \"instances\": " + std::to_string(ds->instances.size()) + ", \"root_id\": \"" +
                       esc(f.ids[in.root]) + "\", \"root_name\": \"" + esc(in.root->name) + "\",\n  \"placement\": {\"position\": [" +
                       num(pos.x) + ", " + num(pos.y) + ", " + num(pos.z) + "], \"heading_deg\": " + num(heading) + "},\n" +
                       "  \"camera\": {\"az_deg\": " + num(sp.az) + ", \"el_deg\": " + num(sp.el) + ", \"fov_deg\": " + num(kFov) +
                       ", \"fill\": " + num(sp.fill) + "}, \"ksub\": " + num(sp.ksub) + ", \"kpart\": " + num(sp.kpart) +
                       ", \"cutaway\": " + (c.cutaway && !sp.cut.empty() ? "true" : "false") + ", \"cut\": [" + cutj + "],\n" +
                       "  \"size\": [" + std::to_string(sp.w) + ", " + std::to_string(sp.h) + "], \"model\": \"" + sl +
                       "_model.png\", \"prims\": \"" + sl + "_prims.png\", \"crop_model\": " + cropOf(RA, 0.04f) +
                       ", \"crop_prims\": " + cropOf(RB, 0.04f) + ",\n  \"own_parts\": " +
                       std::to_string(in.own.size()) + ", \"legend\": " + mapJson(legend) + ", \"legend_drawn\": " + mapJson(legendDrawn) +
                       ",\n  \"nested\": " + mapJson(nestedCount) + ", \"nested_parts\": " + mapJson(nestedParts) + ", \"removed\": " +
                       mapJson(removed) + ",\n  \"parts\": [\n" + pj + "\n  ],\n  \"groups\": [\n" + gj + "\n  ]\n}\n";
    std::ofstream(dir2 + "/" + sl + ".json") << json;
    logLine(c, "shapeview " + sp.design + ": " + std::to_string(parts.size()) + " parts -> " + dir2 + "/" + sl);
    return ok;
}

// ================================================================================================ lineup
// Several designs at one scale: each in its own root frame (axis along local +Z) turned to lie along +X, stacked top to
// bottom in the given order, left-aligned; orthographic camera from the front, raised by 15 deg (X lengths are true).
bool lineup(Ctx& c, const std::string& text) {
    Fresh& f = fresh();
    std::vector<std::string> names;
    size_t a = 0;
    while (a <= text.size()) {
        const size_t b = text.find('+', a);
        names.push_back(text.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    struct Item {
        std::string design;
        std::vector<DrawPart> parts;
        Vec3 lo, hi;
        float y = 0;
    };
    std::vector<Item> items;
    for (const std::string& nm : names) {
        const designs::Design* ds = f.cat.find(nm);
        if (!ds) {
            std::fprintf(stderr, "lineup: no design '%s'\n", nm.c_str());
            return false;
        }
        const int inst = ds->instances.front();
        const designs::Instance& in = f.cat.instances[static_cast<size_t>(inst)];
        Item it;
        it.design = nm;
        it.parts = gatherParts(f.cat, inst, {}, false);
        // The exhaust flame is an effect of a fired round, not its body: left out of the size comparison.
        it.parts.erase(std::remove_if(it.parts.begin(), it.parts.end(),
                                      [](const DrawPart& d) { return d.node->name.find("flame") != std::string::npos; }),
                       it.parts.end());
        // Root frame: the instance root's world (a one-part design: its own rest scale only).
        const Mat4 inv = in.root->isLeaf() ? Mat4::identity() : inverse(in.root->world);
        it.lo = {1e9f, 1e9f, 1e9f};
        it.hi = {-1e9f, -1e9f, -1e9f};
        for (DrawPart& d : it.parts) {
            d.parked = false;
            d.world = rotateY(radians(90.0f)) * (in.root->isLeaf() ? scale(restScaleOf(*d.node)) : inv * d.world);
            for (const Vertex& v : meshFor(d.prim).vertices) {
                const Vec3 p = transformPoint(d.world, v.position);
                it.lo = {std::min(it.lo.x, p.x), std::min(it.lo.y, p.y), std::min(it.lo.z, p.z)};
                it.hi = {std::max(it.hi.x, p.x), std::max(it.hi.y, p.y), std::max(it.hi.z, p.z)};
            }
        }
        items.push_back(it);
    }
    float maxLen = 0.0f;
    for (const Item& it : items) maxLen = std::max(maxLen, it.hi.x - it.lo.x);
    const float gap = 0.08f * maxLen;
    float y = 0.0f;
    std::string ij;
    for (Item& it : items) {
        const float h = it.hi.y - it.lo.y;
        it.y = y - it.hi.y;   // top of this item at y
        for (DrawPart& d : it.parts) d.world = translate({-it.lo.x, it.y, 0}) * d.world;
        y -= h + gap;
    }
    // Orthographic camera: from +Z, raised by 15 deg about X.
    const float el = 25.0f;
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    std::vector<Vec3> pts;
    for (const Item& it : items)
        for (const DrawPart& d : it.parts)
            for (const Vertex& v : meshFor(d.prim).vertices) pts.push_back(transformPoint(d.world, v.position));
    const Vec3 centre = boxCentre(pts);
    const Vec3 dir = dirFrom(0.0f, el);
    Cam cam;
    cam.ortho = true;
    cam.fwd = dir * -1.0f;
    cam.right = normalize(cross(cam.fwd, Vec3{0, 1, 0}));
    cam.up = cross(cam.right, cam.fwd);
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, r = 0.0f;
    for (const Vec3& p : pts) {
        const Vec3 v = p - centre;
        x0 = std::min(x0, dot(v, cam.right));
        x1 = std::max(x1, dot(v, cam.right));
        y0 = std::min(y0, dot(v, cam.up));
        y1 = std::max(y1, dot(v, cam.up));
        r = std::max(r, length(v));
    }
    const float m = 0.04f * std::max(x1 - x0, y1 - y0);
    x0 -= m;
    x1 += m;
    y0 -= m;
    y1 += m;
    cam.w = 2000;
    cam.h = std::max(200, static_cast<int>(std::lround(2000.0f * (y1 - y0) / (x1 - x0))));
    cam.eye = centre + dir * (3.0f * r);
    cam.view = lookAt(cam.eye, centre, {0, 1, 0});
    cam.proj = ortho(x0, x1, y0, y1, 0.5f * r, 6.0f * r);
    const float pxPerM = static_cast<float>(cam.w) / (x1 - x0);

    std::vector<DrawPart> all;
    for (const Item& it : items) all.insert(all.end(), it.parts.begin(), it.parts.end());
    for (DrawPart& d : all) d.hidden = false;
    const std::string dir2 = c.dir + "/shapeview";
    std::error_code ec;
    std::filesystem::create_directories(dir2, ec);
    std::string sl = "lineup";
    for (const std::string& n : names) sl += "_" + slug(n);
    const Image model = renderModel(c, all, cam, false, false);   // no shadows: the rounds are stacked
    bool ok = save(c, dir2 + "/" + sl + "_model.png", model);
    Vec3 bgTop, bgBottom;
    cornerColours(model, bgTop, bgBottom);
    std::vector<Shaded> sh;
    int id = 0;
    for (const DrawPart& d : all) {
        Shaded s;
        s.mesh = &meshFor(d.prim);
        s.color = partColour(d);
        s.id = ++id;
        s.world = d.world;
        sh.push_back(s);
    }
    Raster R = makeRaster(cam.w, cam.h);
    rasterise(R, cam, sh, lightFor(cam));
    drawEdges(R, cam, sh);
    ok = save(c, dir2 + "/" + sl + "_prims.png", resolve(R, bgTop, bgBottom)) && ok;
    for (const Item& it : items) {
        float lx, ly;
        project(cam, Vec3{0.0f, it.y + it.hi.y, 0.0f}, lx, ly);
        std::map<std::string, int> legend;
        for (const DrawPart& d : it.parts) ++legend[primKeyOf(d)];
        std::string lg = "{";
        for (const auto& kv : legend) lg += std::string(lg.size() > 1 ? ", " : "") + "\"" + esc(kv.first) + "\": " + std::to_string(kv.second);
        lg += "}";
        ij += std::string(ij.empty() ? "" : ",\n") + "    {\"design\": \"" + esc(it.design) + "\", \"length_m\": " +
              num(it.hi.x - it.lo.x) + ", \"diameter_m\": " + num(std::max(it.hi.y - it.lo.y, it.hi.z - it.lo.z)) +
              ", \"parts\": " + std::to_string(it.parts.size()) + ", \"legend\": " + lg + ", \"label_px\": [" + num(lx) + ", " +
              num(ly) + "]}";
    }
    std::ofstream(dir2 + "/" + sl + ".json") << "{\n  \"size\": [" << cam.w << ", " << cam.h << "], \"px_per_m\": " << num(pxPerM)
                                              << ", \"el_deg\": " << num(el) << ", \"left_out\": \"exhaust flame (Round flame)\", \"model\": \"" << sl << "_model.png\", \"prims\": \""
                                              << sl << "_prims.png\",\n  \"items\": [\n" << ij << "\n  ]\n}\n";
    return ok;
}

// ================================================================================================ tile
// One part as a parts:<object> tile: tile:<object>|<part name> (first leaf of that name in hierarchy order).
bool tile(Ctx& c, const std::string& text) {
    const size_t bar = text.find('|');
    const std::string object = text.substr(0, bar), part = bar == std::string::npos ? "" : text.substr(bar + 1);
    const SceneNode* hit = nullptr;
    for (const SceneNode* r : c.scene->roots()) {
        if (r->object != object) continue;
        for (const SceneNode* n : rigLeavesDfs(*r))
            if (n->name == part || partKindName(n->name) == part) {
                hit = n;
                break;
            }
        if (hit) break;
    }
    if (!hit) {
        std::fprintf(stderr, "tile: no part '%s' in '%s'\n", part.c_str(), object.c_str());
        logLine(c, "tile: no part '" + part + "' in '" + object + "'");
        return false;
    }
    const std::string dir2 = c.dir + "/tiles";
    std::error_code ec;
    std::filesystem::create_directories(dir2, ec);
    const SceneNode& n = *hit;
    const Vec3 S = restScaleOf(n), R = n.local.rotationDeg;
    const MeshData mesh = sceneMesh(n.primitive);
    Vec3 centre;
    float radius;
    meshSphere(mesh, rotateEulerYXZ(R) * scale(S), centre, radius);
    const float kNorm = 1.0f / radius;
    const float fill = 0.7f, fov = 30.0f;
    const Vec3 target{0.0f, 3.0f, 0.0f};
    const CameraView cam = viewFrom(target, dirFrom(sunnyYaw(c, 0.0f, 35.0f), 25.0f), 1.0f / (fill * std::tan(radians(fov) * 0.5f)), fov, 1.0f);
    RenderSettings s = baseSettings(c, LabStage::kObject);
    s.fogOn = false;
    s.practicalsOn = false;
    s.spotOn = false;
    Material m = n.material;
    m.opacity = 1.0f;
    const bool beam = n.primitive == PrimitiveType::BeamCone;
    if (beam) {
        c.renderer->labOverrideMesh(PrimitiveType::Cylinder, mesh);
        m.emissive = 0.0f;
    }
    LabStage st;
    SceneNode* node = st.add(beam ? PrimitiveType::Cylinder : n.primitive, m, target - centre * kNorm, S * kNorm, R,
                             beam ? ShadingModel::Phong : n.shading, n.name);
    node->restScale = S;
    st.install(*c.scene);
    c.renderer->labInvalidateCaches();
    const Image img = renderImage(c, cam, s, 512, 512);
    st.uninstall(*c.scene);
    if (beam) c.renderer->labOverrideMesh(PrimitiveType::Cylinder, MeshData{});
    c.renderer->labInvalidateCaches();
    const std::string sl = slug(object) + "__" + slug(part);
    const bool ok = save(c, dir2 + "/" + sl + ".png", img);
    std::ofstream(dir2 + "/" + sl + ".json") << "{\"object\": \"" << esc(object) << "\", \"part\": \"" << esc(n.name)
                                             << "\", \"primitive\": \"" << primitiveName(n.primitive) << "\", \"S_cm\": ["
                                             << num(S.x * 100) << ", " << num(S.y * 100) << ", " << num(S.z * 100) << "]}\n";
    return ok;
}

}  // namespace ts::partscap
