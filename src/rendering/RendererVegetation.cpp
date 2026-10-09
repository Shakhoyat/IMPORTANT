// v9 M6: the asset vegetation inside the frame (VegetationRenderer): when it is used, which primitive trees it
// replaces, and its colour pass. The pre-pass and shadow passes call it from RendererFrame.cpp.
#include <cstdlib>
#include <fstream>

#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "objects/VegetationLayout.h"
#include "rendering/Renderer.h"

namespace ts {

namespace {

bool vegetationDisabledByEnv() {
    static const bool off = [] {
        const char* v = std::getenv("TS_NO_VEG_ASSETS");
        return v && *v == '1';
    }();
    return off;
}

bool vegetationWanted(const RenderSettings& s) {
    return !vegetationDisabledByEnv() && s.quality >= Quality::Ultra && s.view != ViewMode::Diagnostic && s.isolate.empty() &&
           s.edges == EdgeMode::Solid;
}

float groundAt(float x, float z) {
    return x * x + z * z < layout::kBaseRadius * layout::kBaseRadius ? terrainHeight(x, z) : layout::kApronLevel;
}

}  // namespace

void Renderer::prepareVegetation(const Scene& scene, const RenderSettings& s) {
    if (vegLayoutBuilt_ || !vegetationWanted(s)) return;
    vegLayoutBuilt_ = true;
    if (!vegetation_.load()) return;
    std::vector<const SceneNode*> leaves(scene.leaves().begin(), scene.leaves().end());
    VegLayout layout = extractVegetation(leaves);
    addGroundCover(layout, groundAt, 0x5EEDu);
    vegetation_.setLayout(layout.instances);
    vegHidden_.clear();
    vegHidden_.insert(layout.replaced.begin(), layout.replaced.end());
    std::ofstream("build/vegetation_assets.json") << vegetation_.summaryJson();
}

bool Renderer::vegActive(const RenderSettings& s) { return vegetation_.ready() && vegetationWanted(s); }


VegetationRenderer::View Renderer::vegView(const CameraView& cam, int h, const Scene& scene) const {
    VegetationRenderer::View v;
    v.viewProj = cam.proj * cam.view;
    v.camPos = cam.position;
    const float fovY = 2.0f * std::atan(1.0f / std::max(cam.proj.at(1, 1), 1e-4f));
    v.pixelsPerRadian = static_cast<float>(h) / std::max(fovY, 1e-3f);
    v.time = scene.time();
    return v;
}

void Renderer::drawVegetation(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st) {
    if (!vegActive(s) || !s.vegetationOn) return;
    const bool timed = mrt_;
    if (timed) timer_.begin("vegetation");
    VegetationRenderer::View v = vegView(cam, h, scene);
    v.a2c = mrt_ && fb_.samples() > 1;
    auto prepare = [&](Shader& sh) {
        setFrameUniforms(sh, scene, cam, w, h, s);
        sh.set("uFogged", 1.0f);
        sh.set("uHeatVariation", 0.015f);
        sh.set("uMetalness", 0.0f);
        sh.set("uSpecular", 0.15f);
        sh.set("uShininess", 16.0f);
        sh.set("uRoughness", 0.85f);
        sh.set("uEmissive", 0.0f);
        sh.set("uOpacity", 1.0f);
        sh.set("uBaseColor", primitiveColor(PrimitiveType::Cone));
    };
    st.drawCalls += vegetation_.draw(v, prepare);
    const VegetationRenderer::Stats& vs = vegetation_.stats();
    st.triangles += vs.triangles;
    st.vegInstances = vs.instancesDrawn + vs.impostors;
    st.vegImpostors = vs.impostors;
    st.vegTriangles = vs.triangles;
    for (int l = 0; l < 4; ++l) st.vegLod[l] = vs.perLod[l];
    if (timed) timer_.end();
}

}  // namespace ts
