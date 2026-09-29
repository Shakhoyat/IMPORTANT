// The v2 frame: shadow maps, depth/normal pre-pass, SSAO, volumetric fog, HDR scene, bloom and composite.
#include <algorithm>

#include "objects/Terrain.h"
#include "rendering/Renderer.h"

namespace ts {

namespace {

constexpr float kShadowCasterMargin = 30.0f;  // metres behind the fitted sphere that may still cast shadows
constexpr float kFogMaxDistance = 70.0f;

}  // namespace

const char* qualityName(Quality q) {
    switch (q) {
        case Quality::Low: return "LOW";
        case Quality::High: return "HIGH";
        case Quality::Ultra: return "ULTRA";
    }
    return "?";
}

QualityProfile qualityProfile(Quality q) {
    QualityProfile p;
    if (q == Quality::Low) {
        p.msaa = 2;
        p.moonShadowRes = 1024;
        p.spotShadowRes = 0;
        p.ssao = false;
        p.volumetric = false;
        p.cascadeSplit = 10.0f;
        p.grassBlades = 14000;
        p.rainDrops = 6000;
    } else if (q == Quality::Ultra) {
        p.msaa = 8;
        p.moonShadowRes = 4096;
        p.spotShadowRes = 2048;
        p.ssaoSamples = 16;
        p.pcfTaps = 16;
        p.moonShafts = true;
        p.grassBlades = 140000;
        p.rainDrops = 36000;
        p.fogSteps = 48;
        p.fogDivisor = 2;
        p.cascadeSplit = 14.0f;
    }
    return p;
}

void Renderer::computeLightMatrices(const Scene& scene, const CameraView& cam, const QualityProfile& q) {
    // Camera field of view and aspect ratio recovered from the projection matrix.
    const float fovY = 2.0f * std::atan(1.0f / cam.proj.at(1, 1));
    const float aspect = cam.proj.at(1, 1) / cam.proj.at(0, 0);
    frame_.split = q.cascadeSplit;
    const Sphere nearSlice = frustumSliceSphere(inverse(cam.view), fovY, aspect, cam.nearZ, q.cascadeSplit);
    const Sphere world{{0.0f, 1.5f, 0.0f}, kTerrainRadius + 3.0f};
    const Vec3 moonDir = scene.lights.moon.direction;
    frame_.moon[0] = fitDirectionalShadow(moonDir, nearSlice, q.moonShadowRes, kShadowCasterMargin);
    frame_.moon[1] = fitDirectionalShadow(moonDir, world, q.moonShadowRes, kShadowCasterMargin);
    const SpotLight& spot = scene.lights.spot;
    frame_.spot = spotShadowMatrix(spot.position, spot.direction, spot.outerDeg, 0.05f, 40.0f);
}

namespace {

bool isDynamic(const SceneNode& n) { return n.object == "Autonomous UAV" || n.object == "Target Rover"; }

}  // namespace

void Renderer::drawDepthOnly(const Scene& scene, const Mat4& lightVP, Casters which, FrameStats& st) {
    depth_->set("uLightVP", lightVP);
    const Frustum frustum(lightVP);
    for (const SceneNode* n : scene.leaves()) {
        if (n->primitive == PrimitiveType::BeamCone || n->name == "Moon" || n->object == "Backdrop") continue;
        if (n->material.opacity < 1.0f) continue;  // blur discs cast no shadow
        if (!inFrustum(*n, frustum)) continue;
        if (which == Casters::AllButUav && n->object == "Autonomous UAV") continue;  // the lamp is inside it
        if (which == Casters::Static && isDynamic(*n)) continue;
        if (which == Casters::Dynamic && !isDynamic(*n)) continue;
        depth_->set("uModel", n->world);
        const Mesh& mesh = *meshes_.at(n->primitive);
        mesh.bind(true);
        mesh.draw(true);
        ++st.shadowDraws;
    }
}

void Renderer::renderShadowMaps(const Scene& scene, FrameStats& st) {
    timer_.begin("shadow maps");
    depth_->use();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);  // the terrain is an open surface: both sides must cast
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);  // slope-scaled depth bias against shadow acne
    shadows_.bindMoonLayer(0);  // near cascade follows the camera: everything, every frame
    glClear(GL_DEPTH_BUFFER_BIT);
    drawDepthOnly(scene, frame_.moon[0].viewProj, Casters::All, st);
    // World cascade: its matrix never changes, so the static casters are rendered once and cached;
    // each frame the cache is copied in and only the moving UAV and rover are drawn on top.
    if (!shadows_.staticValid || shadows_.staticKey != scene.explode) {
        shadows_.bindStaticCache();
        glClear(GL_DEPTH_BUFFER_BIT);
        drawDepthOnly(scene, frame_.moon[1].viewProj, Casters::Static, st);
        shadows_.staticValid = true;
        shadows_.staticKey = scene.explode;
    }
    shadows_.copyStaticCacheToLayer(1);
    drawDepthOnly(scene, frame_.moon[1].viewProj, Casters::Dynamic, st);
    if (frame_.shadowMode == 2) {
        shadows_.bindSpot();
        glClear(GL_DEPTH_BUFFER_BIT);
        drawDepthOnly(scene, frame_.spot.viewProj, Casters::AllButUav, st);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_CULL_FACE);
    timer_.end();
}

void Renderer::renderPrepass(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st) {
    timer_.begin("pre-pass");
    fb_.bindPrepass();
    glClearColor(0.5f, 0.5f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    prepass_->use();
    prepass_->set("uView", cam.view);
    prepass_->set("uProj", cam.proj);
    const Frustum frustum(cam.proj * cam.view);
    for (const SceneNode* n : scene.leaves()) {
        if (n->primitive == PrimitiveType::BeamCone || n->name == "Moon" || n->material.opacity < 1.0f) continue;
        if (!inFrustum(*n, frustum)) continue;
        if (!s.isolate.empty() && n->object != s.isolate) continue;
        prepass_->set("uModel", n->world);
        prepass_->set("uNormalMat", normalMatrix(n->world));
        const Mesh& mesh = *meshes_.at(n->primitive);
        mesh.bind(true);
        mesh.draw(true);
        ++st.prepassDraws;
    }
    timer_.end();
}

void Renderer::renderSsao(const CameraView& cam, const QualityProfile& q) {
    timer_.begin("ssao");
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(emptyVao_);
    fb_.ao[0].bind();
    ssao_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassDepth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassNormal);
    ssao_->set("uDepth", 0);
    ssao_->set("uNormal", 1);
    ssao_->set("uProj", cam.proj);
    ssao_->set("uInvProj", inverse(cam.proj));
    ssao_->set("uRadius", 0.45f);
    ssao_->set("uSamples", q.ssaoSamples);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    fb_.ao[1].bind();
    blur_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_.ao[0].tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassDepth);
    blur_->set("uSrc", 0);
    blur_->set("uDepth", 1);
    blur_->set("uNearFar", cam.nearZ, cam.farZ);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    timer_.end();
}

void Renderer::renderFog(const Scene& scene, const CameraView& cam, const RenderSettings& s, const QualityProfile& q) {
    timer_.begin("volumetric fog");
    const LightRig& L = scene.lights;
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(emptyVao_);
    fb_.fog.bind();
    fog_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassDepth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadows_.moonTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadows_.spotTex);
    glActiveTexture(GL_TEXTURE0);
    fog_->set("uDepth", 0);
    fog_->set("uMoonShadow", 1);
    fog_->set("uSpotShadow", 2);
    fog_->set("uInvViewProj", inverse(cam.proj * cam.view));
    fog_->set("uCamPos", cam.position);
    fog_->set("uTime", scene.time());
    fog_->set("uSteps", q.fogSteps);
    fog_->set("uMaxDistance", kFogMaxDistance);
    fog_->set("uDensity", 0.022f * (1.0f + 1.2f * s.rain));
    fog_->set("uHeightFalloff", 0.22f);
    fog_->set("uAmbientFog", Vec3{0.006f, 0.008f, 0.014f});
    fog_->set("uMoonDir", L.moon.direction);
    fog_->set("uMoonColor", L.moon.color * (s.moonOn ? L.moon.intensity * 0.35f : 0.0f));
    fog_->set("uMoonVP1", frame_.moon[1].viewProj);
    fog_->set("uBeaconPos", L.beacon.position);
    fog_->set("uBeaconColor", L.beacon.color * (s.beaconOn ? L.beacon.intensity * 0.6f : 0.0f));
    fog_->set("uBeaconAtt", L.beacon.linear, L.beacon.quadratic);
    fog_->set("uSpotPos", L.spot.position);
    fog_->set("uSpotDir", L.spot.direction);
    fog_->set("uSpotColor", L.spot.color * (s.spotOn ? L.spot.intensity * 6.0f : 0.0f));
    fog_->set("uSpotCos", std::cos(radians(L.spot.innerDeg)), std::cos(radians(L.spot.outerDeg)));
    fog_->set("uSpotAtt", L.spot.linear, L.spot.quadratic);
    fog_->set("uSpotVP", frame_.spot.viewProj);
    fog_->set("uSpotShadowOn", frame_.shadowMode == 2 ? 1 : 0);
    fog_->set("uMoonShafts", q.moonShafts && frame_.shadowMode > 0 ? 1 : 0);
    setPracticalUniforms(*fog_, s.practicalsOn);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    timer_.end();
}

void Renderer::gatherPracticals(const Scene& scene, const CameraView& cam) {
    // The 8 practical lights that matter most for this view: closest to the camera relative to their range.
    std::vector<std::pair<float, const SceneNode*>> ranked;
    for (const SceneNode* n : scene.practicalLights()) {
        const float d = length(transformPoint(n->world, {0, 0, 0}) - cam.position);
        ranked.push_back({d / std::max(n->material.lightRange, 0.1f), n});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    practicalCount_ = std::min(static_cast<int>(ranked.size()), kMaxPracticals);
    for (int i = 0; i < practicalCount_; ++i) {
        const SceneNode& n = *ranked[i].second;
        const Material& m = n.material;
        const Vec3 p = transformPoint(n.world, {0, 0, 0});
        const Vec3 dir = normalize(transformDirection(n.world, {0, 0, 1}));
        const float k = m.lightIntensity * clampf(m.emissive, 0.0f, 1.5f);
        const Vec3 c{std::pow(m.baseColor.x, 2.2f) * k, std::pow(m.baseColor.y, 2.2f) * k, std::pow(m.baseColor.z, 2.2f) * k};
        const float cone = m.lightConeDeg > 0.0f ? std::cos(radians(m.lightConeDeg)) : -2.0f;
        const float vals[3][4] = {{p.x, p.y, p.z, m.lightRange}, {c.x, c.y, c.z, 0.0f}, {dir.x, dir.y, dir.z, cone}};
        for (int j = 0; j < 4; ++j) {
            practicalPos_[4 * i + j] = vals[0][j];
            practicalColor_[4 * i + j] = vals[1][j];
            practicalDir_[4 * i + j] = vals[2][j];
        }
    }
}

void Renderer::setPracticalUniforms(Shader& sh, bool enabled) {
    sh.set("uPracticalCount", enabled ? practicalCount_ : 0);
    if (!enabled || practicalCount_ == 0) return;
    sh.setVec4Array("uPracticalPos", practicalPos_, practicalCount_);
    sh.setVec4Array("uPracticalColor", practicalColor_, practicalCount_);
    sh.setVec4Array("uPracticalDir", practicalDir_, practicalCount_);
}

void Renderer::bindLightingTextures() {
    glActiveTexture(GL_TEXTURE0 + kUnitMoonShadow);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadows_.moonTex);
    glActiveTexture(GL_TEXTURE0 + kUnitSpotShadow);
    glBindTexture(GL_TEXTURE_2D, shadows_.spotTex);
    glActiveTexture(GL_TEXTURE0 + kUnitAO);
    glBindTexture(GL_TEXTURE_2D, frame_.ao && fb_.ao[1].tex ? fb_.ao[1].tex : whiteTex_);
    glActiveTexture(GL_TEXTURE0 + kUnitGround);
    glBindTexture(GL_TEXTURE_2D, ground_.texture() ? ground_.texture() : whiteTex_);
    glActiveTexture(GL_TEXTURE0 + kUnitTrail);
    glBindTexture(GL_TEXTURE_2D, trailTex_ ? trailTex_ : whiteTex_);
    glActiveTexture(GL_TEXTURE0);
}

void Renderer::setShadowUniforms(Shader& sh, const RenderSettings& s) {
    const bool hdr = s.cinematic && s.view == ViewMode::Visible;
    sh.set("uShadows", hdr ? frame_.shadowMode : 0);
    sh.set("uUseAO", hdr && frame_.ao ? 1 : 0);
    sh.set("uVolumetric", hdr && frame_.fog ? 1 : 0);
    sh.set("uMoonVP0", frame_.moon[0].viewProj);
    sh.set("uMoonVP1", frame_.moon[1].viewProj);
    sh.set("uMoonTexel", frame_.moon[0].texelWorld, frame_.moon[1].texelWorld);
    sh.set("uCascadeSplit", frame_.split);
    sh.set("uSpotVP", frame_.spot.viewProj);
    sh.set("uPcfTaps", frame_.pcfTaps);
}

void Renderer::drawGrass(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                         FrameStats& st) {
    if (s.view == ViewMode::HeatRaw || s.view == ViewMode::Diagnostic || !s.isolate.empty()) return;
    grass_.setCount(qualityProfile(s.quality).grassBlades);
    if (grass_.count() <= 0) return;
    // The rover pushes blades aside; the rotor wash flattens them under a low UAV.
    const Vec3 uav = scene.drone.position;
    const float altitude = uav.y - terrainHeight(uav.x, uav.z);
    GrassField::Pusher rover{scene.rover.position, 0.45f, 1.2f};
    GrassField::Pusher wash{{uav.x, 0.0f, uav.z}, 2.4f, clampf(1.5f - 0.25f * altitude, 0.0f, 1.0f)};
    auto prepare = [&](Shader& sh) {
        setFrameUniforms(sh, scene, cam, w, h, s);
        sh.set("uBaseColor", primitiveColor(PrimitiveType::Cube));
        sh.set("uFogged", 1.0f);
        sh.set("uHeatVariation", 0.0f);
    };
    st.drawCalls += grass_.draw(cam.proj * cam.view, prepare, rover, wash, scene.time());
    st.grassBlades = grass_.count();
    st.triangles += static_cast<long long>(grass_.count()) * 12;
}

void Renderer::drawRain(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                        FrameStats& st) {
    // Visible light only: a long-wave IR camera does not see the streaks.
    if (s.rain <= 0.01f || s.view != ViewMode::Visible || !s.cinematic || !s.isolate.empty()) return;
    rain_.setCount(qualityProfile(s.quality).rainDrops);
    Shader& sh = rain_.shader();
    sh.use();
    setFrameUniforms(sh, scene, cam, w, h, s);
    sh.set("uTime", scene.time());
    sh.set("uBox", Vec3{24.0f, 14.0f, 24.0f});
    sh.set("uWind", Vec3{1.6f, 0.0f, 0.7f});
    sh.set("uFallSpeed", 7.5f);
    sh.set("uRainAmount", s.rain);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    rain_.draw();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    ++st.drawCalls;
    st.rainDrops = rain_.count();
    st.triangles += static_cast<long long>(rain_.count()) * 12;
}

void Renderer::beginFrame(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                          FrameStats& st) {
    QualityProfile q = qualityProfile(s.quality);
    if (!s.ssaoOn) q.ssao = false;
    if (!s.fogOn) q.volumetric = false;
    if (!s.msaaOn) q.msaa = 0;
    fb_.ensure(w, h, std::min(q.msaa, maxSamples_), q.fogDivisor);
    const bool visible = s.view == ViewMode::Visible;
    frame_.settings = s;
    frame_.pcfTaps = q.pcfTaps;
    frame_.cam = cam;
    frame_.time = scene.time();
    frame_.hdr = s.cinematic && visible && s.edges == EdgeMode::Solid;  // wireframe figures stay unprocessed
    frame_.thermalPost = s.cinematic && s.view == ViewMode::Thermal;
    frame_.shadowMode = 0;
    frame_.ao = frame_.fog = false;
    if (frame_.hdr) {
        gatherPracticals(scene, cam);
        shadows_.ensure(q.moonShadowRes, q.spotShadowRes);
        computeLightMatrices(scene, cam, q);
        if (s.shadowsOn) {
            frame_.shadowMode = (q.spotShadowRes > 0 && s.spotOn && s.spotShadowOn) ? 2 : 1;
            renderShadowMaps(scene, st);
        }
        if (q.ssao || q.volumetric) renderPrepass(scene, cam, s, st);
        if (q.ssao) {
            renderSsao(cam, q);
            frame_.ao = true;
        }
        if (q.volumetric) {
            renderFog(scene, cam, s, q);
            frame_.fog = true;
        }
    }
    timer_.begin("scene");
    fb_.bindScene();
    drawScene(scene, cam, w, h, s, st);
    timer_.end();
}

void Renderer::renderBloom() {
    if (fb_.bloom.empty()) return;
    glBindVertexArray(emptyVao_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    bloomDown_->use();
    bloomDown_->set("uSrc", 0);
    bloomDown_->set("uThreshold", frame_.hdr ? 1.2f : 0.85f);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = fb_.resolved.tex;
    for (size_t i = 0; i < fb_.bloom.size(); ++i) {
        fb_.bloom[i].bind();
        bloomDown_->set("uPrefilter", i == 0 ? 1 : 0);
        glBindTexture(GL_TEXTURE_2D, src);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        src = fb_.bloom[i].tex;
    }
    bloomUp_->use();
    bloomUp_->set("uSrc", 0);
    bloomUp_->set("uRadius", 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (size_t i = fb_.bloom.size() - 1; i > 0; --i) {
        fb_.bloom[i - 1].bind();
        glBindTexture(GL_TEXTURE_2D, fb_.bloom[i].tex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
}

void Renderer::endFrame(GLuint targetFbo, int w, int h, float time) {
    const RenderSettings& s = frame_.settings;
    timer_.begin("resolve + bloom");
    fb_.resolveScene();
    const bool post = (frame_.hdr || frame_.thermalPost) && s.bloomOn;
    if (post) renderBloom();
    timer_.end();

    timer_.begin("composite");
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(emptyVao_);
    composite_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_.resolved.tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fb_.bloom.empty() ? fb_.resolved.tex : fb_.bloom[0].tex);
    const bool bloomUsed = post && !fb_.bloom.empty();
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, fb_.fog.tex);
    glActiveTexture(GL_TEXTURE0);
    composite_->set("uScene", 0);
    composite_->set("uBloom", 1);
    composite_->set("uFog", 2);
    composite_->set("uMode", frame_.hdr ? 0 : (frame_.thermalPost ? 1 : 2));
    composite_->set("uUseFog", frame_.fog ? 1 : 0);
    composite_->set("uBloomStrength", !bloomUsed ? 0.0f : (frame_.hdr ? s.bloom * 0.12f : 0.5f));
    composite_->set("uExposure", s.exposure * 1.5f);
    composite_->set("uTime", time);
    composite_->set("uGrain", 0.03f);
    composite_->set("uVignette", 0.55f);
    composite_->set("uFade", s.fade);
    composite_->set("uLetterbox", s.letterbox);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_CULL_FACE);
    timer_.end();
    timer_.endFrame();
}

}  // namespace ts
