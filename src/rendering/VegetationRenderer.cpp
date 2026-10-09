#include "rendering/VegetationRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "assets/AssetPaths.h"
#include "assets/FoliageLod.h"
#include "assets/Image.h"
#include "assets/Impostor.h"
#include "assets/Model.h"
#include "rendering/ImageWriter.h"
#include "rendering/ShaderSources.h"
#include "rendering/ShadersVegetation.h"

namespace ts {

namespace {

// What each kind of vegetation is made of, how much it moves in the wind and how it is tinted.
struct Species {
    const char* asset;
    VegKind kind;
    int maxVariants;
    bool impostor;
    float sway, branch, flutter;   // wind: trunk tip displacement / height, branch swing (m), flutter (m)
    float albedoScale;
    Vec3 tintA, tintB;             // per-instance tint range
    float transmission;
    asset::Anchor anchor;
};

const Species kSpecies[] = {
    {"fir_sapling_medium", VegKind::CampConifer, 3, true, 0.012f, 0.05f, 0.015f, 0.95f, {0.92f, 1.0f, 0.95f}, {1.05f, 1.02f, 0.86f}, 0.7f, asset::Anchor::TrunkAxis},
    {"tree_small_02", VegKind::Broadleaf, 1, true, 0.010f, 0.07f, 0.03f, 0.95f, {0.95f, 1.0f, 0.9f}, {1.08f, 1.02f, 0.78f}, 1.0f, asset::Anchor::TrunkAxis},
    {"shrub_01", VegKind::Bush, 5, false, 0.03f, 0.04f, 0.02f, 0.95f, {0.9f, 1.0f, 0.9f}, {1.08f, 1.0f, 0.8f}, 1.0f, asset::Anchor::Lowest},
    {"shrub_02", VegKind::Bush, 4, false, 0.03f, 0.04f, 0.02f, 0.9f, {0.9f, 1.0f, 0.9f}, {1.05f, 1.0f, 0.85f}, 0.8f, asset::Anchor::Lowest},
    {"boulder_01", VegKind::Rock, 1, false, 0, 0, 0, 1.0f, {0.95f, 0.95f, 0.95f}, {1.05f, 1.03f, 1.0f}, 0.0f, asset::Anchor::Lowest},
    {"rock_moss_set_01", VegKind::Rock, 6, false, 0, 0, 0, 1.0f, {0.95f, 0.95f, 0.95f}, {1.05f, 1.03f, 1.0f}, 0.0f, asset::Anchor::Lowest},
    {"rock_moss_set_02", VegKind::Rock, 6, false, 0, 0, 0, 1.0f, {0.95f, 0.95f, 0.95f}, {1.05f, 1.03f, 1.0f}, 0.0f, asset::Anchor::Lowest},
    {"fir_tree_01", VegKind::TallConifer, 3, true, 0.010f, 0.08f, 0.02f, 0.95f, {0.92f, 1.0f, 0.95f}, {1.04f, 1.02f, 0.88f}, 0.7f, asset::Anchor::TrunkAxis},
    {"pine_tree_01", VegKind::TallConifer, 3, true, 0.010f, 0.08f, 0.02f, 0.95f, {0.92f, 1.0f, 0.95f}, {1.04f, 1.02f, 0.88f}, 0.7f, asset::Anchor::TrunkAxis},
    {"pine_sapling_small", VegKind::YoungConifer, 3, true, 0.02f, 0.03f, 0.012f, 0.95f, {0.92f, 1.0f, 0.95f}, {1.05f, 1.02f, 0.85f}, 0.7f, asset::Anchor::TrunkAxis},
    {"fern_02", VegKind::GroundCover, 4, false, 0.02f, 0.03f, 0.02f, 0.95f, {0.92f, 1.0f, 0.92f}, {1.06f, 1.0f, 0.82f}, 1.0f, asset::Anchor::Lowest},
    {"shrub_04", VegKind::GroundCover, 4, false, 0.02f, 0.02f, 0.015f, 0.95f, {0.92f, 1.0f, 0.92f}, {1.06f, 1.0f, 0.82f}, 1.0f, asset::Anchor::Lowest},
    {"shrub_03", VegKind::GroundCover, 4, false, 0.02f, 0.03f, 0.015f, 0.95f, {0.92f, 1.0f, 0.92f}, {1.06f, 1.0f, 0.82f}, 1.0f, asset::Anchor::Lowest},
    {"grass_medium_01", VegKind::GroundCover, 8, false, 0.03f, 0.02f, 0.02f, 0.9f, {0.9f, 1.0f, 0.9f}, {1.1f, 1.0f, 0.75f}, 1.0f, asset::Anchor::Lowest},
};

constexpr float kCellSize = 16.0f;
constexpr int kImpGrid = 8, kImpCell = 160, kImpSuper = 2;
// Projected height (pixels) where a level hands over to the next (LOD 0 -> 1 -> 2 -> impostor); +-kFadeBand cross-fade.
constexpr float kLodPixels[3] = {560.0f, 280.0f, 150.0f};
constexpr float kFadeBand = 0.12f;

const Species* speciesOf(const std::string& asset) {
    for (const Species& s : kSpecies)
        if (asset == s.asset) return &s;
    return nullptr;
}

float smooth01(float a, float b, float x) {
    const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

GLuint uploadTexture(const asset::Image& img, float alphaCutoff, bool srgbColour) {
    (void)srgbColour;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (alphaCutoff > 0.0f) {
        // Coverage-preserving mip chain (assets/Image.h): thin needles and blades keep their density at a distance.
        const std::vector<asset::Image> mips = asset::buildMips(img, alphaCutoff);
        for (size_t l = 0; l < mips.size(); ++l)
            glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), GL_RGBA8, mips[l].w, mips[l].h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mips[l].rgba.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLfloat maxAniso = 0.0f;
    glGetFloatv(0x84FF, &maxAniso);   // GL_MAX_TEXTURE_MAX_ANISOTROPY (core 4.6 / EXT_texture_filter_anisotropic)
    if (glGetError() == GL_NO_ERROR && maxAniso > 1.0f) glTexParameterf(GL_TEXTURE_2D, 0x84FE, std::min(8.0f, maxAniso));
    while (glGetError() != GL_NO_ERROR) {
    }
    return tex;
}

GLuint flatTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    asset::Image img;
    img.w = img.h = 1;
    img.rgba = {r, g, b, a};
    return uploadTexture(img, 0.0f, false);
}

// Premultiplied 2x2 box (impostor atlas super-sampling and mips): colour weighted by coverage, alpha averaged.
asset::Image downPremul(const asset::Image& s) {
    asset::Image d;
    d.w = std::max(1, s.w / 2);
    d.h = std::max(1, s.h / 2);
    d.rgba.resize(static_cast<size_t>(d.w) * d.h * 4);
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            int acc[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const uint8_t* p = s.at(std::min(2 * x + (k & 1), s.w - 1), std::min(2 * y + (k >> 1), s.h - 1));
                for (int c = 0; c < 4; ++c) acc[c] += p[c];
            }
            uint8_t* o = d.at(x, y);
            for (int c = 0; c < 4; ++c) o[c] = static_cast<uint8_t>((acc[c] + 2) / 4);
        }
    return d;
}

// 'scales' (in/out): with preserve, the per-level coverage scales are computed and returned; without, the given scales
// are applied (so the normal atlas stays premultiplied by exactly the albedo atlas' coverage at every level).
GLuint uploadPremulMips(asset::Image level0, bool preserve, std::vector<float>& scales) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const float target = preserve ? asset::alphaCoverage(level0, 0.35f) : 0.0f;
    int level = 0;
    asset::Image cur = std::move(level0);
    while (true) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, cur.w, cur.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cur.rgba.data());
        if (cur.w == 1 && cur.h == 1) break;
        asset::Image next = downPremul(cur);
        {
            // scale alpha and the premultiplied colour together: the colour (rgb / a) is unchanged
            float k = 1.0f;
            if (preserve) {
                asset::Image probe = next;
                k = asset::preserveCoverage(probe, 0.35f, target);
                scales.push_back(k);
            } else if (static_cast<size_t>(level) < scales.size()) {
                k = scales[static_cast<size_t>(level)];
            }
            for (size_t i = 0; i < next.rgba.size(); ++i) next.rgba[i] = static_cast<uint8_t>(std::min(255.0f, next.rgba[i] * k + 0.5f));
        }
        cur = std::move(next);
        ++level;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

}  // namespace

VegetationRenderer::~VegetationRenderer() {
    for (Archetype& a : archetypes_) {
        for (Lod& l : a.lod) {
            if (l.vao) glDeleteVertexArrays(1, &l.vao);
            if (l.vbo) glDeleteBuffers(1, &l.vbo);
            if (l.ebo) glDeleteBuffers(1, &l.ebo);
        }
        if (a.imp.albedo) glDeleteTextures(1, &a.imp.albedo);
        if (a.imp.normal) glDeleteTextures(1, &a.imp.normal);
    }
    std::vector<GLuint> texs;
    for (const Material& m : materials_) texs.insert(texs.end(), {m.albedo, m.normal, m.arm});
    std::sort(texs.begin(), texs.end());
    texs.erase(std::unique(texs.begin(), texs.end()), texs.end());
    for (GLuint t : texs)
        if (t) glDeleteTextures(1, &t);
    if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
    if (quadVao_) glDeleteVertexArrays(1, &quadVao_);
}

bool VegetationRenderer::load() {
    if (attempted_) return ready_;
    attempted_ = true;
    const auto t0 = std::chrono::steady_clock::now();
    const std::string root = asset::findAssetRoot("vegetation/fir_sapling_medium/fir_sapling_medium.gltf");
    if (root.empty()) {
        std::printf("Vegetation assets: assets/vegetation not found -> procedural v8 trees\n");
        return false;
    }
    using namespace glsl::veg;
    const std::string v330 = "#version 330 core\n";
    mesh_ = std::make_unique<Shader>();
    depth_ = std::make_unique<Shader>();
    prepass_ = std::make_unique<Shader>();
    impostor_ = std::make_unique<Shader>();
    impostorPrepass_ = std::make_unique<Shader>();
    bake_ = std::make_unique<Shader>();
    bool ok = mesh_->build("vegetation", v330 + kWindCommon + kMeshVert,
                           glsl::withLightingAndOutput((std::string(kFoliageLight) + kMeshFrag).c_str()));
    ok = ok && depth_->build("vegetation-depth", v330 + kWindCommon + kDepthVert, v330 + kDepthFrag);
    ok = ok && prepass_->build("vegetation-prepass", v330 + kWindCommon + kPrepassVert, v330 + kPrepassFrag);
    ok = ok && impostor_->build("vegetation-impostor", v330 + "uniform vec3 uCamPos;\n" + kImpostorVert,
                                glsl::withLightingAndOutput((std::string(kFoliageLight) + kImpostorFrag).c_str()));
    ok = ok && impostorPrepass_->build("vegetation-impostor-prepass", v330 + "uniform vec3 uCamPos;\n" + kImpostorVert, kImpostorPrepassFrag);
    ok = ok && bake_->build("vegetation-bake", kBakeVert, kBakeFrag);
    if (!ok) {
        std::fprintf(stderr, "Vegetation shaders failed: procedural v8 trees\n");
        return false;
    }
    for (Shader* sh : {mesh_.get(), impostor_.get()}) {
        sh->use();
        sh->set("uMoonShadow", 5);
        sh->set("uSpotShadow", 6);
        sh->set("uAO", 7);
        sh->set("uGroundMap", 8);
        sh->set("uHeatTrail", 9);
    }
    for (Shader* sh : {mesh_.get(), depth_.get(), prepass_.get(), bake_.get()}) {
        sh->use();
        sh->set("uAlbedoTex", 0);
        sh->set("uNormalTex", 1);
        sh->set("uArmTex", 2);
    }
    for (Shader* sh : {impostor_.get(), impostorPrepass_.get()}) {
        sh->use();
        sh->set("uImpAlbedo", 0);
        sh->set("uImpNormal", 1);
    }

    for (const Species& s : kSpecies)
        if (!loadAsset(root, s.asset, s.kind, s.maxVariants)) std::printf("Vegetation assets: %s missing\n", s.asset);
    for (int k = 0; k < static_cast<int>(VegKind::Count); ++k) byKind_[k].clear();
    for (size_t i = 0; i < archetypes_.size(); ++i) byKind_[static_cast<int>(archetypes_[i].kind)].push_back(static_cast<int>(i));
    // Every kind the camp uses must have at least one asset, else the procedural trees stay.
    for (VegKind k : {VegKind::CampConifer, VegKind::Broadleaf, VegKind::Bush, VegKind::Rock, VegKind::TallConifer})
        if (byKind_[static_cast<int>(k)].empty()) {
            std::printf("Vegetation assets incomplete -> procedural v8 trees\n");
            return false;
        }
    for (Archetype& a : archetypes_)
        if (speciesOf(a.asset)->impostor) bakeImpostor(a);

    glGenBuffers(1, &instanceVbo_);
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
    ready_ = true;
    loadSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    size_t tris = 0;
    for (const Archetype& a : archetypes_) tris += a.lod[0].triangles;
    std::printf("Vegetation assets: %zu variants (%zu LOD0 triangles), impostors baked, %.2f s (%s)\n", archetypes_.size(), tris,
                loadSeconds_, root.c_str());
    return true;
}

bool VegetationRenderer::loadAsset(const std::string& root, const std::string& name, VegKind kind, int maxVariants) {
    asset::Model model;
    std::string err;
    if (!asset::loadGltf(root + "/vegetation/" + name + "/" + name + ".gltf", model, &err)) return false;
    const Species& sp = *speciesOf(name);
    // Materials: textures with coverage-preserving mips for the cards.
    const int base = static_cast<int>(materials_.size());
    for (const asset::MaterialInfo& mi : model.materials) {
        Material m;
        m.foliage = asset::isFoliageMaterial(mi);
        asset::Image img;
        m.albedo = !mi.baseColor.empty() && asset::loadImage(model.dir + "/" + mi.baseColor, img)
                       ? uploadTexture(img, m.foliage ? 0.35f : 0.0f, true)   // the alpha-test cut-off of the depth passes
                       : flatTexture(128, 128, 128, 255);
        m.normal = !mi.normal.empty() && asset::loadImage(model.dir + "/" + mi.normal, img) ? uploadTexture(img, 0.0f, false)
                                                                                             : flatTexture(128, 128, 255, 255);
        m.arm = !mi.arm.empty() && asset::loadImage(model.dir + "/" + mi.arm, img) ? uploadTexture(img, 0.0f, false)
                                                                                 : flatTexture(255, 200, 0, 255);
        // Thermal: needles / leaves as the v8 foliage, bark as mats::bark (0.18), rock as mats::stone (0.30).
        if (m.foliage) {
            m.heat = 0.13f;
            m.surface = 5;
        } else if (kind == VegKind::Rock) {
            m.heat = 0.30f;
            m.surface = 7;
        } else {
            m.heat = 0.18f;
            m.surface = 6;
        }
        materials_.push_back(m);
    }
    std::vector<bool> foliage;
    for (const asset::MaterialInfo& mi : model.materials) foliage.push_back(asset::isFoliageMaterial(mi));
    asset::LodSettings ls;
    ls.anchor = sp.anchor;
    if (kind == VegKind::Rock) {   // rocks: geometric simplification only
        ls.solidRatio[1] = 0.25f;
        ls.solidRatio[2] = 0.06f;
        ls.solidError[1] = 0.01f;
        ls.solidError[2] = 0.04f;
    }
    if (kind == VegKind::GroundCover || kind == VegKind::Bush) {
        ls.keep[1] = 0.5f;
        ls.keep[2] = 0.2f;
    }
    for (size_t vi = 0; vi < model.variants.size() && static_cast<int>(vi) < maxVariants; ++vi) {
        asset::Variant& v = model.variants[vi];
        if (v.parts.empty()) continue;
        bool hasWind = true;
        for (const asset::MeshPart& p : v.parts) hasWind = hasWind && p.color.size() == p.vertexCount() * 4;
        if (!hasWind) asset::computeWindColors(v, foliage, sp.anchor);
        std::vector<asset::LodInfo> info;
        const std::vector<asset::Variant> chain = asset::buildLodChain(v, model.materials, ls, &info);
        Archetype a;
        a.asset = name;
        a.variant = v.name;
        a.kind = kind;
        a.materialBase = base;
        a.minY = v.minY;
        a.height = std::max(v.maxY - v.minY, 0.05f);
        a.radius = std::max(v.radius, 0.05f);
        a.sphere = 0.05f;
        for (const asset::MeshPart& p : v.parts)
            for (size_t k = 0; k < p.vertexCount(); ++k) {
                const float x = p.pos[3 * k], y = p.pos[3 * k + 1] - v.minY - 0.5f * a.height, z = p.pos[3 * k + 2];
                a.sphere = std::max(a.sphere, std::sqrt(x * x + y * y + z * z));
            }
        for (int l = 0; l < 3; ++l) {
            Lod& lod = a.lod[l];
            a.lodErrorMetres[l] = info[l].solidErrorMetres;
            a.cardScale[l] = info[l].cardScale;
            // Interleaved vertices: position, normal, uv (floats), wind colour (4 x u8) = 36 bytes.
            std::vector<uint8_t> vb;
            std::vector<uint32_t> ib;
            for (const asset::MeshPart& p : chain[l].parts) {
                const uint32_t first = static_cast<uint32_t>(vb.size() / 36);
                for (size_t k = 0; k < p.vertexCount(); ++k) {
                    float f[8] = {p.pos[3 * k], p.pos[3 * k + 1], p.pos[3 * k + 2], p.nrm[3 * k], p.nrm[3 * k + 1], p.nrm[3 * k + 2], p.uv[2 * k], p.uv[2 * k + 1]};
                    const uint8_t* fb = reinterpret_cast<const uint8_t*>(f);
                    vb.insert(vb.end(), fb, fb + 32);
                    vb.insert(vb.end(), &p.color[4 * k], &p.color[4 * k] + 4);
                }
                Range r;
                r.first = static_cast<GLsizei>(ib.size());
                r.count = static_cast<GLsizei>(p.idx.size());
                r.material = base + p.material;
                for (uint32_t i : p.idx) ib.push_back(first + i);
                lod.ranges.push_back(r);
            }
            lod.triangles = ib.size() / 3;
            glGenVertexArrays(1, &lod.vao);
            glGenBuffers(1, &lod.vbo);
            glGenBuffers(1, &lod.ebo);
            glBindVertexArray(lod.vao);
            glBindBuffer(GL_ARRAY_BUFFER, lod.vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vb.size()), vb.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(12));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 36, reinterpret_cast<void*>(24));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 36, reinterpret_cast<void*>(32));
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lod.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(ib.size() * 4), ib.data(), GL_STATIC_DRAW);
            glBindVertexArray(0);
        }
        archetypes_.push_back(std::move(a));
    }
    return true;
}

void VegetationRenderer::bakeImpostor(Archetype& a) {
    // Bounding sphere of the variant (roots at y = 0).
    const Vec3 centre{0.0f, 0.5f * a.height, 0.0f};
    const float radius = a.sphere * 1.02f;   // tight sphere: the tree fills its picture
    const int cell = kImpCell * kImpSuper, size = kImpGrid * cell;
    GLuint tex[2] = {0, 0}, depth = 0, fbo = 0;
    glGenTextures(2, tex);
    for (GLuint t : tex) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tex[1], 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    glViewport(0, 0, size, size);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    bake_->use();
    bake_->set("uMinY", a.minY);
    const Lod& lod = a.lod[0];
    glBindVertexArray(lod.vao);
    for (int j = 0; j < kImpGrid; ++j)
        for (int i = 0; i < kImpGrid; ++i) {
            const asset::OctDir d = asset::impostorCellDirection(i, j, kImpGrid);
            const Vec3 dir{d.x, d.y, d.z};
            const Vec3 hint = std::fabs(dir.y) > 0.999f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            const Vec3 right = normalize(cross(hint, dir)), up = cross(dir, right);
            const Vec3 eye = centre + dir * (2.0f * radius);
            Mat4 view = Mat4::identity();
            const Vec3 rows[3] = {right, up, dir};
            for (int r = 0; r < 3; ++r) {
                view.at(r, 0) = rows[r].x;
                view.at(r, 1) = rows[r].y;
                view.at(r, 2) = rows[r].z;
                view.at(r, 3) = -dot(rows[r], eye);
            }
            const Mat4 proj = ortho(-radius, radius, -radius, radius, 0.01f, 4.0f * radius);
            glViewport(i * cell, j * cell, cell, cell);
            bake_->set("uViewProj", proj * view);
            bake_->set("uBakeDir", dir);
            for (const Range& r : lod.ranges) {
                const Material& m = materials_[static_cast<size_t>(r.material)];
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m.albedo);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, m.arm);
                bake_->set("uFoliage", m.foliage ? 1 : 0);
                glDrawElements(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<size_t>(r.first) * 4));
            }
        }
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    // Read back, premultiply, 2x down-sample (super-sampled edges), coverage-preserving mips, upload.
    asset::Image img[2];
    for (int k = 0; k < 2; ++k) {
        img[k].w = img[k].h = size;
        img[k].rgba.resize(static_cast<size_t>(size) * size * 4);
        glReadBuffer(GL_COLOR_ATTACHMENT0 + k);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, img[k].rgba.data());
    }
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depth);
    glDeleteTextures(2, tex);
    for (size_t p = 0; p < img[0].rgba.size(); p += 4) {
        const float cov = img[0].rgba[p + 3] / 255.0f;
        for (int c = 0; c < 3; ++c) img[0].rgba[p + c] = static_cast<uint8_t>(img[0].rgba[p + c] * cov + 0.5f);
        const uint8_t ao = img[1].rgba[p + 3];
        for (int c = 0; c < 3; ++c) img[1].rgba[p + c] = static_cast<uint8_t>(img[1].rgba[p + c] * cov + 0.5f);
        img[1].rgba[p + 3] = static_cast<uint8_t>(ao * cov + 0.5f);
    }
    asset::Image alb = img[0], nrm = img[1];
    for (int s = 1; s < kImpSuper; s *= 2) {
        alb = downPremul(alb);
        nrm = downPremul(nrm);
    }
    // The normal atlas carries the coverage of the albedo atlas in the impostor shader (it divides by albedo alpha).
    if (const char* dump = std::getenv("TS_DUMP_IMPOSTORS"); dump && *dump) {   // debugging: the atlas over grey
        std::vector<uint8_t> rgb(static_cast<size_t>(alb.w) * alb.h * 3);
        for (int y = 0; y < alb.h; ++y)
            for (int x = 0; x < alb.w; ++x) {
                const uint8_t* p = alb.at(x, alb.h - 1 - y);
                for (int c = 0; c < 3; ++c)
                    rgb[(static_cast<size_t>(y) * alb.w + x) * 3 + c] = static_cast<uint8_t>(std::min(255, p[c] + (255 - p[3]) * 128 / 255));
            }
        writePng(std::string(dump) + "/" + a.asset + "_" + a.variant + ".png", alb.w, alb.h, rgb);
    }
    std::vector<float> scales;
    a.imp.albedo = uploadPremulMips(alb, true, scales);
    a.imp.normal = uploadPremulMips(nrm, false, scales);
    a.imp.grid = kImpGrid;
    a.imp.cell = kImpCell;
    a.imp.centre = centre;
    a.imp.radius = radius;
}

void VegetationRenderer::setLayout(const std::vector<VegInstance>& in) {
    instances_.clear();
    for (const VegInstance& v : in) {
        const std::vector<int>& list = byKind_[static_cast<int>(v.kind)];
        if (list.empty()) continue;
        Instance i;
        i.arch = list[vegHash(v.seed * 2654435761u + 17u) % list.size()];
        const Archetype& a = archetypes_[static_cast<size_t>(i.arch)];
        i.x = v.x;
        i.y = v.y;
        i.z = v.z;
        i.yaw = radians(v.yawDeg);
        i.sy = v.height / a.height;
        i.sxz = v.width > 0.0f ? clampf(v.width / (2.0f * a.radius), 0.7f * i.sy, 1.6f * i.sy) : i.sy;
        if (v.kind == VegKind::Rock) i.y -= 0.12f * v.height;   // bed the rock into the ground
        i.seed = static_cast<float>(vegHash(v.seed) & 0xFFFF) / 65536.0f;
        i.worldHeight = a.height * i.sy;
        i.worldRadius = std::max(a.radius * i.sxz, 0.5f * i.worldHeight);
        instances_.push_back(i);
    }
    buildCells();
}

void VegetationRenderer::buildCells() {
    cells_.clear();
    if (instances_.empty()) return;
    float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
    for (const Instance& i : instances_) {
        minX = std::min(minX, i.x);
        maxX = std::max(maxX, i.x);
        minZ = std::min(minZ, i.z);
        maxZ = std::max(maxZ, i.z);
    }
    const int nx = std::max(1, static_cast<int>(std::ceil((maxX - minX + 1e-3f) / kCellSize)));
    const int nz = std::max(1, static_cast<int>(std::ceil((maxZ - minZ + 1e-3f) / kCellSize)));
    std::vector<Cell> grid(static_cast<size_t>(nx * nz));
    for (size_t k = 0; k < instances_.size(); ++k) {
        const Instance& i = instances_[k];
        const int cx = std::min(nx - 1, static_cast<int>((i.x - minX) / kCellSize));
        const int cz = std::min(nz - 1, static_cast<int>((i.z - minZ) / kCellSize));
        grid[static_cast<size_t>(cz * nx + cx)].items.push_back(static_cast<int>(k));
    }
    for (Cell& c : grid) {
        if (c.items.empty()) continue;
        Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        float reach = 0.0f;
        for (int k : c.items) {
            const Instance& i = instances_[static_cast<size_t>(k)];
            lo = {std::min(lo.x, i.x), std::min(lo.y, i.y), std::min(lo.z, i.z)};
            hi = {std::max(hi.x, i.x), std::max(hi.y, i.y + i.worldHeight), std::max(hi.z, i.z)};
            reach = std::max(reach, i.worldRadius);
        }
        c.centre = (lo + hi) * 0.5f;
        c.radius = length(hi - lo) * 0.5f + reach;
        cells_.push_back(std::move(c));
    }
}

void VegetationRenderer::select(const View& v, bool allowImpostor) {
    selection_.assign(archetypes_.size() * 4, {});
    const Frustum frustum(v.viewProj);
    for (const Cell& c : cells_) {
        if (!frustum.intersects(c.centre, c.radius)) continue;
        for (int k : c.items) {
            const Instance& i = instances_[static_cast<size_t>(k)];
            const Vec3 centre{i.x, i.y + 0.5f * i.worldHeight, i.z};
            const float r = std::sqrt(i.worldRadius * i.worldRadius + 0.25f * i.worldHeight * i.worldHeight);
            if (!frustum.intersects(centre, r)) continue;
            const Archetype& a = archetypes_[static_cast<size_t>(i.arch)];
            const float dist = std::max(length(centre - v.camPos), 0.1f);
            const float px = i.worldHeight / dist * v.pixelsPerRadian;
            const bool hasImp = a.imp.albedo != 0 && allowImpostor;
            const bool small = a.kind == VegKind::GroundCover || a.kind == VegKind::Bush || a.kind == VegKind::Rock;
            // Small objects fade out instead of turning into impostors.
            const float cullPx = a.kind == VegKind::GroundCover ? 14.0f : 6.0f;
            float keepAll = 1.0f;
            if (small && !v.staticPass) {
                if (px < cullPx) continue;
                keepAll = smooth01(cullPx, cullPx * 1.6f, px);
            }
            // Level and cross-fade: t = continuous level from the projected size.
            int level;
            float fade = 0.0f;   // share of the next (coarser) level inside the band
            if (v.staticPass) {
                level = 2;
            } else {
                level = 3;
                for (int l = 0; l < 3; ++l)
                    if (px > kLodPixels[l] * (1.0f - kFadeBand)) {
                        level = l;
                        fade = 1.0f - smooth01(kLodPixels[l] * (1.0f - kFadeBand), kLodPixels[l] * (1.0f + kFadeBand), px);
                        break;
                    }
                level = std::min(level + v.lodBias, 3);
                if (v.lodBias) fade = 0.0f;
                if (level == 3 && !hasImp) {
                    level = 2;
                    fade = 0.0f;
                }
                if (level == 2 && !hasImp) fade = 0.0f;
            }
            const float seed = i.seed;
            auto push = [&](int lv, float dither) {
                if (lv == 3 && !hasImp) return;
                selection_[static_cast<size_t>(i.arch) * 4 + static_cast<size_t>(lv)].push_back(
                    {i.x, i.y, i.z, i.yaw, i.sxz, i.sy, seed, dither});
            };
            if (fade > 0.001f && level < 3) {
                // this level keeps the noise values >= fade, the next one those < fade (complementary dither)
                push(level, -std::max(fade, 1e-3f));
                push(level + 1, std::max(fade, 1e-3f));
            } else if (keepAll < 0.999f) {
                push(level, keepAll);
            } else {
                push(level, 0.0f);
            }
        }
    }
}

void VegetationRenderer::uploadSelection() {
    size_t total = 0;
    selectionOffset_.assign(selection_.size(), 0);
    for (size_t k = 0; k < selection_.size(); ++k) {
        selectionOffset_[k] = total;
        total += selection_[k].size();
    }
    std::vector<GpuInstance> all;
    all.reserve(total);
    for (const auto& s : selection_) all.insert(all.end(), s.begin(), s.end());
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    const size_t bytes = std::max<size_t>(all.size(), 1) * sizeof(GpuInstance);
    if (bytes > instanceCapacity_) instanceCapacity_ = bytes * 2;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instanceCapacity_), nullptr, GL_STREAM_DRAW);   // orphan
    if (!all.empty()) glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(all.size() * sizeof(GpuInstance)), all.data());
}

void VegetationRenderer::setMaterial(Shader& sh, const Material& m, bool bindTextures) {
    if (bindTextures) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m.albedo);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m.normal);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m.arm);
        glActiveTexture(GL_TEXTURE0);
        sh.set("uHeat", m.heat);
        sh.set("uHeatVariation", 0.015f);
        sh.set("uSurface", m.surface);
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m.albedo);
    }
    sh.set("uFoliage", m.foliage ? 1 : 0);
}

void VegetationRenderer::setArchetypeUniforms(Shader& sh, const Archetype& a, const View& v) {
    const Species& s = *speciesOf(a.asset);
    const float k = v.staticPass ? 0.0f : 1.0f;
    const float amp[4] = {s.sway * k, s.branch * k, s.flutter * k, a.height};
    const GLint loc = glGetUniformLocation(sh.id(), "uWindAmp");
    if (loc >= 0) glUniform4fv(loc, 1, amp);
    const float wd = 1.0f / std::sqrt(0.8f * 0.8f + 0.45f * 0.45f);
    sh.set("uWindDir", 0.8f * wd, 0.45f * wd);
    sh.set("uMinY", a.minY);
    sh.set("uTime", v.staticPass ? 0.0f : v.time);
    sh.set("uAlbedoScale", s.albedoScale);
    sh.set("uTintA", s.tintA);
    sh.set("uTintB", s.tintB);
    sh.set("uTransmission", s.transmission);
}

int VegetationRenderer::drawMeshes(Shader& sh, bool bindTextures, bool foliageOnlyAlpha, const View& v) {
    (void)foliageOnlyAlpha;
    int draws = 0;
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    for (size_t ai = 0; ai < archetypes_.size(); ++ai) {
        const Archetype& a = archetypes_[ai];
        bool uniformsSet = false;
        for (int l = 0; l < 3; ++l) {
            const size_t key = ai * 4 + static_cast<size_t>(l);
            const GLsizei n = static_cast<GLsizei>(selection_[key].size());
            if (n == 0) continue;
            if (!uniformsSet) {
                setArchetypeUniforms(sh, a, v);
                uniformsSet = true;
            }
            const Lod& lod = a.lod[l];
            glBindVertexArray(lod.vao);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
            const size_t off = selectionOffset_[key] * sizeof(GpuInstance);
            for (int at = 4; at <= 5; ++at) {
                glEnableVertexAttribArray(at);
                glVertexAttribDivisor(at, 1);
                glVertexAttribPointer(at, 4, GL_FLOAT, GL_FALSE, sizeof(GpuInstance), reinterpret_cast<void*>(off + static_cast<size_t>(at - 4) * 16));
            }
            for (const Range& r : lod.ranges) {
                setMaterial(sh, materials_[static_cast<size_t>(r.material)], bindTextures);
                glDrawElementsInstanced(GL_TRIANGLES, r.count, GL_UNSIGNED_INT, reinterpret_cast<void*>(static_cast<size_t>(r.first) * 4), n);
                ++draws;
            }
            stats_.instancesDrawn += n;
            stats_.perLod[l] += n;
            stats_.triangles += static_cast<long long>(lod.triangles) * n;
        }
    }
    glBindVertexArray(0);
    if (cull) glEnable(GL_CULL_FACE);
    return draws;
}

int VegetationRenderer::drawImpostors(Shader& sh, const View& v) {
    int draws = 0;
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(quadVao_);
    for (size_t ai = 0; ai < archetypes_.size(); ++ai) {
        const Archetype& a = archetypes_[ai];
        const size_t key = ai * 4 + 3;
        const GLsizei n = static_cast<GLsizei>(selection_[key].size());
        if (n == 0 || !a.imp.albedo) continue;
        setArchetypeUniforms(sh, a, v);
        sh.set("uImpCentre", a.imp.centre);
        sh.set("uImpRadius", a.imp.radius);
        sh.set("uImpGrid", static_cast<float>(a.imp.grid));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, a.imp.albedo);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, a.imp.normal);
        glActiveTexture(GL_TEXTURE0);
        sh.set("uHeat", 0.13f);
        sh.set("uHeatVariation", 0.015f);
        glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
        const size_t off = selectionOffset_[key] * sizeof(GpuInstance);
        for (int at = 4; at <= 5; ++at) {
            glEnableVertexAttribArray(at);
            glVertexAttribDivisor(at, 1);
            glVertexAttribPointer(at, 4, GL_FLOAT, GL_FALSE, sizeof(GpuInstance), reinterpret_cast<void*>(off + static_cast<size_t>(at - 4) * 16));
        }
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n);
        ++draws;
        stats_.impostors += n;
        stats_.perLod[3] += n;
        stats_.triangles += 2LL * n;
    }
    glBindVertexArray(0);
    if (cull) glEnable(GL_CULL_FACE);
    return draws;
}

int VegetationRenderer::drawDepth(const View& v, const Mat4& lightVP) {
    if (!ready_ || instances_.empty()) return 0;
    View lv = v;
    lv.viewProj = lightVP;   // cull against the light frustum, LOD from the camera distance
    select(lv, false);
    uploadSelection();
    depth_->use();
    depth_->set("uLightVP", lightVP);
    return drawMeshes(*depth_, false, true, v);
}

int VegetationRenderer::drawPrepass(const View& v, const Mat4& view, const Mat4& proj) {
    if (!ready_ || instances_.empty()) return 0;
    select(v, true);
    uploadSelection();
    prepass_->use();
    prepass_->set("uView", view);
    prepass_->set("uProj", proj);
    int draws = drawMeshes(*prepass_, false, true, v);
    impostorPrepass_->use();
    impostorPrepass_->set("uView", view);
    impostorPrepass_->set("uProj", proj);
    impostorPrepass_->set("uCamPos", v.camPos);
    draws += drawImpostors(*impostorPrepass_, v);
    return draws;
}

std::string VegetationRenderer::summaryJson() const {
    std::ostringstream o;
    o << "{\n  \"load_seconds\": " << loadSeconds_ << ",\n  \"impostor\": {\"grid\": " << kImpGrid << ", \"cell_px\": " << kImpCell
      << ", \"supersample\": " << kImpSuper << ", \"atlas_px\": " << kImpGrid * kImpCell << "},\n  \"lod_pixels\": [" << kLodPixels[0]
      << ", " << kLodPixels[1] << ", " << kLodPixels[2] << "],\n  \"variants\": [\n";
    for (size_t i = 0; i < archetypes_.size(); ++i) {
        const Archetype& a = archetypes_[i];
        o << "    {\"asset\": \"" << a.asset << "\", \"variant\": \"" << a.variant << "\", \"kind\": " << static_cast<int>(a.kind)
          << ", \"height_m\": " << a.height << ", \"triangles\": [" << a.lod[0].triangles << ", " << a.lod[1].triangles << ", "
          << a.lod[2].triangles << "], \"solid_error_m\": [" << a.lodErrorMetres[0] << ", " << a.lodErrorMetres[1] << ", "
          << a.lodErrorMetres[2] << "], \"card_scale\": [" << a.cardScale[0] << ", " << a.cardScale[1] << ", " << a.cardScale[2]
          << "], \"impostor\": " << (a.imp.albedo ? "true" : "false") << "}" << (i + 1 < archetypes_.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

}  // namespace ts
