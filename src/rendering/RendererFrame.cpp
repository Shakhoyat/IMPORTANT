// The v2 frame: shadow maps, depth/normal pre-pass, SSAO / GTAO, contact shadows, volumetric fog, HDR scene, bloom and
// composite.
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "objects/BaseLayout.h"
#include "objects/Terrain.h"
#include "rendering/Renderer.h"
#include "rendering/SceneAccess.h"

namespace ts {

namespace {

constexpr float kShadowCasterMargin = 30.0f;  // metres behind the fitted sphere that may still cast shadows
constexpr float kFogMaxDistance = 70.0f;
// Mean log2 luminance of the reference night frame (establishing shot): auto-exposure maps it to the base
// exposure, so the designed look is kept and only darker / brighter views are corrected (+-1 EV).
// v6: the key of each frame comes from the scene's Atmosphere (DayCycle); this is the night value.
constexpr float kExposureKeyLog2 = -5.2f;
// v9 M8 aerial perspective (clear day): extinction 1/m at height 0 and the scale height of air + regional haze. 200 m of
// horizontal view keep ~75 % of the surface radiance, the far hills turn towards the sky colour.
constexpr float kAerialDensity = 0.0009f, kAerialHeight = 150.0f, kAerialMie = 0.25f;
// Contact shadows: march length (m) and strength.
constexpr float kContactLength = 0.6f, kContactStrength = 0.85f;

// v9 M8 ablation switch for the before / after figures, the exe and the benchmarks: TS_LIGHTING_OFF is a comma list of
// pcss, contact, gtao, aerial (e.g. TS_LIGHTING_OFF=contact,pcss shows the v8 shadows with the same build).
bool lightingOff(const char* feature) {
    const char* env = std::getenv("TS_LIGHTING_OFF");
    if (!env) return false;
    const std::string list = std::string(",") + env + ",";
    return list.find(std::string(",") + feature + ",") != std::string::npos;
}

}  // namespace

const char* qualityName(Quality q) {
    switch (q) {
        case Quality::Low: return "LOW";
        case Quality::High: return "HIGH";
        case Quality::Ultra: return "ULTRA";
        case Quality::Epic: return "EPIC";
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
        p.taa = false;
        p.flare = false;
        p.foliageDensity = 0.2f;
        p.fireflies = 60;
    } else if (q == Quality::Epic) {   // discrete GPU (RTX 4070 12 GB class)
        p.msaa = 4;                     // + TAA
        p.moonShadowRes = 4096;
        p.spotShadowRes = 2048;
        p.ssaoSamples = 16;
        p.pcfTaps = 16;
        p.moonShafts = true;
        p.grassBlades = 450000;
        p.rainDrops = 60000;
        p.fogSteps = 64;
        p.fogDivisor = 2;
        p.cascadeSplit = 14.0f;
        p.prepassDivisor = 1;
        p.ssr = true;
        p.ssrSteps = 64;
        p.dof = true;
        p.motionBlur = true;
        p.foliageDensity = 1.0f;
        p.fireflies = 700;
        p.pcss = true;             // v9 M8
        p.blockerTaps = 16;
        p.contactShadows = true;
        p.contactSteps = 16;
        p.gtao = true;
        p.gtaoSlices = 3;
        p.gtaoSteps = 8;
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
        p.ssr = true;
        p.dof = true;
        p.motionBlur = true;
        p.foliageDensity = 0.7f;
        p.fireflies = 400;
        p.pcss = true;             // v9 M8 (SSAO stays: GTAO is EPIC only)
        p.blockerTaps = 16;
        p.contactShadows = true;
        p.contactSteps = 12;
    } else {                       // HIGH
        p.pcss = true;
        p.blockerTaps = 8;
        p.contactShadows = true;
        p.contactSteps = 10;
    }
    return p;
}

void Renderer::computeLightMatrices(const Scene& scene, const CameraView& cam, const QualityProfile& q) {
    // Camera field of view and aspect ratio recovered from the projection matrix.
    const float fovY = 2.0f * std::atan(1.0f / cam.proj.at(1, 1));
    const float aspect = cam.proj.at(1, 1) / cam.proj.at(0, 0);
    frame_.split = q.cascadeSplit;
    const Sphere nearSlice = frustumSliceSphere(inverse(cam.view), fovY, aspect, cam.nearZ, q.cascadeSplit);
    const Sphere world{{0.0f, 1.5f, 0.0f}, layout::kBaseRadius + 3.0f};
    const Vec3 moonDir = scene.lights.moon.direction;
    frame_.moon[0] = fitDirectionalShadow(moonDir, nearSlice, q.moonShadowRes, kShadowCasterMargin);
    frame_.moon[1] = fitDirectionalShadow(moonDir, world, q.moonShadowRes, kShadowCasterMargin);
    const SpotLight& spot = scene.lights.spot;
    frame_.spot = spotShadowMatrix(spot.position, spot.direction, spot.outerDeg, 0.05f, 40.0f);
}

namespace {

bool isDynamic(const SceneNode& n) { return n.dynamic || n.object == "Autonomous UAV" || n.object == "Target Rover"; }
bool isSkyBody(const SceneNode& n) { return n.name == "Moon" || n.name == "Sun"; }

}  // namespace

void Renderer::drawDepthOnly(const Scene& scene, const Mat4& lightVP, Casters which, FrameStats& st) {
    depth_->set("uLightVP", lightVP);
    const Frustum frustum(lightVP);
    std::vector<const SceneNode*> casters;   // v7 WP-Q: instanced path
    const bool instanced = instancing_;
    for (const SceneNode* n : scene.leaves()) {
        if (!n->visible || n->primitive == PrimitiveType::BeamCone || isSkyBody(*n) || n->object == "Backdrop") continue;
        if (n->material.opacity < 1.0f) continue;  // blur discs cast no shadow
        if (!inFrustum(*n, frustum)) continue;
        if (which == Casters::AllButUav && n->object == "Autonomous UAV") continue;  // the lamp is inside it
        if (which == Casters::Static && isDynamic(*n)) continue;
        if (which == Casters::Dynamic && !isDynamic(*n)) continue;
        if (instanced) {
            casters.push_back(n);
            continue;
        }
        depth_->set("uModel", n->world);
        const Mesh& mesh = *meshes_.at(n->primitive);
        mesh.bind(true);
        mesh.draw(true);
        ++st.shadowDraws;
    }
    if (!instanced || casters.empty()) return;
    // Depth only: the draw order does not matter, so the casters are simply grouped by mesh.
    std::stable_sort(casters.begin(), casters.end(), [](const SceneNode* a, const SceneNode* b) {
        return static_cast<int>(a->primitive) < static_cast<int>(b->primitive);
    });
    if (uploadInstances(casters, 4, nullptr)) {
        depthI_->use();
        depthI_->set("uLightVP", lightVP);
        st.shadowDraws += drawInstanced(*depthI_, casters, nullptr);
    } else {   // too many parts for one buffer texture: per-part draws
        for (const SceneNode* n : casters) {
            depth_->use();
            depth_->set("uModel", n->world);
            meshes_.at(n->primitive)->bind(true);
            meshes_.at(n->primitive)->draw(true);
            ++st.shadowDraws;
        }
    }
    depth_->use();   // the callers keep drawing with the per-part depth program
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
#ifdef TS_HAS_FOLIAGE
    if (frame_.settings.foliageOn) st.shadowDraws += foliage_.drawDepth(frame_.moon[0].viewProj, frame_.time);
    depth_->use();
#endif
    // World cascade: its matrix never changes, so the static casters are rendered once and cached;
    // each frame the cache is copied in and only the moving UAV and rover are drawn on top.
    // The cache also depends on the foliage cards (on/off, density of the preset).
    const float cacheKey = scene.explode + (frame_.settings.foliageOn ? 10.0f * qualityProfile(frame_.settings.quality).foliageDensity : 0.0f);
    // v6: the key light moves by day (quantised to 1 deg by DayCycle), so its direction is part of the key.
    const Vec3 keyDir = scene.lights.moon.direction;
    const bool sameDir = keyDir.x == shadows_.staticDir.x && keyDir.y == shadows_.staticDir.y && keyDir.z == shadows_.staticDir.z;
    if (!shadows_.staticValid || shadows_.staticKey != cacheKey || !sameDir) {
        shadows_.bindStaticCache();
        glClear(GL_DEPTH_BUFFER_BIT);
        drawDepthOnly(scene, frame_.moon[1].viewProj, Casters::Static, st);
#ifdef TS_HAS_FOLIAGE
        if (frame_.settings.foliageOn) st.shadowDraws += foliage_.drawDepth(frame_.moon[1].viewProj, 0.0f);
        depth_->use();
#endif
        shadows_.staticValid = true;
        shadows_.staticKey = cacheKey;
        shadows_.staticDir = keyDir;
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
    std::vector<const SceneNode*> parts;   // v7 WP-Q: instanced path
    const bool instanced = instancing_;
    for (const SceneNode* n : scene.leaves()) {
        if (!n->visible || n->primitive == PrimitiveType::BeamCone || isSkyBody(*n) || n->material.opacity < 1.0f) continue;
        if (!inFrustum(*n, frustum)) continue;
        if (!s.isolate.empty() && n->object != s.isolate) continue;
        if (instanced) {
            parts.push_back(n);
            continue;
        }
        prepass_->set("uModel", n->world);
        prepass_->set("uNormalMat", normalMatrix(n->world));
        const Mesh& mesh = *meshes_.at(n->primitive);
        mesh.bind(true);
        mesh.draw(true);
        ++st.prepassDraws;
    }
    if (instanced && !parts.empty()) {
        std::stable_sort(parts.begin(), parts.end(), [](const SceneNode* a, const SceneNode* b) {
            return static_cast<int>(a->primitive) < static_cast<int>(b->primitive);
        });
        if (uploadInstances(parts, 7, nullptr)) {
            prepassI_->use();
            prepassI_->set("uView", cam.view);
            prepassI_->set("uProj", cam.proj);
            st.prepassDraws += drawInstanced(*prepassI_, parts, nullptr);
        } else {
            for (const SceneNode* n : parts) {
                prepass_->set("uModel", n->world);
                prepass_->set("uNormalMat", normalMatrix(n->world));
                meshes_.at(n->primitive)->bind(true);
                meshes_.at(n->primitive)->draw(true);
                ++st.prepassDraws;
            }
        }
    }
    timer_.end();
}

void Renderer::renderSsao(const CameraView& cam, const QualityProfile& q) {
    timer_.begin(frame_.gtao ? "gtao" : "ssao");
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(emptyVao_);
    fb_.ao[0].bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassDepth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassNormal);
    if (frame_.gtao) {   // v9 M8: horizon-based ground-truth AO (EPIC), rotated per frame for the TAA to accumulate
        gtao_->use();
        gtao_->set("uDepth", 0);
        gtao_->set("uNormal", 1);
        gtao_->set("uProj", cam.proj);
        gtao_->set("uInvProj", inverse(cam.proj));
        gtao_->set("uRadius", 0.8f);
        gtao_->set("uSlices", q.gtaoSlices);
        gtao_->set("uSteps", q.gtaoSteps);
        gtao_->set("uFrame", frame_.taa ? static_cast<float>(frameIndex_ % 64) : 0.0f);
        gtao_->set("uPower", 1.4f);
    } else {
        ssao_->use();
        ssao_->set("uDepth", 0);
        ssao_->set("uNormal", 1);
        ssao_->set("uProj", cam.proj);
        ssao_->set("uInvProj", inverse(cam.proj));
        ssao_->set("uRadius", 0.45f);
        ssao_->set("uSamples", q.ssaoSamples);
    }
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

// v9 M8: screen-space contact shadows of the sun (kContactFrag) into fb_.contact, read by moonShadow() in the scene.
void Renderer::renderContactShadows(const Scene& scene, const CameraView& cam, const QualityProfile& q) {
    timer_.begin("contact shadows");
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(emptyVao_);
    fb_.contact.bind();
    contact_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassDepth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fb_.prepassNormal);
    glActiveTexture(GL_TEXTURE0);
    contact_->set("uDepth", 0);
    contact_->set("uNormal", 1);
    contact_->set("uProj", cam.proj);
    contact_->set("uInvProj", inverse(cam.proj));
    contact_->set("uLightView", normalize(transformDirection(cam.view, scene.lights.moon.direction * -1.0f)));
    contact_->set("uLength", kContactLength);
    contact_->set("uSteps", q.contactSteps);
    contact_->set("uFrame", frame_.taa ? static_cast<float>(frameIndex_ % 64) : 0.0f);
    contact_->set("uStrength", kContactStrength * clampf(scene.lights.atmo.day * 1.5f, 0.0f, 1.0f));
    glDrawArrays(GL_TRIANGLES, 0, 3);
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
    // v9 M1: clear-day haze is thin and smooth (v8 cut it to 0.0013): half the march steps by day, and the lamp halo
    // loop is skipped once the halos have faded (uHaloScale below), ~1 ms at 1080p EPIC.
    fog_->set("uSteps", L.atmo.day > 0.5f ? std::max(16, q.fogSteps / 2) : q.fogSteps);
    fog_->set("uMaxDistance", kFogMaxDistance);
    fog_->set("uDensity", L.atmo.volFogDensity * (1.0f + 1.2f * s.rain));
    fog_->set("uHeightFalloff", 0.22f);
    fog_->set("uAmbientFog", L.atmo.fogAmbient);
    fog_->set("uHaloScale", 1.0f - 0.9f * L.atmo.day);
    fog_->set("uMoonDir", L.moon.direction);
    fog_->set("uMoonColor", L.moon.color * (s.moonOn && L.moon.enabled ? L.moon.intensity * 0.35f : 0.0f));
    fog_->set("uMoonVP1", frame_.moon[1].viewProj);
    fog_->set("uBeaconPos", L.beacon.position);
    fog_->set("uBeaconColor", L.beacon.color * (s.beaconOn && L.beacon.enabled ? L.beacon.intensity * 0.6f * L.atmo.practicalScale : 0.0f));
    fog_->set("uBeaconAtt", L.beacon.linear, L.beacon.quadratic);
    fog_->set("uSpotPos", L.spot.position);
    fog_->set("uSpotDir", L.spot.direction);
    fog_->set("uSpotColor", L.spot.color * (s.spotOn && L.spot.enabled ? L.spot.intensity * 6.0f : 0.0f));
    fog_->set("uSpotCos", std::cos(radians(L.spot.innerDeg)), std::cos(radians(L.spot.outerDeg)));
    fog_->set("uSpotAtt", L.spot.linear, L.spot.quadratic);
    fog_->set("uSpotVP", frame_.spot.viewProj);
    fog_->set("uSpotShadowOn", frame_.shadowMode == 2 ? 1 : 0);
    fog_->set("uMoonShafts", q.moonShafts && frame_.shadowMode > 0 ? 1 : 0);
    setPracticalUniforms(*fog_, s.practicalsOn);
    setIndoorUniforms(*fog_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    timer_.end();
}

void Renderer::gatherPracticals(const Scene& scene, const CameraView& cam) {
    // Indoor volumes of the underground bunker (lights on the other side of its walls are skipped).
    const WalkSpace* ws = sceneaccess::walkSpace(scene);
    indoorCount_ = 0;
    if (ws) {
        const std::vector<IndoorLight>& lights = ws->indoorLights();
        for (size_t v = 0; v < ws->indoorVolumes().size(); ++v) {
            if (indoorCount_ == 8) break;
            const Box& b = ws->indoorVolumes()[v];
            const float mn[3] = {b.min.x, b.min.y, b.min.z}, mx[3] = {b.max.x, b.max.y, b.max.z};
            for (int j = 0; j < 3; ++j) {
                indoorMin_[3 * indoorCount_ + j] = mn[j];
                indoorMax_[3 * indoorCount_ + j] = mx[j];
            }
            const IndoorLight l = v < lights.size() ? lights[v] : IndoorLight{};
            indoorLight_[2 * indoorCount_] = l.sky;
            indoorLight_[2 * indoorCount_ + 1] = l.key;
            ++indoorCount_;
        }
    }
    const bool camInside = sceneaccess::insideIndoor(ws, cam.position);
    frame_.cameraIndoor = camInside;

    // The 16 lights that matter most for this view: lamps and fires of the scene plus transient lights from
    // the combat simulation (blast flashes, burning wrecks, rocket motors), ranked by distance / range.
    // Lights on the other side of the bunker walls rank last.
    struct Candidate {
        float score;
        Vec3 pos, color, dir;
        float range, cone;
    };
    std::vector<Candidate> ranked;
    for (const SceneNode* n : scene.practicalLights()) {
        const Material& m = n->material;
        const float k = m.lightIntensity * clampf(m.emissive, 0.0f, 1.5f) * practicalFactor(*n);
        if (k <= 0.0f) continue;
        const Vec3 p = transformPoint(n->world, {0, 0, 0});
        const Vec3 c{std::pow(m.baseColor.x, 2.2f) * k, std::pow(m.baseColor.y, 2.2f) * k, std::pow(m.baseColor.z, 2.2f) * k};
        ranked.push_back({length(p - cam.position) / std::max(m.lightRange, 0.1f), p, c,
                          normalize(transformDirection(n->world, {0, 0, 1})), m.lightRange,
                          m.lightConeDeg > 0.0f ? std::cos(radians(m.lightConeDeg)) : -2.0f});
    }
    if (const std::vector<DynamicLight>* dyn = sceneaccess::dynamicLights(scene)) {
        for (const DynamicLight& l : *dyn) {
            // Blasts are brief and bright: rank them ahead of ordinary lamps at the same distance.
            ranked.push_back({0.5f * length(l.position - cam.position) / std::max(l.range, 0.1f), l.position, l.color,
                              l.direction, l.range, l.coneCos});
        }
    }
    for (Candidate& c : ranked) {
        if (sceneaccess::insideIndoor(ws, c.pos) != camInside) c.score += 4.0f;
    }
    std::sort(ranked.begin(), ranked.end(), [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
    practicalCount_ = std::min(static_cast<int>(ranked.size()), kMaxPracticals);
    for (int i = 0; i < practicalCount_; ++i) {
        const Candidate& c = ranked[i];
        const float inside = sceneaccess::insideIndoor(ws, c.pos) ? 1.0f : 0.0f;
        const float vals[3][4] = {{c.pos.x, c.pos.y, c.pos.z, c.range}, {c.color.x, c.color.y, c.color.z, inside},
                                  {c.dir.x, c.dir.y, c.dir.z, c.cone}};
        for (int j = 0; j < 4; ++j) {
            practicalPos_[4 * i + j] = vals[0][j];
            practicalColor_[4 * i + j] = vals[1][j];
            practicalDir_[4 * i + j] = vals[2][j];
        }
    }
}

float Renderer::practicalFactor(const SceneNode& n) const {
    if (n.material.lightIntensity <= 0.0f || atmo_.practicalScale >= 1.0f) return 1.0f;
    if (n.name == "Fire embers" || n.name == "Strobe" || n.name.rfind("Muzzle", 0) == 0) return 1.0f;
    if (sceneaccess::insideIndoor(walk_, transformPoint(n.world, {0, 0, 0}))) return 1.0f;   // bunker lights stay on
    return atmo_.practicalScale;
}

void Renderer::setIndoorUniforms(Shader& sh) const {
    sh.set("uIndoorCount", indoorCount_);
    if (indoorCount_ == 0) return;
    sh.setVec3Array("uIndoorMin", indoorMin_, indoorCount_);
    sh.setVec3Array("uIndoorMax", indoorMax_, indoorCount_);
    sh.setVec2Array("uIndoorLight", indoorLight_, indoorCount_);
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
    glActiveTexture(GL_TEXTURE0 + kUnitMoonDepth);   // v9 M8: the cascades as raw depth (sampler object, no compare)
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadows_.moonTex);
    glBindSampler(kUnitMoonDepth, moonDepthSampler_);
    glActiveTexture(GL_TEXTURE0 + kUnitContact);
    glBindTexture(GL_TEXTURE_2D, frame_.contact && fb_.contact.tex ? fb_.contact.tex : whiteTex_);
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
    // v9 M8: PCSS on the sun's cascades (frame_.pcss is off at night: the moon keeps the v8 fixed-radius PCF)
    sh.set("uPcss", hdr && frame_.pcss ? 1 : 0);
    sh.set("uBlockerTaps", frame_.blockerTaps);
    sh.set("uMoonDepthRange", frame_.moon[0].depthRange, frame_.moon[1].depthRange);
    sh.set("uLightTan", 2.0f * std::tan(0.5f * radians(kSunSourceDeg)));
    sh.set("uPcssSearch", pcssSearchRadius(kSunSourceDeg, kPcssMaxBlockerDistance));
    sh.set("uFrameNoise", frame_.taa ? 0.618034f * static_cast<float>(frameIndex_ % 16) : 0.0f);
    sh.set("uContactOn", hdr && frame_.contact ? 1 : 0);
}

void Renderer::drawGrass(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                         FrameStats& st) {
    if (s.view == ViewMode::HeatRaw || s.view == ViewMode::Diagnostic || !s.isolate.empty()) return;
    grass_.setCount(qualityProfile(s.quality).grassBlades);
    if (grass_.count() <= 0) return;
    // Rovers and vehicles push blades aside; rotor wash flattens them under a low UAV / helicopter. The shader
    // takes two pushers: the two closest to the camera.
    std::vector<GrassField::Pusher> pushers;
    const Vec3 uav = scene.drone.position;
    const float altitude = uav.y - terrainHeight(uav.x, uav.z);
    pushers.push_back({{uav.x, 0.0f, uav.z}, 2.4f, clampf(1.5f - 0.25f * altitude, 0.0f, 1.0f)});
    for (const auto& in : scene.intruders())
        if (in->life == Intruder::Life::Alive) pushers.push_back({in->state.position, 0.45f, 1.2f});
    for (const auto& c : scene.controllables()) {
        if (!c->possessable()) continue;
        const Vec3 p = c->position();
        const float agl = p.y - terrainHeight(p.x, p.z);
        const std::string name = c->name();
        if (name.find("TANK") != std::string::npos) pushers.push_back({p, 2.2f, 1.4f});
        else if (name.find("HELI") != std::string::npos && agl < 8.0f)
            pushers.push_back({{p.x, 0.0f, p.z}, 5.0f, clampf(1.6f - 0.18f * agl, 0.0f, 1.2f)});
    }
    std::sort(pushers.begin(), pushers.end(), [&](const GrassField::Pusher& a, const GrassField::Pusher& b) {
        return length(a.position - cam.position) - 3.0f * a.strength < length(b.position - cam.position) - 3.0f * b.strength;
    });
    while (pushers.size() < 2) pushers.push_back({{0, -100, 0}, 0.0f, 0.0f});
    const GrassField::Pusher rover = pushers[0], wash = pushers[1];
    auto prepare = [&](Shader& sh) {
        setFrameUniforms(sh, scene, cam, w, h, s);
        sh.set("uBaseColor", primitiveColor(PrimitiveType::Cube));
        sh.set("uFogged", 1.0f);
        sh.set("uHeatVariation", 0.0f);
    };
    st.drawCalls += grass_.draw(cam.proj * cam.view, cam.position, prepare, rover, wash, scene.time());
    st.grassBlades = grass_.submitted();
    st.triangles += static_cast<long long>(grass_.submitted()) * grass_.trianglesPerBlade();
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

namespace {

// Halton (2, 3) sub-pixel offsets in [-0.5, 0.5]: the TAA jitter sequence (8 frames).
float halton(unsigned i, unsigned base) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}

Vec3 viewForward(const Mat4& view) { return normalize(Vec3{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)}); }

}  // namespace

void Renderer::drawFoliage(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                           FrameStats& st) {
#ifdef TS_HAS_FOLIAGE
    if (!s.foliageOn || s.view == ViewMode::HeatRaw || s.view == ViewMode::Diagnostic || !s.isolate.empty()) return;
    const float density = qualityProfile(s.quality).foliageDensity;
    if (density <= 0.0f) return;
    foliage_.build(scene, density);
    auto prepare = [&](Shader& sh) {
        setFrameUniforms(sh, scene, cam, w, h, s);
        sh.set("uFogged", 1.0f);
        sh.set("uHeatVariation", 0.0f);
    };
    st.drawCalls += foliage_.draw(cam.proj * cam.view, prepare, scene.time());
#else
    (void)scene; (void)cam; (void)w; (void)h; (void)s; (void)st;
#endif
}

void Renderer::drawParticles(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                             FrameStats& st) {
#ifdef TS_HAS_PARTICLES
    // Fireflies stay hidden in rain; visible-light effect only.
    if (!s.particlesOn || s.view != ViewMode::Visible || !s.cinematic || !s.isolate.empty() || s.rain > 0.3f) return;
    if (atmo_.night < 0.5f) return;   // fireflies and glowing motes only after dusk
    particles_.setCount(qualityProfile(s.quality).fireflies);
    if (particles_.count() <= 0) return;
    auto prepare = [&](Shader& sh) { setFrameUniforms(sh, scene, cam, w, h, s); };
    st.drawCalls += particles_.draw(cam.proj * cam.view, cam.position, prepare, scene.time());
#else
    (void)scene; (void)cam; (void)w; (void)h; (void)s; (void)st;
#endif
}

void Renderer::drawEffects(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                           FrameStats& st, bool transparent) {
#ifdef TS_HAS_EFFECTS
    if (s.view == ViewMode::Depth || s.view == ViewMode::Diagnostic || !s.isolate.empty()) return;
    // Soft particles: the depth pre-pass (rendered for SSAO / fog on HIGH and above) is not attached to the
    // scene target, so sprites can read it to fade where they meet geometry.
    const bool softDepth = (frame_.ao || frame_.fog) && fb_.prepassDepth != 0;
    if (softDepth) {
        glActiveTexture(GL_TEXTURE0 + 10);
        glBindTexture(GL_TEXTURE_2D, fb_.prepassDepth);
        glActiveTexture(GL_TEXTURE0);
    }
    auto prepare = [&](Shader& sh) {
        setFrameUniforms(sh, scene, cam, w, h, s);
        sh.set("uHasSceneDepth", softDepth ? 1 : 0);
    };
    // Generic lambda: its body is only instantiated (and type-checked) when Scene provides combat().
    auto draw = [&](const auto& sc) {
        if constexpr (sceneaccess::HasCombat<std::decay_t<decltype(sc)>>::value) {
            const auto& combat = sc.combat();
            EffectsFrame f;
            f.particles = &combat.particles();
            f.projectiles = &combat.projectiles();
            f.scorches = &combat.scorches();
            const Mat4 vp = cam.proj * cam.view;
            st.drawCalls += transparent ? effects_.drawTransparent(f, vp, cam.position, prepare, sc.time())
                                        : effects_.drawOpaque(f, vp, cam.position, prepare, sc.time());
        }
    };
    draw(scene);
#else
    (void)scene; (void)cam; (void)w; (void)h; (void)s; (void)st; (void)transparent;
#endif
}

void Renderer::beginFrame(const Scene& scene, const CameraView& camIn, int w, int h, const RenderSettings& s,
                          FrameStats& st) {
    QualityProfile q = qualityProfile(s.quality);
    if (!s.ssaoOn) q.ssao = false;
    if (!s.fogOn) q.volumetric = false;
    if (!s.msaaOn) q.msaa = 0;
    if (lightingOff("pcss")) q.pcss = false;              // v9 M8 ablation (TS_LIGHTING_OFF)
    if (lightingOff("contact")) q.contactShadows = false;
    if (lightingOff("gtao")) q.gtao = false;
    fb_.ensure(w, h, std::min(q.msaa, maxSamples_), q.fogDivisor, q.prepassDivisor);
    const bool visible = s.view == ViewMode::Visible;
    frame_.settings = s;
    if (lightingOff("aerial")) frame_.settings.aerialOn = false;
    frame_.pcfTaps = q.pcfTaps;
    frame_.time = scene.time();
    atmo_ = scene.lights.atmo;
    walk_ = sceneaccess::walkSpace(scene);
    frame_.dt = prevTime_ >= 0.0f && scene.time() > prevTime_ ? std::min(scene.time() - prevTime_, 0.25f) : 1.0f / 60.0f;
    prevTime_ = scene.time();
    frame_.hdr = s.cinematic && visible && s.edges == EdgeMode::Solid;  // wireframe figures stay unprocessed
    frame_.thermalPost = s.view == ViewMode::Thermal && s.edges == EdgeMode::Solid;
    const bool temporal = frame_.hdr || frame_.thermalPost;
    frame_.taa = temporal && q.taa && s.taaOn;
    frame_.ssr = frame_.hdr && q.ssr && s.ssrOn;
    frame_.ssrSteps = q.ssrSteps;
    frame_.flare = frame_.hdr && q.flare && s.flareOn && s.bloomOn;
    frame_.dof = frame_.hdr && q.dof && s.dof > 0.0f;
    frame_.shadowMode = 0;
    frame_.ao = frame_.fog = false;
    // v9 M8: PCSS and GTAO are daylight features; the moonlit night keeps the v8 fixed PCF and SSAO (night parity:
    // the night was graded with them; SSIM against v8 / M1 in docs/v9_notes/m8/night_ssim.txt).
    const bool sunKey = scene.lights.atmo.keyIsSun && scene.lights.atmo.day > 0.0f;
    frame_.pcss = q.pcss && s.pcssOn && sunKey;
    frame_.blockerTaps = q.blockerTaps;
    frame_.gtao = q.gtao && s.gtaoOn && sunKey;
    frame_.contact = false;
    rtBegin(scene, camIn, w, h, s);   // v7 WP-H: ray-traced modes (no-op in raster mode)

    // Camera cut (director cut, camera mode switch, teleport): the history no longer matches the view.
    // A switch between the visible HDR image and the thermal sensor (UAV FPV -> gimbal) is a cut too: the adapted
    // statistics hold log luminance in one mode and heat in the other, and the TAA history holds the other image.
    const int postMode = frame_.hdr ? 0 : (frame_.thermalPost ? 1 : 2);
    if (postMode != prevPostMode_) historyValid_ = adaptValid_ = false;
    prevPostMode_ = postMode;
    const Vec3 fwd = viewForward(camIn.view);
    if (length(camIn.position - prevCamPos_) > 2.5f || dot(fwd, prevCamFwd_) < 0.85f) historyValid_ = false;
    prevCamPos_ = camIn.position;
    prevCamFwd_ = fwd;

    // TAA: sub-pixel jitter of the projection (Halton 2,3); every pass that fills the depth buffer uses it.
    CameraView cam = camIn;
    if (frame_.taa) {
        const unsigned k = (frameIndex_ % 8) + 1;
        const float jx = (halton(k, 2) - 0.5f) * 2.0f / static_cast<float>(w);
        const float jy = (halton(k, 3) - 0.5f) * 2.0f / static_cast<float>(h);
        cam.proj.at(0, 2) += jx;
        cam.proj.at(1, 2) += jy;
        // A point at view depth z moves by (jx, jy) * z / -z... in NDC: x_ndc += -jx (w = -z); in UV: -j / 2.
        frame_.jitterUV[0] = -0.5f * jx;
        frame_.jitterUV[1] = -0.5f * jy;
    } else {
        frame_.jitterUV[0] = frame_.jitterUV[1] = 0.0f;
    }
    ++frameIndex_;
    frame_.cam = cam;
    specLodEye_ = cam.position;   // v8 specular LOD (metres per pixel at 1 m from the projection)
    specLodPixel_ = frame_.hdr ? 2.0f / std::max(std::fabs(camIn.proj.at(1, 1)) * static_cast<float>(h), 1e-3f) : 0.0f;
    frame_.viewProj = camIn.proj * camIn.view;
    frame_.viewProjJ = cam.proj * cam.view;

    if (frame_.hdr && !skyOnly_) {   // v7 WP-H: a fully ray-traced frame needs only the raster sky
        gatherPracticals(scene, cam);
        shadows_.ensure(q.moonShadowRes, q.spotShadowRes);
        computeLightMatrices(scene, cam, q);
        if (s.shadowsOn && rtActive_ == rt::RtMode::Raster) {   // v7 WP-H: hybrid / RT trace their shadows
            frame_.shadowMode = (q.spotShadowRes > 0 && s.spotOn && scene.lights.spot.enabled && s.spotShadowOn) ? 2 : 1;
            renderShadowMaps(scene, st);
        }
        // v9 M8: contact shadows of the sun need the pre-pass depth too (off at night: the moon keeps the v8 look)
        const bool contact = q.contactShadows && s.contactShadowsOn && s.shadowsOn && scene.lights.atmo.keyIsSun &&
                             scene.lights.atmo.day > 0.0f;
        if (q.ssao || q.volumetric || contact) renderPrepass(scene, cam, s, st);
        if (q.ssao) {
            renderSsao(cam, q);
            frame_.ao = true;
        }
        if (contact) {
            renderContactShadows(scene, cam, q);
            frame_.contact = true;
        }
        if (q.volumetric) {
            renderFog(scene, cam, s, q);
            frame_.fog = true;
        }
    }
    timer_.begin("scene");
    fb_.bindScene(true);
    mrt_ = true;
    drawScene(scene, cam, w, h, s, st);
    timer_.end();
}

void Renderer::renderBloom(GLuint source) {
    if (fb_.bloom.empty()) return;
    glBindVertexArray(emptyVao_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    bloomDown_->use();
    bloomDown_->set("uSrc", 0);
    // The threshold follows the exposure key (night key -5.2 -> 1.2): a sunlit wall must not bloom.
    bloomDown_->set("uThreshold", frame_.hdr ? 1.2f * std::exp2(atmo_.exposureKeyLog2 - kExposureKeyLog2) : 0.85f);
    glActiveTexture(GL_TEXTURE0);
    GLuint src = source;
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

namespace {

void bindTex(int unit, GLuint tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

}  // namespace

GLuint Renderer::renderCombine() {
    timer_.begin("combine + ssr");
    fb_.lit.bind();
    combine_->use();
    bindTex(0, fb_.resolved.tex);
    bindTex(1, fb_.fog.tex);
    bindTex(2, fb_.surface.tex);
    bindTex(3, fb_.depthTex);
    combine_->set("uScene", 0);
    combine_->set("uFog", 1);
    combine_->set("uSurface", 2);
    combine_->set("uDepth", 3);
    combine_->set("uMode", frame_.hdr ? 0 : (frame_.thermalPost ? 1 : 2));
    combine_->set("uUseFog", frame_.fog ? 1 : 0);
    combine_->set("uSsr", frame_.ssr ? 1 : 0);
    combine_->set("uSsrSteps", frame_.ssrSteps);
    combine_->set("uViewProj", frame_.viewProjJ);
    combine_->set("uInvViewProj", inverse(frame_.viewProjJ));
    combine_->set("uCamPos", frame_.cam.position);
    combine_->set("uNearFar", frame_.cam.nearZ, frame_.cam.farZ);
    combine_->set("uTime", frame_.time);
    // v9 M8 aerial perspective: by day only (the night keeps its own fog look exactly)
    setSkyUniforms(*combine_);
    const bool aerial = frame_.hdr && frame_.settings.aerialOn;
    combine_->set("uAerialDensity", aerial ? kAerialDensity * atmo_.day * (1.0f + 1.5f * frame_.settings.rain) : 0.0f);
    combine_->set("uAerialHeight", kAerialHeight);
    combine_->set("uAerialMie", kAerialMie);
    combine_->set("uSunColor", atmo_.sunColor);
    combine_->set("uHazeRef", atmo_.fogColor);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    timer_.end();
    return fb_.lit.tex;
}

GLuint Renderer::renderTaa(GLuint current) {
    if (!frame_.taa) {
        historyValid_ = false;
        return current;
    }
    timer_.begin("taa");
    const int out = historyIndex_ ^ 1;
    fb_.history[out].bind();
    taa_->use();
    bindTex(0, current);
    bindTex(1, fb_.history[historyIndex_].tex);
    bindTex(2, fb_.depthTex);
    taa_->set("uCurrent", 0);
    taa_->set("uHistory", 1);
    taa_->set("uDepth", 2);
    taa_->set("uReproject", prevViewProj_ * inverse(frame_.viewProjJ));
    taa_->set("uJitterUV", frame_.jitterUV[0], frame_.jitterUV[1]);
    taa_->set("uFeedback", 0.9f);
    taa_->set("uReset", historyValid_ ? 0 : 1);
    taa_->set("uNearFar", frame_.cam.nearZ, frame_.cam.farZ);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    historyIndex_ = out;
    historyValid_ = true;
    timer_.end();
    return fb_.history[out].tex;
}

void Renderer::renderStatistics(GLuint image) {
    timer_.begin("exposure / agc");
    reduce_->use();
    reduce_->set("uSrc", 0);
    reduce_->set("uHeat", frame_.thermalPost ? 1 : 0);
    reduce_->set("uLetterbox", frame_.settings.letterbox);
    GLuint src = image;
    for (size_t i = 0; i < fb_.reduce.size(); ++i) {
        fb_.reduce[i].bind();
        reduce_->set("uMode", i == 0 ? 0 : 1);
        bindTex(0, src);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        src = fb_.reduce[i].tex;
    }
    // Eye / sensor adaptation: about 1.5 s for the exposure, faster (0.5 s) for the thermal AGC.
    const int out = adaptIndex_ ^ 1;
    fb_.adapt[out].bind();
    adapt_->use();
    bindTex(0, src);
    bindTex(1, fb_.adapt[adaptIndex_].tex);
    adapt_->set("uCurrent", 0);
    adapt_->set("uPrevious", 1);
    const float speed = frame_.thermalPost ? 2.0f : 0.7f;
    adapt_->set("uRate", adaptValid_ ? 1.0f - std::exp(-frame_.dt * speed) : 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    adaptIndex_ = out;
    adaptValid_ = true;
    timer_.end();
}

void Renderer::renderFlare() {
    if (fb_.bloom.size() < 2) return;
    timer_.begin("lens flare");
    fb_.flare.bind();
    flare_->use();
    bindTex(0, fb_.bloom[1].tex);
    flare_->set("uSrc", 0);
    flare_->set("uThreshold", 0.35f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    fb_.streak.bind();
    streak_->use();
    bindTex(0, fb_.bloom[0].tex);
    streak_->set("uSrc", 0);
    streak_->set("uThreshold", 0.6f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    timer_.end();
}

GLuint Renderer::renderDof(GLuint image) {
    timer_.begin("depth of field");
    const RenderSettings& s = frame_.settings;
    // Circle of confusion of an object at infinity, in pixels: scales with the frame height and strength.
    const float apertureFull = 0.012f * static_cast<float>(fb_.height()) * s.dof;
    fb_.dof.bind();
    dof_->use();
    bindTex(0, image);
    bindTex(1, fb_.depthTex);
    dof_->set("uSrc", 0);
    dof_->set("uDepth", 1);
    dof_->set("uNearFar", frame_.cam.nearZ, frame_.cam.farZ);
    dof_->set("uFocus", s.focusDistance);
    dof_->set("uAperture", apertureFull * 0.5f);
    dof_->set("uMaxCoc", std::max(2.0f, apertureFull * 0.5f));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    fb_.dofCombined.bind();
    dofCombine_->use();
    bindTex(0, image);
    bindTex(1, fb_.dof.tex);
    bindTex(2, fb_.depthTex);
    dofCombine_->set("uSharp", 0);
    dofCombine_->set("uBlur", 1);
    dofCombine_->set("uDepth", 2);
    dofCombine_->set("uNearFar", frame_.cam.nearZ, frame_.cam.farZ);
    dofCombine_->set("uFocus", s.focusDistance);
    dofCombine_->set("uAperture", apertureFull);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    timer_.end();
    return fb_.dofCombined.tex;
}

void Renderer::adaptedStats(float out[4]) const {
    glBindTexture(GL_TEXTURE_2D, fb_.adapt[adaptIndex_].tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, out);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::endFrame(GLuint targetFbo, int w, int h, float time) {
    const RenderSettings& s = frame_.settings;
    mrt_ = false;
    timer_.begin("resolve");
    fb_.resolveScene();
    timer_.end();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(emptyVao_);

    GLuint image = rtActive_ == rt::RtMode::Raster ? renderCombine() : rtEnd();   // v7 WP-H
    image = renderTaa(image);
    const bool temporal = frame_.hdr || frame_.thermalPost;
    if (temporal) renderStatistics(image);
    const bool bloom = frame_.hdr && s.bloomOn;
    timer_.begin("bloom");
    if (bloom) renderBloom(image);
    timer_.end();
    if (frame_.cameraIndoor) frame_.flare = false;
    if (frame_.flare) renderFlare();
    if (frame_.dof) image = renderDof(image);

    timer_.begin("composite");
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, w, h);
    glBindVertexArray(emptyVao_);
    composite_->use();
    bindTex(0, image);
    bindTex(1, fb_.bloom.empty() ? whiteTex_ : fb_.bloom[0].tex);
    bindTex(2, fb_.flare.tex);
    bindTex(3, fb_.streak.tex);
    bindTex(4, fb_.adapt[adaptIndex_].tex);
    bindTex(5, fb_.depthTex);
    composite_->set("uScene", 0);
    composite_->set("uBloom", 1);
    composite_->set("uFlare", 2);
    composite_->set("uStreak", 3);
    composite_->set("uStats", 4);
    composite_->set("uDepth", 5);
    composite_->set("uMode", frame_.hdr ? 0 : (frame_.thermalPost ? 1 : 2));
    composite_->set("uPalette", s.palette);
    // v9 M8 bloom / flare by day: AgX rolls the highlights off itself, so a lighter veil (the night keeps the v8 values)
    composite_->set("uBloomStrength", bloom && !fb_.bloom.empty() ? s.bloom * 0.12f * lerpf(1.0f, 0.6f, atmo_.day) : 0.0f);
    composite_->set("uFlareStrength", frame_.flare ? 0.05f * lerpf(1.0f, 0.4f, atmo_.day) : 0.0f);
    // v9 M8 display transform: AgX by default, ACES as the option (RenderSettings::toneMap, or env TS_TONEMAP=aces|agx)
    tonemap::Operator op = s.toneMap;
    if (const char* env = std::getenv("TS_TONEMAP")) {
        if (std::strcmp(env, "aces") == 0) op = tonemap::Operator::Aces;
        else if (std::strcmp(env, "agx") == 0) op = tonemap::Operator::AgX;
    }
    composite_->set("uToneMap", op == tonemap::Operator::Aces ? 1 : 0);
    // AgX by day, the v8 ACES curve at night (the night grade was authored for its toe), cross-faded through twilight.
    const float agxW = op == tonemap::Operator::AgX ? tonemap::agxDayWeight(atmo_.day) : 0.0f;
    composite_->set("uDayContrast", tonemap::dayContrast(agxW));
    composite_->set("uDaySaturation", tonemap::daySaturation(agxW));
    composite_->set("uAgxWeight", agxW);
    composite_->set("uAgxExposure", tonemap::kAgxExposure);
    composite_->set("uAcesExposure", tonemap::kAcesExposure);
    composite_->set("uExposure", s.exposure * 1.5f);
    composite_->set("uAutoExposure", s.autoExposureOn && adaptValid_ ? 1 : 0);
    composite_->set("uKeyLog2", atmo_.exposureKeyLog2);
    composite_->set("uPurkinje", atmo_.purkinje);
    composite_->set("uDayGrade", atmo_.day);
    composite_->set("uEvRange", 1.5f);
    composite_->set("uTime", time);
    composite_->set("uGrain", 0.03f);
    composite_->set("uVignette", 0.55f);
    composite_->set("uFade", s.fade);
    composite_->set("uLetterbox", s.letterbox);
    const bool blur = frame_.hdr && s.motionBlur > 0.0f && qualityProfile(s.quality).motionBlur && historyValid_;
    composite_->set("uMotionBlur", blur ? s.motionBlur : 0.0f);
    composite_->set("uReproject", prevViewProj_ * inverse(frame_.viewProjJ));
    composite_->set("uNearFar", frame_.cam.nearZ, frame_.cam.farZ);
    composite_->set("uShutter", s.nucShutter);
    composite_->set("uSensorNoise", 0.035f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_CULL_FACE);
    timer_.end();
    drawRayOverlay(targetFbo, w, h);   // v7 WP-H: ray debugger tree + RT status line (nothing in plain raster mode)
    timer_.endFrame();
    prevViewProj_ = frame_.viewProj;  // unjittered: the TAA history is kept on the unjittered pixel grid
}

}  // namespace ts
