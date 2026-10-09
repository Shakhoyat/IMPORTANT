// v9 M6/M7 offline asset baker:  build/bake_assets CACHE_DIR ASSETS_DIR [name ...]
//
// Turns the full-resolution CC0 originals downloaded by tools/fetch_assets.py (Poly Haven glTF models, ambientCG
// materials) into the game-ready files under assets/ that the renderer loads at start-up:
//   * foliage cards (twigs, leaves, blades) are first simplified per card (meshoptimizer: a subdivided flat card
//     collapses to a few triangles at ~zero error), then thinned to the asset's triangle budget (FoliageLod.h:
//     a deterministic subset of the cards, each grown about its anchor so the crown keeps its coverage);
//     fir_tree_01 has 4.1 M twig triangles per tree in the original (480 MB), the baked tree ~0.14 M;
//   * solid parts (trunks, branches, rocks) are simplified with meshoptimizer within a small error bound;
//   * wind weights + crown occlusion are written to COLOR_0 (FoliageLod.h computeWindColors);
//   * the separate JPG alpha masks are merged into RGBA PNG albedos (colour dilated into the transparent texels);
//     other textures are re-encoded as JPG (quality 90);
//   * ambientCG terrain maps are re-encoded (assets/terrain/<Name>/).
// It also writes ASSETS_DIR/bake_summary.json (triangle counts before / after), read by the workbook.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "assets/FoliageLod.h"
#include "assets/Image.h"
#include "assets/Model.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

using namespace ts::asset;

namespace {

struct Spec {
    const char* name;
    size_t foliageBudget;   // foliage-card triangles of the largest variant after baking (0 = keep all)
    float cardSimplify;     // per-card simplification target ratio (1 = off)
    float solidRatio;       // solid-part simplification ratio (1 = off)
    Anchor anchor;
    int splitX;             // > 0: the source has this many objects in one mesh, lined up along x: split them
    int texSize;            // max texture size
    float growth = 0.5f;    // kept cards grow by keep^-growth (0.5 keeps the card area; more fills the crown out)
};

// Budgets: the camp / treeline trees are seen from 5-60 m at 1080p; LOD 1 / LOD 2 / impostor come at run time.
const Spec kSpecs[] = {
    {"fir_tree_01", 420000, 0.45f, 0.5f, Anchor::TrunkAxis, 0, 1024, 0.72f},
    {"pine_tree_01", 420000, 0.45f, 0.5f, Anchor::TrunkAxis, 0, 1024, 0.72f},
    {"fir_sapling_medium", 160000, 0.45f, 0.6f, Anchor::TrunkAxis, 0, 1024, 0.6f},
    {"pine_sapling_small", 80000, 0.45f, 1.0f, Anchor::TrunkAxis, 0, 1024},
    {"tree_small_02", 260000, 0.30f, 0.5f, Anchor::TrunkAxis, 0, 1024},
    {"shrub_01", 40000, 0.40f, 1.0f, Anchor::Lowest, 5, 1024},
    {"shrub_02", 0, 0.6f, 1.0f, Anchor::Lowest, 0, 1024},
    {"shrub_03", 0, 1.0f, 1.0f, Anchor::Lowest, 0, 1024},
    {"shrub_04", 0, 0.6f, 1.0f, Anchor::Lowest, 4, 1024},
    {"fern_02", 0, 0.6f, 1.0f, Anchor::Lowest, 0, 1024},
    {"grass_medium_01", 0, 1.0f, 1.0f, Anchor::Lowest, 0, 1024},
    {"grass_medium_02", 0, 1.0f, 1.0f, Anchor::Lowest, 0, 1024},
    {"boulder_01", 0, 1.0f, 0.18f, Anchor::Lowest, 0, 1024},
    {"rock_moss_set_01", 0, 1.0f, 0.4f, Anchor::Lowest, 0, 1024},
    {"rock_moss_set_02", 0, 1.0f, 0.4f, Anchor::Lowest, 0, 1024},
};
const char* kTerrainSets[] = {"Grass004", "Ground048", "Rock051", "Gravel040", "Ground037"};

bool makeDir(const std::string& d) {
    std::string cur;
    for (size_t i = 0; i <= d.size(); ++i) {
        if (i == d.size() || d[i] == '/') {
            if (!cur.empty()) {
#ifdef _WIN32
                mkdir(cur.c_str());
#else
                mkdir(cur.c_str(), 0755);
#endif
            }
        }
        if (i < d.size()) cur += d[i];
    }
    return true;
}

std::string baseName(const std::string& p) {
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}
std::string stem(const std::string& p) {
    const std::string b = baseName(p);
    const size_t d = b.find_last_of('.');
    return d == std::string::npos ? b : b.substr(0, d);
}

bool writeJpg(const std::string& path, const Image& img, int quality) {
    std::vector<uint8_t> rgb(static_cast<size_t>(img.w) * img.h * 3);
    for (size_t i = 0; i < static_cast<size_t>(img.w) * img.h; ++i)
        for (int c = 0; c < 3; ++c) rgb[3 * i + c] = img.rgba[4 * i + c];
    return stbi_write_jpg(path.c_str(), img.w, img.h, 3, rgb.data(), quality) != 0;
}

Image capSize(const Image& img, int maxSize) {
    if (img.w <= maxSize && img.h <= maxSize) return img;
    const float k = static_cast<float>(maxSize) / std::max(img.w, img.h);
    return resizeBox(img, static_cast<int>(img.w * k), static_cast<int>(img.h * k));
}

size_t fileSize(const std::string& p) {
    struct stat st {};
    return stat(p.c_str(), &st) == 0 ? static_cast<size_t>(st.st_size) : 0;
}

// Splits a variant whose mesh holds several objects lined up along x (gaps between them) into 'n' variants.
std::vector<Variant> splitAlongX(const Variant& v, int n) {
    // Component centres along x, then the n - 1 widest gaps between consecutive (sorted) centres.
    std::vector<std::pair<float, int>> centres;   // x, part index * 2^20 + component
    struct Comp {
        int part;
        uint32_t id;
        float x;
    };
    std::vector<Comp> comps;
    std::vector<std::vector<uint32_t>> triComp(v.parts.size());
    for (size_t pi = 0; pi < v.parts.size(); ++pi) {
        const MeshPart& p = v.parts[pi];
        uint32_t count = 0;
        triComp[pi] = triangleComponents(p.idx, p.vertexCount(), &count);
        std::vector<float> lo(count, 1e30f), hi(count, -1e30f);
        for (size_t t = 0; t < triComp[pi].size(); ++t)
            for (int k = 0; k < 3; ++k) {
                const float x = p.pos[3 * p.idx[3 * t + k]];
                lo[triComp[pi][t]] = std::min(lo[triComp[pi][t]], x);
                hi[triComp[pi][t]] = std::max(hi[triComp[pi][t]], x);
            }
        for (uint32_t c = 0; c < count; ++c) comps.push_back({static_cast<int>(pi), c, 0.5f * (lo[c] + hi[c])});
    }
    std::vector<float> xs;
    for (const Comp& c : comps) xs.push_back(c.x);
    std::sort(xs.begin(), xs.end());
    std::vector<std::pair<float, float>> gaps;   // size, split position
    for (size_t i = 1; i < xs.size(); ++i) gaps.push_back({xs[i] - xs[i - 1], 0.5f * (xs[i] + xs[i - 1])});
    std::sort(gaps.begin(), gaps.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<float> cuts;
    for (int i = 0; i < n - 1 && i < static_cast<int>(gaps.size()); ++i) cuts.push_back(gaps[i].second);
    std::sort(cuts.begin(), cuts.end());
    auto bucketOf = [&](float x) { return static_cast<int>(std::upper_bound(cuts.begin(), cuts.end(), x) - cuts.begin()); };
    std::map<std::pair<int, uint32_t>, int> compBucket;
    for (const Comp& c : comps) compBucket[{c.part, c.id}] = bucketOf(c.x);
    std::vector<Variant> out(cuts.size() + 1);
    for (size_t b = 0; b < out.size(); ++b) {
        out[b].name = v.name + "_" + static_cast<char>('a' + b);
        for (size_t pi = 0; pi < v.parts.size(); ++pi) {
            const MeshPart& p = v.parts[pi];
            MeshPart q = p;
            q.idx.clear();
            for (size_t t = 0; t < triComp[pi].size(); ++t)
                if (compBucket[{static_cast<int>(pi), triComp[pi][t]}] == static_cast<int>(b))
                    q.idx.insert(q.idx.end(), &p.idx[3 * t], &p.idx[3 * t] + 3);
            compactPart(q);
            if (!q.idx.empty()) out[b].parts.push_back(std::move(q));
        }
        // recentre on the object's own footprint
        float cx = 0, cz = 0;
        size_t n2 = 0;
        for (const MeshPart& q : out[b].parts)
            for (size_t i = 0; i < q.vertexCount(); ++i) {
                cx += q.pos[3 * i];
                cz += q.pos[3 * i + 2];
                ++n2;
            }
        if (n2) {
            cx /= n2;
            cz /= n2;
        }
        float minY = 1e30f;
        for (const MeshPart& q : out[b].parts)
            for (size_t i = 0; i < q.vertexCount(); ++i) minY = std::min(minY, q.pos[3 * i + 1]);
        for (MeshPart& q : out[b].parts)
            for (size_t i = 0; i < q.vertexCount(); ++i) {
                q.pos[3 * i] -= cx;
                q.pos[3 * i + 2] -= cz;
                q.pos[3 * i + 1] -= minY;
            }
        out[b].computeBounds();
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const Variant& x) { return x.parts.empty(); }), out.end());
    return out;
}

std::string bakeTexture(const std::string& srcDir, const std::string& rel, const std::string& alphaRel, const std::string& outDir,
                        int maxSize, bool foliage, size_t& bytes) {
    if (rel.empty()) return "";
    Image img;
    if (!loadImage(srcDir + "/" + rel, img)) {
        std::fprintf(stderr, "  cannot read %s\n", rel.c_str());
        return "";
    }
    img = capSize(img, maxSize);
    makeDir(outDir + "/textures");
    if (foliage && !alphaRel.empty()) {
        Image mask;
        if (loadImage(srcDir + "/" + alphaRel, mask)) {
            applyAlphaMask(img, mask);
            dilateColour(img, 24);
            const std::string out = "textures/" + stem(rel) + ".png";
            stbi_write_png_compression_level = 9;
            stbi_write_png((outDir + "/" + out).c_str(), img.w, img.h, 4, img.rgba.data(), img.w * 4);
            bytes += fileSize(outDir + "/" + out);
            return out;
        }
    }
    const std::string out = "textures/" + stem(rel) + ".jpg";
    writeJpg(outDir + "/" + out, img, 90);
    bytes += fileSize(outDir + "/" + out);
    return out;
}

std::string alphaFor(const std::string& dir, const std::string& diff) {
    // fir_tree_01_twig_diff_1k.jpg -> fir_tree_01_twig_alpha_1k.jpg ; shrub_01_diff_1k.jpg -> shrub_01_alpha_1k.jpg
    std::string a = diff;
    const size_t p = a.find("_diff_");
    if (p == std::string::npos) return "";
    a.replace(p, 6, "_alpha_");
    FILE* f = std::fopen((dir + "/" + a).c_str(), "rb");
    if (!f) return "";
    std::fclose(f);
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s CACHE_DIR ASSETS_DIR [name ...]\n", argv[0]);
        return 2;
    }
    const std::string cache = argv[1], assets = argv[2];
    std::vector<std::string> only(argv + 3, argv + argc);
    auto wanted = [&](const std::string& n) { return only.empty() || std::find(only.begin(), only.end(), n) != only.end(); };
    std::ostringstream summary;
    summary << "{\n  \"models\": [\n";
    bool firstModel = true;
    int failures = 0;
    for (const Spec& spec : kSpecs) {
        if (!wanted(spec.name)) continue;
        const auto t0 = std::chrono::steady_clock::now();
        const std::string src = cache + "/polyhaven/" + spec.name + "/" + spec.name + ".gltf";
        Model m;
        std::string err;
        if (!loadGltf(src, m, &err)) {
            std::fprintf(stderr, "%s: %s\n", spec.name, err.c_str());
            ++failures;
            continue;
        }
        const size_t trisIn = m.triangles();
        // A material whose albedo has a separate alpha mask is a card material even when the glTF says OPAQUE
        // (pine_tree_01's twigs): it is alpha-tested.
        for (MaterialInfo& mi : m.materials)
            if (mi.alphaMode == AlphaMode::Opaque && !alphaFor(m.dir, mi.baseColor).empty()) mi.alphaMode = AlphaMode::Mask;
        if (spec.splitX > 1 && m.variants.size() == 1) m.variants = splitAlongX(m.variants[0], spec.splitX);
        std::vector<bool> foliage;
        for (const MaterialInfo& mi : m.materials) foliage.push_back(isFoliageMaterial(mi));
        // 1) per-card simplification of the foliage, solid simplification
        for (Variant& v : m.variants)
            for (MeshPart& p : v.parts) {
                const bool f = foliage[p.material];
                if (f && spec.cardSimplify < 1.0f) p = simplifyPart(p, spec.cardSimplify, 0.0015f);
                else if (!f && spec.solidRatio < 1.0f) p = simplifyPart(p, spec.solidRatio, spec.solidRatio < 0.3f ? 0.012f : 0.004f, nullptr, true);
            }
        // 2) card thinning to the budget (same fraction for every variant: the largest one sets it)
        size_t largest = 0;
        for (const Variant& v : m.variants) {
            size_t n = 0;
            for (const MeshPart& p : v.parts)
                if (foliage[p.material]) n += p.triangleCount();
            largest = std::max(largest, n);
        }
        const float keep = spec.foliageBudget > 0 && largest > spec.foliageBudget ? static_cast<float>(spec.foliageBudget) / largest : 1.0f;
        float grow = 1.0f;
        for (Variant& v : m.variants) {
            for (size_t pi = 0; pi < v.parts.size(); ++pi) {
                MeshPart& p = v.parts[pi];
                if (!foliage[p.material] || keep >= 1.0f) {
                    compactPart(p);
                    continue;
                }
                ThinStats st;
                p = thinCards(p, keep, spec.growth, 0xC0FFEEu + static_cast<uint32_t>(pi), spec.anchor, &st);
                compactPart(p);
                grow = st.scale;
            }
            computeWindColors(v, foliage, spec.anchor);
        }
        // 3) textures
        const std::string outDir = assets + "/vegetation/" + spec.name;
        makeDir(outDir);
        size_t texBytes = 0;
        for (size_t i = 0; i < m.materials.size(); ++i) {
            MaterialInfo& mi = m.materials[i];
            const std::string alpha = foliage[i] ? alphaFor(m.dir, mi.baseColor) : "";
            mi.baseColor = bakeTexture(m.dir, mi.baseColor, alpha, outDir, spec.texSize, foliage[i], texBytes);
            mi.normal = bakeTexture(m.dir, mi.normal, "", outDir, spec.texSize, false, texBytes);
            mi.arm = bakeTexture(m.dir, mi.arm, "", outDir, spec.texSize, false, texBytes);
            if (mi.alphaMode == AlphaMode::Blend) mi.alphaMode = AlphaMode::Mask;   // rendered alpha-tested / alpha-to-coverage
            if (mi.alphaMode == AlphaMode::Mask) mi.alphaCutoff = 0.5f;
        }
        const std::string outGltf = outDir + "/" + spec.name + ".gltf";
        if (!writeGltf(outGltf, m, &err)) {
            std::fprintf(stderr, "%s: %s\n", spec.name, err.c_str());
            ++failures;
            continue;
        }
        const size_t binBytes = fileSize(outDir + "/" + spec.name + ".bin");
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-20s %zu variants, %9zu -> %7zu triangles (cards kept %.3f, grown x%.2f), bin %.1f MB, textures %.1f MB, %.1f s\n",
                    spec.name, m.variants.size(), trisIn, m.triangles(), keep, grow, binBytes / 1e6, texBytes / 1e6, secs);
        std::fflush(stdout);
        summary << (firstModel ? "" : ",\n") << "    {\"name\": \"" << spec.name << "\", \"variants\": " << m.variants.size()
                << ", \"source_triangles\": " << trisIn << ", \"baked_triangles\": " << m.triangles() << ", \"card_keep\": " << keep
                << ", \"card_growth\": " << grow << ", \"bin_bytes\": " << binBytes << ", \"texture_bytes\": " << texBytes
                << ", \"variant_triangles\": [";
        for (size_t i = 0; i < m.variants.size(); ++i) summary << (i ? ", " : "") << m.variants[i].triangles();
        summary << "], \"variant_height\": [";
        for (size_t i = 0; i < m.variants.size(); ++i) summary << (i ? ", " : "") << (m.variants[i].maxY - m.variants[i].minY);
        summary << "]}";
        firstModel = false;
    }
    summary << "\n  ],\n  \"terrain\": [\n";
    bool firstSet = true;
    for (const char* set : kTerrainSets) {
        if (!wanted(set) && !(only.size() == 1 && only[0] == "terrain")) {
            if (!only.empty()) continue;
        }
        const std::string dir = cache + "/ambientcg/" + set, outDir = assets + "/terrain/" + set;
        makeDir(outDir);
        size_t bytes = 0;
        int maps = 0;
        for (const char* suffix : {"Color", "NormalGL", "Displacement", "Roughness", "AmbientOcclusion"}) {
            Image img;
            const std::string in = dir + "/" + set + "_1K-JPG_" + suffix + ".jpg";
            if (!loadImage(in, img)) continue;
            const std::string out = outDir + "/" + set + "_" + suffix + ".jpg";
            writeJpg(out, img, 90);
            bytes += fileSize(out);
            ++maps;
        }
        std::printf("terrain %-10s %d maps, %.1f MB\n", set, maps, bytes / 1e6);
        summary << (firstSet ? "" : ",\n") << "    {\"name\": \"" << set << "\", \"maps\": " << maps << ", \"bytes\": " << bytes << "}";
        firstSet = false;
        if (maps == 0) ++failures;
    }
    summary << "\n  ]\n}\n";
    if (only.empty()) {
        std::ofstream(assets + "/bake_summary.json") << summary.str();
    }
    return failures ? 1 : 0;
}
