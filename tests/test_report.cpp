// Final report pass (T440-T449): the design catalogue behind the export's "designs" list, the primitive colour key, the
// CPU half of the shape-view capture (request syntax, cutaway, pull-apart offsets, software rasteriser) and the
// low-resolution generator calls of the primitive cards. CPU only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "TestKit.h"
#include "objects/Terrain.h"
#include "primitives/Annulus.h"
#include "primitives/Capsule.h"
#include "primitives/Cylinder.h"
#include "primitives/Helix.h"
#include "primitives/Hemisphere.h"
#include "primitives/Library.h"
#include "primitives/Sphere.h"
#include "primitives/Torus.h"
#include "report/Designs.h"
#include "report/PrimitiveColors.h"
#include "report/ShapeView.h"
#include "scene/PartOrder.h"
#include "scene/Scene.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

std::string keyOf(const SceneNode& n) {
    return primkey::colorOf(n.primitive, n.primitive == PrimitiveType::Cube && isCuboidScale(restScaleOf(n))).key;
}

void testDesigns() {
    Scene sc;
    sc.updateWorld();
    const designs::Catalogue cat = designs::collect(sc.roots());
    // T440 every leaf belongs to exactly one design instance; the designs add up to the scene.
    {
        int sum = 0, uniformBad = 0;
        std::set<const SceneNode*> seen;
        int dup = 0;
        for (const designs::Design& d : cat.designs) {
            std::set<int> sizes;
            for (int i : d.instances) {
                sizes.insert(cat.partsOf(i));
                sum += cat.partsOf(i);
                for (const SceneNode* l : cat.instances[static_cast<size_t>(i)].own) dup += !seen.insert(l).second;
            }
            // instances x parts = total for every design whose instances all have the same part count
            int total = 0;
            for (int i : d.instances) total += cat.partsOf(i);
            if (sizes.size() == 1 && static_cast<int>(d.instances.size()) * *sizes.begin() != total) ++uniformBad;
        }
        const int leaves = static_cast<int>(sc.leaves().size());
        record("T440", "Report designs cover the scene: sum over designs of instances x parts = scene total, every leaf once",
               "sum = leaves, 0 unassigned, 0 counted twice",
               fmt("%zu designs, sum %d / %d leaves, unassigned %zu, twice %d, uniform mismatches %d", cat.designs.size(), sum,
                   leaves, cat.unassigned.size(), dup, uniformBad),
               sum == leaves && cat.unassigned.empty() && dup == 0 && uniformBad == 0 && leaves > 5000);
    }
    // T441 the aggregate objects are split into the designs they place.
    {
        bool noAggregate = true;
        for (const char* agg : {"Base", "Environment", "Camp camouflage", "Backdrop", "UAV pads"})
            noAggregate = noAggregate && !cat.find(agg);
        auto count = [&](const char* name) {
            const designs::Design* d = cat.find(name);
            return d ? static_cast<int>(d->instances.size()) : 0;
        };
        int pineGroups = 0;
        for (const SceneNode* l : sc.leaves())
            if (l->parent && l->parent->name == "Pine" && l->parent->children.front().get() == l) ++pineGroups;
        const designs::Design* tower = cat.find("Guard tower");
        bool towerSame = tower != nullptr;
        if (tower)
            for (int i : tower->instances) towerSame = towerSame && cat.partsOf(i) == cat.partsOf(tower->instances.front());
        // nested: the camouflage net over the bunker ramp, the cargo trucks in the motor pool
        const designs::Design* bunker = cat.find("Command bunker");
        int bunkerNets = 0;
        if (bunker)
            for (int n : cat.instances[static_cast<size_t>(bunker->instances.front())].nested)
                bunkerNets += cat.designs[static_cast<size_t>(cat.instances[static_cast<size_t>(n)].design)].name == "Camouflage net";
        const bool ok = noAggregate && count("Guard tower") == 4 && towerSame && count("Pine") == pineGroups && pineGroups > 10 &&
                        count("Soldier") == 8 && count("Main battle tank") == 3 && count("Cargo truck") == 2 && bunkerNets == 1 &&
                        count("UAV round") == 8 && count("MLRS rocket") == 6 && count("AA shell casing") == 20;
        record("T441", "Aggregates split into designs (no Base / Environment / Camp / Backdrop row); nested designs found",
               "guard tower x4 (same parts), pine = Pine groups, soldier x8, tank x3, cargo truck x2, UAV round x8, MLRS rocket x6, "
               "AA casing x20, 1 net nested in the command bunker",
               fmt("aggregates absent %d, towers %d (same %d), pines %d / %d groups, soldiers %d, tanks %d, trucks %d, UAV rounds %d, "
                   "rockets %d, casings %d, bunker nets %d",
                   noAggregate, count("Guard tower"), towerSame, count("Pine"), pineGroups, count("Soldier"), count("Main battle tank"),
                   count("Cargo truck"), count("UAV round"), count("MLRS rocket"), count("AA shell casing"), bunkerNets),
               ok);
    }
    // T442 per-primitive totals of the designs = the scene's primitive instance counts (Section 6 = Section 19).
    {
        std::map<std::string, int> scene, designs;
        for (const SceneNode* l : sc.leaves()) ++scene[keyOf(*l)];
        for (const designs::Instance& in : cat.instances)
            for (const SceneNode* l : in.own) ++designs[keyOf(*l)];
        int bad = 0;
        for (const auto& kv : scene) bad += designs[kv.first] != kv.second;
        record("T442", "Per-primitive part totals over all designs equal the scene's primitive instance counts",
               "every primitive key equal", fmt("%zu keys, %d different (cuboid %d / %d)", scene.size(), bad, designs["Cuboid"], scene["Cuboid"]),
               bad == 0 && scene.size() >= 12);
    }
    // T443 shape-view request syntax and cutaway: the named parts are cut, nested designs flagged, legend = own parts.
    {
        const shape::Spec sp = shape::parseSpec("Command bunker|az=200|el=40|ksub=0.7|kpart=0.2|inst=1|cut=Roof slab*;Camouflage net");
        bool parsed = sp.design == "Command bunker" && sp.az == 200.0f && sp.el == 40.0f && sp.ksub == 0.7f && sp.kpart == 0.2f &&
                      sp.cut.size() == 2 && shape::nameMatches("Roof slab A", "Roof slab*") && !shape::nameMatches("Roof", "Roof slab*") &&
                      shape::nameMatches("Camouflage net", "Camouflage net");
        const designs::Design* bunker = cat.find("Command bunker");
        int cut = 0, nested = 0, own = 0, roofCut = 0, cutWithout = 0;
        if (bunker) {
            const int inst = bunker->instances.front();
            for (const shape::DrawPart& d : shape::gatherParts(cat, inst, sp.cut, true)) {
                cut += d.cut;
                nested += d.nested >= 0;
                own += d.nested < 0;
                roofCut += d.cut && d.node->name.rfind("Roof slab", 0) == 0;
            }
            for (const shape::DrawPart& d : shape::gatherParts(cat, inst, sp.cut, false)) cutWithout += d.cut;
        }
        const int ownExpected = bunker ? cat.partsOf(bunker->instances.front()) : -1;
        record("T443", "Shape-view request: options parsed; --cutaway leaves out the named parts (and only with the flag)",
               "options read back; roof slabs + the nested net cut; own parts = the design's parts; nothing cut without --cutaway",
               fmt("parsed %d; cut %d (roof slabs %d), nested %d, own %d / %d, cut without flag %d", parsed, cut, roofCut, nested, own,
                   ownExpected, cutWithout),
               parsed && roofCut == 2 && cut == roofCut + nested && nested > 0 && own == ownExpected && cutWithout == 0);
    }
    // T444 pull-apart: parts move away from their group centre, sub-assemblies move as blocks, k = 0 changes nothing.
    {
        const designs::Design* tank = cat.find("Main battle tank");
        int towards = 0, moved = 0, still = 0, blockSame = 0, blockParts = 0;
        if (tank) {
            const int inst = tank->instances.front();
            const SceneNode* root = cat.instances[static_cast<size_t>(inst)].root;
            std::vector<shape::DrawPart> parts = shape::gatherParts(cat, inst, {}, false);
            const auto groups = shape::explode(parts, root, 0.5f, 0.0f);   // kpart 0: a group's parts share its offset
            for (const shape::DrawPart& d : parts) {
                if (!shape::drawn(d)) continue;
                const auto g = groups.find(d.node->parent);
                if (g == groups.end()) continue;
                ++blockParts;
                blockSame += length(d.offset - g->second.offset) < 1e-5f;
            }
            const auto g2 = shape::explode(parts, root, 0.5f, 0.3f);
            for (const shape::DrawPart& d : parts) {
                if (!shape::drawn(d) || d.prim == PrimitiveType::BeamCone) continue;
                const auto g = g2.find(d.node->parent);
                const Vec3 c = transformPoint(d.world, {0, 0, 0});
                const Vec3 own = d.offset - g->second.offset;   // the part's own share: kpart (c - group centre)
                if (length(own) > 1e-6f) ++moved;
                if (dot(own, c - g->second.centre) < -1e-6f) ++towards;
            }
            shape::explode(parts, root, 0.0f, 0.0f);
            for (const shape::DrawPart& d : parts) still += length(d.offset) > 1e-6f;
        }
        record("T444", "Pull-apart offsets: k_part moves each part away from its group centre, k_sub moves groups as blocks",
               "with k_part = 0 every part has its group's offset; parts off their group centre move (> 150), none towards it; "
               "k = 0: no offset",
               fmt("block offsets shared %d / %d, parts moved %d, towards centre %d, offsets with k = 0: %d", blockSame, blockParts, moved,
                   towards, still),
               tank && blockSame == blockParts && blockParts > 200 && moved > 150 && towards == 0 && still == 0);
    }
    // T445 the software rasteriser draws a part in its key colour (Lambert keeps the hue), leaves the background, crops.
    {
        const MeshData& cube = shape::meshFor(PrimitiveType::Cube);
        shape::Shaded s;
        s.mesh = &cube;
        s.world = scale({1.0f, 1.0f, 1.0f});
        const primkey::PrimColor& key = primkey::colorOf(PrimitiveType::Cube, false);
        s.color = primkey::srgb(key);
        s.id = 1;
        std::vector<Vec3> pts;
        shape::cornersOf(s.world, pts);
        const shape::Cam cam = shape::fitPoints(pts, normalize(Vec3{0.5f, 0.45f, 0.75f}), 200, 150, 0.8f);
        shape::Raster R = shape::makeRaster(cam.w, cam.h);
        shape::rasterise(R, cam, {s}, shape::lightFor(cam));
        const shape::Image img = shape::resolve(R, {1, 1, 1}, {1, 1, 1});
        const size_t mid = (static_cast<size_t>(img.h / 2) * img.w + img.w / 2) * 3;
        const float r = img.px[mid], g = img.px[mid + 1], b = img.px[mid + 2];
        // hue kept: the pixel is the key colour times one factor (blue channel of the cube key is the largest)
        const float k = b / key.b;
        const bool hue = std::fabs(g - key.g * k) <= 3.0f && std::fabs(r - key.r * k) <= 3.0f && k > 0.35f && k <= 1.0f;
        const bool corner = img.px[0] == 255 && img.px[1] == 255 && img.px[2] == 255;
        const int bw = (R.x1 - R.x0) / shape::kSS, bh = (R.y1 - R.y0) / shape::kSS;
        const bool fill = std::max(bw / static_cast<float>(cam.w), bh / static_cast<float>(cam.h)) > 0.7f &&
                          std::max(bw / static_cast<float>(cam.w), bh / static_cast<float>(cam.h)) < 0.9f;
        record("T445", "Shape-view rasteriser: a cube in its key colour (Lambert factor only), background untouched, fit ~80 %",
               "centre pixel = key x k (0.35 < k <= 1), corner = background, larger extent 70-90 % of the frame",
               fmt("centre (%.0f, %.0f, %.0f) = key x %.2f, corner bg %d, extent %d x %d px of %d x %d", r, g, b, k, corner, bw, bh, cam.w,
                   cam.h),
               hue && corner && fill);
    }
}

// T446 the primitive colour key: 16 distinct colours, families in disjoint L* bands, cube / cuboid told apart.
void testColorKey() {
    std::set<std::tuple<int, int, int>> rgb;
    std::map<std::string, std::pair<float, float>> band;
    for (const primkey::PrimColor& c : primkey::kColors) {
        rgb.insert({c.r, c.g, c.b});
        auto lin = [](uint8_t v) {
            const float s = v / 255.0f;
            return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
        };
        const float y = 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
        const float L = 116.0f * std::cbrt(y) - 16.0f;
        auto it = band.find(c.family);
        if (it == band.end()) band[c.family] = {L, L};
        else it->second = {std::min(it->second.first, L), std::max(it->second.second, L)};
    }
    std::vector<std::pair<float, float>> b;
    for (const auto& kv : band) b.push_back(kv.second);
    std::sort(b.begin(), b.end());
    float gap = 1e9f;
    for (size_t i = 1; i < b.size(); ++i) gap = std::min(gap, b[i].first - b[i - 1].second);
    const bool cuboid = std::string(primkey::colorOf(PrimitiveType::Cube, true).key) == "Cuboid" &&
                        std::string(primkey::colorOf(PrimitiveType::Cube, false).key) == "Cube" &&
                        std::string(primkey::colorOf(PrimitiveType::BeamCone, false).key) == "Cylinder (frustum)";
    record("T446", "Primitive colour key: 16 distinct colours, 5 families in disjoint greyscale (L*) bands",
           "16 colours, 5 families, smallest gap between family bands >= 2.5 L*, cube / cuboid / frustum keys",
           fmt("%zu colours, %zu families, smallest band gap %.1f L*, keys %d", rgb.size(), band.size(), gap, cuboid),
           rgb.size() == 16 && band.size() == 5 && gap >= 2.5f && cuboid);
}

// T447 the primitive cards' low-resolution meshes: the real generators at small parameters obey the library formulas.
void testLowres() {
    struct Case {
        const char* name;
        MeshData m;
        size_t V, T;
    };
    const int s = 8;
    const std::vector<Case> cases = {
        {"sphere 8x4", createSphere(8, 4), static_cast<size_t>((4 - 1) * s + 2), static_cast<size_t>(2 * s * (4 - 1))},
        {"hemisphere 8x2", createHemisphere(8, 2), static_cast<size_t>(s * (2 + 1) + 2), static_cast<size_t>(2 * s * 2)},
        {"cylinder 8", createCylinder(8, 0.5f, 0.5f, true), static_cast<size_t>(4 * s + 2), static_cast<size_t>(4 * s)},
        {"frustum 8", createCylinder(8, lib::kBeamTop, lib::kBeamBottom, false), static_cast<size_t>(2 * s), static_cast<size_t>(2 * s)},
        {"cone 8", createCylinder(8, 0.5f, 0.0f, true), static_cast<size_t>(3 * s + 1), static_cast<size_t>(2 * s)},
        {"torus 8x6", createTorus(0.35f, 0.15f, 8, 6), 48, 96},
        {"capsule 8x2", createCapsule(8, 2, 0.25f), static_cast<size_t>(2 * 2 * s + 2), static_cast<size_t>(4 * 2 * s)},
        {"annulus 8", createAnnulus(0.43f, 8), static_cast<size_t>(4 * s), static_cast<size_t>(4 * s)},
        {"helix 16x6", createHelix(0.475f, 0.025f, 2.0f, 16, 6), static_cast<size_t>((16 + 1) * 6 + 2 * (6 + 1)),
         static_cast<size_t>(2 * 16 * 6 + 2 * 6)},
    };
    int bad = 0;
    std::string obs;
    for (const Case& c : cases) {
        const bool ok = c.m.vertexCount() == c.V && c.m.triangleCount() == c.T;
        bad += !ok;
        if (!ok) obs += fmt("%s V %zu T %zu (want %zu %zu); ", c.name, c.m.vertexCount(), c.m.triangleCount(), c.V, c.T);
    }
    const MeshData disc = createTerrainDisc(3, 8);
    const bool discOk = disc.vertexCount() == 1 + 3 * 8 && disc.triangleCount() <= static_cast<size_t>(8 * (2 * 3 - 1));
    record("T447", "Primitive cards: the real generators at low resolution follow the library's V / T formulas",
           "sphere 26 / 48, hemisphere 26 / 32, cylinder 34 / 32, frustum 16 / 16, cone 25 / 16, torus 48 / 96, capsule 34 / 64, "
           "annulus 32 / 32, helix 116 / 204, terrain disc V = 1 + k s",
           obs.empty() ? fmt("all %zu match; terrain disc V %zu T %zu", cases.size(), disc.vertexCount(), disc.triangleCount()) : obs,
           bad == 0 && discOk);
}

}  // namespace

TS_TEST_GROUP(Report) {
    testDesigns();
    testColorKey();
    testLowres();
}
