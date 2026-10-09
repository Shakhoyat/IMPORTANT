// v9 M6 (asset vegetation): T370-T375. CPU only. (T376-T379 are reserved for M7.)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "TestKit.h"
#include "assets/FoliageLod.h"
#include "assets/Image.h"
#include "assets/Impostor.h"
#include "assets/Model.h"
#include "objects/Backdrop.h"
#include "objects/VegetationLayout.h"
#include "scene/Scene.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

std::string base64(const std::vector<unsigned char>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const unsigned v = in[i] << 16;
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = (in[i] << 16) | (in[i + 1] << 8);
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += t[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

template <typename T>
void append(std::vector<unsigned char>& b, const std::vector<T>& v) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(v.data());
    b.insert(b.end(), p, p + v.size() * sizeof(T));
}

// n separate unit cards (quads of 2 triangles) on a 10 x 10 grid; card k at x = k % 10, z = k / 10, y = 1.
asset::MeshPart cards(int n) {
    asset::MeshPart p;
    for (int k = 0; k < n; ++k) {
        const float x = static_cast<float>(k % 10) * 2.0f, z = static_cast<float>(k / 10) * 2.0f;
        const uint32_t b = static_cast<uint32_t>(p.vertexCount());
        const float c[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (const auto& q : c) {
            p.pos.insert(p.pos.end(), {x + q[0], 1.0f + q[1], z});
            p.nrm.insert(p.nrm.end(), {0.0f, 0.0f, 1.0f});
            p.uv.insert(p.uv.end(), {q[0], q[1]});
        }
        p.idx.insert(p.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
    }
    return p;
}

double area(const asset::MeshPart& p) {
    double a = 0.0;
    for (size_t t = 0; t + 2 < p.idx.size(); t += 3) {
        const float* A = &p.pos[3 * p.idx[t]];
        const float* B = &p.pos[3 * p.idx[t + 1]];
        const float* C = &p.pos[3 * p.idx[t + 2]];
        const double u[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]}, v[3] = {C[0] - A[0], C[1] - A[1], C[2] - A[2]};
        const double x = u[1] * v[2] - u[2] * v[1], y = u[2] * v[0] - u[0] * v[2], z = u[0] * v[1] - u[1] * v[0];
        a += 0.5 * std::sqrt(x * x + y * y + z * z);
    }
    return a;
}

// A finely tessellated bumpy dome (solid part) of (n+1)^2 vertices.
asset::MeshPart dome(int n) {
    asset::MeshPart p;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float u = static_cast<float>(i) / n, v = static_cast<float>(j) / n;
            const float x = 2.0f * u - 1.0f, z = 2.0f * v - 1.0f;
            const float y = 1.0f - 0.5f * (x * x + z * z) + 0.002f * std::sin(40.0f * x) * std::sin(37.0f * z);
            p.pos.insert(p.pos.end(), {x, y, z});
            p.nrm.insert(p.nrm.end(), {x * 0.5f, 1.0f, z * 0.5f});
            p.uv.insert(p.uv.end(), {u, v});
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * (n + 1) + i), b = a + 1, c = a + static_cast<uint32_t>(n + 1), d = c + 1;
            p.idx.insert(p.idx.end(), {a, c, b, b, c, d});
        }
    return p;
}

}  // namespace

TS_TEST_GROUP(V9M6) {
    // ---- T370: glTF loader (cgltf) on a tiny embedded glTF: variants, recentring, materials, write / read round trip ---
    {
        // Two nodes (variants) with one triangle each, the second offset by x = 6 (Poly Haven lines variants up like this).
        std::vector<float> pos = {0, 0, 0, 1, 0, 0, 0, 2, 0};
        std::vector<float> nrm = {0, 0, 1, 0, 0, 1, 0, 0, 1};
        std::vector<float> uv = {0, 0, 1, 0, 0, 1};
        std::vector<uint16_t> idx = {0, 1, 2, 0};   // 3 indices + padding
        std::vector<unsigned char> bin;
        append(bin, pos);
        append(bin, nrm);
        append(bin, uv);
        append(bin, idx);
        const std::string json = std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,1]}],
          "nodes":[{"name":"a","mesh":0},{"name":"b","mesh":0,"translation":[6,0,0]}],
          "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
          "materials":[{"name":"leaf","alphaMode":"MASK","alphaCutoff":0.4,"doubleSided":true,
             "pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
          "textures":[{"source":0}],"images":[{"uri":"textures/leaf.png"}],
          "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,2,0]},
                       {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
                       {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
                       {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
          "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
                         {"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],
          "buffers":[{"byteLength":)") + std::to_string(bin.size()) + R"(,"uri":"data:application/octet-stream;base64,)" +
                                 base64(bin) + R"("}]})";
        asset::Model m;
        std::string err;
        const bool ok = asset::loadGltfMemory(json, m, &err);
        bool shape = ok && m.variants.size() == 2 && m.materials.size() == 1 && m.triangles() == 2;
        // variant b is recentred: its first vertex at x = 0 again (the node's translation removed)
        shape = shape && std::fabs(m.variants[1].parts[0].pos[0]) < 1e-6f && std::fabs(m.variants[1].maxY - 2.0f) < 1e-6f;
        const bool mat = ok && m.materials[0].alphaMode == asset::AlphaMode::Mask && std::fabs(m.materials[0].alphaCutoff - 0.4f) < 1e-6f &&
                         m.materials[0].doubleSided && m.materials[0].baseColor == "textures/leaf.png";
        // write (baker path) and read back
        bool round = false;
        if (ok) {
            asset::computeWindColors(m.variants[0], {true}, asset::Anchor::Lowest);
            const std::string path = "build/t370_roundtrip.gltf";
            asset::Model back;
            round = asset::writeGltf(path, m) && asset::loadGltf(path, back, &err) && back.variants.size() == 2 &&
                    back.triangles() == 2 && back.variants[0].parts[0].color.size() == 12 && back.materials[0].alphaMode == asset::AlphaMode::Mask;
            std::remove(path.c_str());
            std::remove("build/t370_roundtrip.bin");
        }
        record("T370", "glTF loader (cgltf) on an embedded glTF: two node variants recentred, MASK material, write/read round trip",
               "2 variants, 2 triangles, variant b at x = 0, alphaMode MASK 0.4, round trip keeps COLOR_0",
               fmt("ok %d variants %zu tris %zu shape %d material %d roundtrip %d %s", ok, m.variants.size(), m.triangles(), shape, mat,
                   round, err.c_str()),
               ok && shape && mat && round);
    }

    // ---- T371: card thinning: connected components, deterministic subset, area compensation -------------------------
    {
        const asset::MeshPart in = cards(400);
        uint32_t comps = 0;
        asset::triangleComponents(in.idx, in.vertexCount(), &comps);
        asset::ThinStats s25, s10;
        const asset::MeshPart a = asset::thinCards(in, 0.25f, 0.5f, 7u, asset::Anchor::Lowest, &s25);
        const asset::MeshPart b = asset::thinCards(in, 0.10f, 0.5f, 7u, asset::Anchor::Lowest, &s10);
        // subset: every card kept at 10 % is kept at 25 % (identified by its anchor = lowest vertex, unchanged by growth)
        std::set<std::pair<int, int>> keptA;
        for (size_t v = 0; v < a.vertexCount(); v += 4) keptA.insert({static_cast<int>(std::lround(a.pos[3 * v])), static_cast<int>(std::lround(a.pos[3 * v + 2]))});
        bool subset = true;
        for (size_t v = 0; v < b.vertexCount(); v += 4)
            subset = subset && keptA.count({static_cast<int>(std::lround(b.pos[3 * v])), static_cast<int>(std::lround(b.pos[3 * v + 2]))});
        const double areaRatio = area(a) / area(in);
        const double frac = static_cast<double>(s25.cardsKept) / 400.0;
        record("T371", "Foliage LOD: cards are connected components; thinning keeps a deterministic subset and grows it to keep the area",
               "400 components; kept 25 % +- 6 %; 10 % subset of 25 %; area ratio 1 +- 0.25 (growth keep^-0.5)",
               fmt("components %u, kept %.3f / %.3f, subset %d, area ratio %.3f, growth %.2f", comps, frac,
                   static_cast<double>(s10.cardsKept) / 400.0, subset, areaRatio, s25.scale),
               comps == 400 && std::fabs(frac - 0.25) < 0.06 && subset && std::fabs(areaRatio - 1.0) < 0.25 && std::fabs(s25.scale - 2.0f) < 1e-3f);
    }

    // ---- T372: LOD chain (meshoptimizer): triangle reduction per level, solid error within the bound ------------------
    {
        asset::Variant v;
        v.name = "test";
        asset::MeshPart solid = dome(64);
        solid.material = 0;
        asset::MeshPart leaves = cards(200);
        leaves.material = 1;
        v.parts = {solid, leaves};
        v.computeBounds();
        std::vector<asset::MaterialInfo> mats(2);
        mats[1].alphaMode = asset::AlphaMode::Mask;
        asset::LodSettings s;
        std::vector<asset::LodInfo> info;
        const std::vector<asset::Variant> chain = asset::buildLodChain(v, mats, s, &info);
        const bool decreasing = chain.size() == 3 && info[0].triangles > info[1].triangles && info[1].triangles > info[2].triangles;
        // dome extent ~2 m: 3 % relative error bound -> <= 0.06 m (+ margin), LOD 2 at most ~25 % of LOD 0
        const bool bounded = info.size() == 3 && info[1].solidErrorMetres <= 0.01f * 3.0f + 1e-3f && info[2].solidErrorMetres <= 0.03f * 3.0f + 1e-3f;
        const bool reduced = info.size() == 3 && info[2].triangles * 4 <= info[0].triangles;
        record("T372", "LOD chain: meshoptimizer simplification of the solid + card thinning of the foliage, error bounded",
               "triangles LOD0 > LOD1 > LOD2, LOD2 <= 25 % of LOD0, solid error <= bound x extent",
               fmt("triangles %zu / %zu / %zu, solid error %.4f / %.4f m, card growth %.2f / %.2f", info.size() > 0 ? info[0].triangles : 0,
                   info.size() > 1 ? info[1].triangles : 0, info.size() > 2 ? info[2].triangles : 0, info.size() > 1 ? info[1].solidErrorMetres : -1.0f,
                   info.size() > 2 ? info[2].solidErrorMetres : -1.0f, info.size() > 1 ? info[1].cardScale : 0.0f, info.size() > 2 ? info[2].cardScale : 0.0f),
               decreasing && bounded && reduced);
    }

    // ---- T373: textures: coverage-preserving mips, alpha mask, blade rectangles of a grass atlas ----------------------
    {
        // 256 x 256 atlas: 6 thin vertical blades (2 px wide, 160 px tall) on transparent black.
        asset::Image img;
        img.w = img.h = 256;
        img.rgba.assign(256 * 256 * 4, 0);
        for (int b = 0; b < 6; ++b)
            for (int y = 40; y < 200; ++y)
                for (int x = 20 + 40 * b; x < 22 + 40 * b; ++x) {
                    uint8_t* p = img.at(x, y);
                    p[0] = 40;
                    p[1] = 120;
                    p[2] = 30;
                    p[3] = 255;
                }
        // Needle-like alpha: 30 % of the texels opaque at random. A box filter averages that to alpha ~0.3 (below the 0.5
        // cut-off): the plain mips lose the needles; the coverage-preserving mips keep about 30 % of the texels opaque.
        asset::Image needles;
        needles.w = needles.h = 256;
        needles.rgba.assign(256 * 256 * 4, 0);
        uint32_t h = 12345u;
        for (size_t i = 0; i < 256u * 256u; ++i) {
            h ^= h << 13;
            h ^= h >> 17;
            h ^= h << 5;
            needles.rgba[4 * i + 1] = 100;
            needles.rgba[4 * i + 3] = (h % 100u) < 30u ? 255 : 0;
        }
        const float c0 = asset::alphaCoverage(needles, 0.5f);
        const std::vector<asset::Image> mips = asset::buildMips(needles, 0.5f);
        asset::Image plain = needles;
        for (int i = 0; i < 3; ++i) plain = asset::downsample(plain);
        const float plainCov = asset::alphaCoverage(plain, 0.5f), keptCov = asset::alphaCoverage(mips[3], 0.5f);
        const std::vector<asset::BladeRect> rects = asset::findBladeRects(img, 256, 0.3f, 2.5f);
        bool rectsOk = rects.size() == 6;
        for (size_t i = 0; rectsOk && i < rects.size(); ++i)
            rectsOk = std::fabs(rects[i].h - 160.0f / 256.0f) < 0.01f && rects[i].w < 0.02f && std::fabs(rects[i].u - (20.0f + 40.0f * i) / 256.0f) < 0.01f;
        record("T373", "Alpha-tested foliage textures: coverage-preserving mip chain; grass atlas blade rectangles",
               "random 30 % needle alpha: mip 3 keeps the level-0 coverage within 35 % (a plain box filter loses it); 6 blade rectangles found",
               fmt("coverage L0 %.4f, mip3 preserved %.4f, mip3 plain %.4f, %zu mips, %zu blades", c0, keptCov, plainCov, mips.size(), rects.size()),
               std::fabs(keptCov - c0) < 0.35f * c0 && plainCov < 0.5f * c0 && mips.size() == 9 && rectsOk);
    }

    // ---- T374: octahedral impostors: hemi-octahedral encode / decode, view-cell selection and weights ------------------
    {
        float worst = 0.0f;
        int n = 0;
        for (int i = 0; i < 64; ++i)
            for (int j = 0; j < 64; ++j) {
                const asset::OctDir d = asset::hemiOctDecode((i + 0.5f) / 64.0f, (j + 0.5f) / 64.0f);
                float u, v;
                asset::hemiOctEncode(d.x, d.y, d.z, u, v);
                worst = std::max(worst, std::max(std::fabs(u - (i + 0.5f) / 64.0f), std::fabs(v - (j + 0.5f) / 64.0f)));
                ++n;
            }
        // For views along every cell's own direction the top tap is that cell with weight ~1; weights always sum to 1.
        bool selfPick = true;
        float sumErr = 0.0f;
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j) {
                const asset::OctDir d = asset::impostorCellDirection(i, j, 8);
                const asset::ImpostorTaps t = asset::impostorTaps(d.x, d.y, d.z, 8);
                float sum = 0.0f;
                int best = 0;
                for (int k = 0; k < 4; ++k) {
                    sum += t.weight[k];
                    if (t.weight[k] > t.weight[best]) best = k;
                }
                sumErr = std::max(sumErr, std::fabs(sum - 1.0f));
                selfPick = selfPick && t.cell[best][0] == i && t.cell[best][1] == j && t.weight[best] > 0.99f;
            }
        // A view from straight above uses the central cells; a horizontal view the rim of the map.
        const asset::ImpostorTaps top = asset::impostorTaps(0.0f, 1.0f, 0.0f, 8);
        const asset::OctDir side = asset::impostorCellDirection(0, 0, 8);
        const bool centre = top.cell[0][0] >= 3 && top.cell[3][0] <= 4 && top.cell[0][1] >= 3 && top.cell[3][1] <= 4;
        record("T374", "Octahedral impostors: hemi-octahedral map round trip, the 4-tap view selection picks the matching picture",
               "round trip < 1e-5 on 64x64, self pick weight > 0.99 on all 8x8 cells, weights sum 1, top view -> central cells",
               fmt("round trip %.2e (%d dirs), self pick %d, weight sum err %.1e, top cells (%d,%d)-(%d,%d), corner cell dir y %.3f", worst, n,
                   selfPick, sumErr, top.cell[0][0], top.cell[0][1], top.cell[3][0], top.cell[3][1], side.y),
               worst < 1e-5f && selfPick && sumErr < 1e-5f && centre && side.y < 0.3f);
    }

    // ---- T375: the scene's trees / bushes / boulders become asset instances; the gameplay model is unchanged ---------
    {
        Scene scene;
        std::vector<const SceneNode*> leaves(scene.leaves().begin(), scene.leaves().end());
        const VegLayout layout = extractVegetation(leaves);
        int byKind[static_cast<int>(VegKind::Count)] = {};
        bool heights = true;
        for (const VegInstance& v : layout.instances) {
            ++byKind[static_cast<int>(v.kind)];
            if (v.kind == VegKind::CampConifer) heights = heights && v.height > 2.5f && v.height < 5.0f;
            if (v.kind == VegKind::TallConifer) heights = heights && v.height > 9.0f && v.height < 17.0f;
        }
        // every replaced leaf keeps its gameplay flags (trees: occluders), and all of them belong to vegetation groups
        int occluders = 0, foreign = 0;
        for (const SceneNode* n : layout.replaced) {
            occluders += n->occluder ? 1 : 0;
            const std::string& g = n->parent ? n->parent->name : std::string();
            foreign += (g == "Pine" || g == "Tree" || g == "Bush" || g == "Boulder" || g == "Treeline pine") ? 0 : 1;
        }
        int sceneTreeLeaves = 0;
        for (const SceneNode* n : leaves)
            if (n->object != "Backdrop" && (n->name == "Pine trunk" || n->name == "Trunk")) ++sceneTreeLeaves;
        const int treeline = static_cast<int>(treelinePines().size());
        record("T375", "Asset vegetation layout: one instance per scene tree / bush / boulder / treeline pine; primitives keep their flags",
               "camp trees = trunk leaves, treeline = treelinePines(), heights in range, replaced leaves only from vegetation groups",
               fmt("camp conifers %d + broadleaf %d (trunk leaves %d), bushes %d, rocks %d, treeline %d (%d), replaced %zu (occluders %d, foreign %d)",
                   byKind[0], byKind[1], sceneTreeLeaves, byKind[2], byKind[3], byKind[4], treeline, layout.replaced.size(), occluders, foreign),
               byKind[0] + byKind[1] == sceneTreeLeaves && byKind[4] == treeline && byKind[2] > 0 && byKind[3] > 0 && heights && foreign == 0 &&
                   occluders > 0);
    }
}
