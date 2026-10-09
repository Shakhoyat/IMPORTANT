// v9 M6: CC0 asset vegetation (Poly Haven glTF trees, bushes, ferns, rocks) at game quality.
//
//   * load:      assets/vegetation/<name>/<name>.gltf via cgltf (assets/Model.h), textures via stb_image with
//                coverage-preserving mipmaps for the alpha-tested cards (assets/Image.h);
//   * LOD:       per variant three meshes (assets/FoliageLod.h: meshoptimizer for the solid parts, card thinning
//                for the foliage) + an octahedral impostor baked here at start-up (assets/Impostor.h);
//                the level is chosen per instance from its projected height in pixels, with a dithered cross-fade
//                band between levels (TAA resolves the dither, no popping);
//   * shading:   alpha-to-coverage on the multisampled scene target (alpha sharpened by its screen derivative, so
//                edges are smooth and stable under the TAA jitter), two-sided lighting with translucency, normal maps,
//                hierarchical vertex wind (trunk sway -> branch -> leaf flutter, weights in COLOR_0), per-instance
//                hue / brightness variation, the same sun / moon / sky / practical lights and shadows as the scene;
//   * instancing: one glDrawElementsInstanced per (variant, LOD, material); instances sit in a grid of culling cells
//                (frustum test per cell, then per instance);
//   * shadows:   near cascade with the camera LOD + 1 (and wind), the cached world cascade with LOD 2.
// Missing assets/: ready() stays false and the renderer keeps the procedural v8 trees.
#pragma once

#include <glad/gl.h>

#include <memory>
#include <string>
#include <vector>

#include "math/Frustum.h"
#include "objects/VegetationLayout.h"
#include "rendering/Shader.h"

namespace ts {

class VegetationRenderer {
public:
    VegetationRenderer() = default;
    ~VegetationRenderer();
    VegetationRenderer(const VegetationRenderer&) = delete;
    VegetationRenderer& operator=(const VegetationRenderer&) = delete;

    // Loads the assets and bakes the impostors (needs the GL context). False (and ready() false) if assets/ is missing.
    bool load();
    bool ready() const { return ready_; }
    bool attempted() const { return attempted_; }
    void setLayout(const std::vector<VegInstance>& instances);
    size_t instanceCount() const { return instances_.size(); }

    struct View {
        Mat4 viewProj;
        Vec3 camPos;
        float pixelsPerRadian = 1000.0f;   // screen height / vertical field of view (projected size of the LOD test)
        float time = 0.0f;
        int lodBias = 0;                   // shadow passes draw a coarser level
        bool a2c = false;                  // alpha-to-coverage (multisampled target)
        bool shadowPass = false;
        bool staticPass = false;           // cached world cascade: no wind, fixed LOD 2, no impostors
    };
    // Main pass. 'prepare' sets the frame uniforms (view, proj, lights, shadows) on a shader. Returns draw calls.
    template <typename Prepare>
    int draw(const View& v, Prepare prepare);
    int drawDepth(const View& v, const Mat4& lightVP);                     // shadow maps
    int drawPrepass(const View& v, const Mat4& view, const Mat4& proj);    // depth + view normal (SSAO, fog)

    struct Stats {
        int instancesDrawn = 0, impostors = 0, drawCalls = 0;
        long long triangles = 0;
        int perLod[4] = {0, 0, 0, 0};
    };
    const Stats& stats() const { return stats_; }
    // For the docs / workbook (build/vegetation_assets.json): per variant triangles of LOD 0..2, impostor atlas size.
    std::string summaryJson() const;
    double loadSeconds() const { return loadSeconds_; }

private:
    struct Range {
        GLsizei first = 0, count = 0;
        int material = 0;
    };
    struct Lod {
        GLuint vao = 0, vbo = 0, ebo = 0;
        std::vector<Range> ranges;
        size_t triangles = 0;
    };
    struct Impostor {
        GLuint albedo = 0, normal = 0;
        int grid = 0, cell = 0;
        Vec3 centre;
        float radius = 0.0f;
    };
    struct Archetype {
        std::string asset, variant;
        VegKind kind = VegKind::CampConifer;
        Lod lod[3];
        Impostor imp;
        float minY = 0, height = 1, radius = 1;   // native bounds
        float sphere = 1;                         // bounding sphere radius about (0, height / 2, 0) (impostor)
        int materialBase = 0;
        float lodErrorMetres[3] = {0, 0, 0};
        float cardScale[3] = {1, 1, 1};
    };
    struct Material {
        GLuint albedo = 0, normal = 0, arm = 0;
        bool foliage = false;
        float heat = 0.13f;
        int surface = 5;   // 5 foliage, 6 bark, 7 rock
    };
    struct Instance {
        int arch = 0;
        float x, y, z, yaw;            // yaw radians
        float sxz, sy, seed;
        float worldHeight, worldRadius;
    };
    struct GpuInstance {
        float x, y, z, yaw;
        float sxz, sy, seed, dither;
    };
    struct Cell {
        Vec3 centre;
        float radius = 0;
        std::vector<int> items;
    };

    bool loadAsset(const std::string& root, const std::string& name, VegKind kind, int maxVariants);
    void bakeImpostor(Archetype& a);
    void buildCells();
    // Instance lists per (archetype, level 0..3) for a view; level 3 = impostor.
    void select(const View& v, bool allowImpostor);
    void uploadSelection();
    int drawMeshes(Shader& sh, bool bindTextures, bool foliageOnlyAlpha, const View& v);
    int drawImpostors(Shader& sh, const View& v);
    void setMaterial(Shader& sh, const Material& m, bool bindTextures);
    void setArchetypeUniforms(Shader& sh, const Archetype& a, const View& v);

    std::vector<Archetype> archetypes_;
    std::vector<Material> materials_;
    std::vector<Instance> instances_;
    std::vector<Cell> cells_;
    std::vector<int> byKind_[static_cast<int>(VegKind::Count)];   // archetypes per kind
    std::vector<std::vector<GpuInstance>> selection_;             // [arch * 4 + level]
    std::vector<size_t> selectionOffset_;
    GLuint instanceVbo_ = 0, quadVao_ = 0, quadVbo_ = 0;
    size_t instanceCapacity_ = 0;
    std::unique_ptr<Shader> mesh_, depth_, prepass_, impostor_, impostorPrepass_, bake_;
    bool ready_ = false, attempted_ = false;
    Stats stats_;
    double loadSeconds_ = 0.0;
};

template <typename Prepare>
int VegetationRenderer::draw(const View& v, Prepare prepare) {
    stats_ = Stats{};
    if (!ready_ || instances_.empty()) return 0;
    select(v, true);
    uploadSelection();
    int draws = 0;
    if (v.a2c) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    mesh_->use();
    prepare(*mesh_);
    mesh_->set("uA2C", v.a2c ? 1 : 0);
    draws += drawMeshes(*mesh_, true, false, v);
    impostor_->use();
    prepare(*impostor_);
    impostor_->set("uA2C", v.a2c ? 1 : 0);
    draws += drawImpostors(*impostor_, v);
    if (v.a2c) glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    stats_.drawCalls = draws;
    return draws;
}

}  // namespace ts
