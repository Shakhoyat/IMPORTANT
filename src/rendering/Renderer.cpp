#include "rendering/Renderer.h"

#include "objects/BaseLayout.h"

#include "ai/DroneController.h"
#include "objects/Terrain.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/Sphere.h"
#include "rendering/ShaderSources.h"

namespace ts {

namespace {

// Linear-light colours of the v2 visible view (night sky irradiance, ground bounce, fog).
const Vec3 kSkyAmbient{0.030f, 0.040f, 0.075f};
const Vec3 kGroundBounce{0.010f, 0.010f, 0.011f};
const Vec3 kFogColor{0.014f, 0.018f, 0.030f};

}  // namespace

const char* viewModeName(ViewMode m) {
    switch (m) {
        case ViewMode::Thermal: return "THERMAL";
        case ViewMode::Visible: return "VISIBLE";
        case ViewMode::Diagnostic: return "PRIMITIVES";
        case ViewMode::Depth: return "Z-BUFFER";
        case ViewMode::HeatRaw: return "RAW HEAT";
    }
    return "?";
}

const char* edgeModeName(EdgeMode m) {
    switch (m) {
        case EdgeMode::Solid: return "SOLID";
        case EdgeMode::HiddenLine: return "HIDDEN-LINE";
        case EdgeMode::XRay: return "X-RAY WIRE";
    }
    return "?";
}

const char* overrideName(ShadingOverride s) {
    switch (s) {
        case ShadingOverride::PerObject: return "PER-OBJECT";
        case ShadingOverride::Flat: return "ALL FLAT";
        case ShadingOverride::Gouraud: return "ALL GOURAUD";
        case ShadingOverride::Phong: return "ALL PHONG";
    }
    return "?";
}

Vec3 primitiveColor(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::Cube: return {0.10f, 0.78f, 1.00f};      // cyan
        case PrimitiveType::Sphere: return {1.00f, 0.28f, 0.72f};    // magenta
        case PrimitiveType::Cylinder: return {1.00f, 0.74f, 0.12f};  // amber
        case PrimitiveType::BeamCone: return {1.00f, 0.55f, 0.10f};
        case PrimitiveType::TerrainDisc: return {0.30f, 0.42f, 0.33f};
        case PrimitiveType::Cone: return {0.45f, 1.00f, 0.35f};      // green
        default: return {1, 1, 1};
    }
}

bool Renderer::init() {
    using namespace glsl;
    flat_ = std::make_unique<Shader>();
    gouraud_ = std::make_unique<Shader>();
    phong_ = std::make_unique<Shader>();
    beam_ = std::make_unique<Shader>();
    line_ = std::make_unique<Shader>();
    sky_ = std::make_unique<Shader>();
    bool ok = flat_->build("flat", withLighting(kFlatVert), withLightingAndOutput(kFlatFrag), withLighting(kFlatGeom));
    ok = ok && gouraud_->build("gouraud", withLighting(kGouraudVert), withLightingAndOutput(kGouraudFrag));
    ok = ok && phong_->build("phong", withLighting(kPhongVert), withLightingAndOutput(kPhongFrag));
    ok = ok && beam_->build("beam", kBeamVert, kBeamFrag);
    ok = ok && line_->build("line", kLineVert, kLineFrag);
    ok = ok && sky_->build("sky", kSkyVert, withPalette(kSkyFrag));
    for (auto* p : {&depth_, &prepass_, &ssao_, &blur_, &fog_, &bloomDown_, &bloomUp_, &composite_})
        *p = std::make_unique<Shader>();
    ok = ok && depth_->build("depth", kDepthVert, kDepthFrag);
    ok = ok && prepass_->build("prepass", kPrepassVert, kPrepassFrag);
    ok = ok && ssao_->build("ssao", kFullscreenVert, withPalette(kSsaoFrag));
    ok = ok && blur_->build("ao-blur", kFullscreenVert, kBlurFrag);
    ok = ok && fog_->build("fog", kFullscreenVert, withPalette(kFogFrag));
    ok = ok && bloomDown_->build("bloom-down", kFullscreenVert, kBloomDownFrag);
    ok = ok && bloomUp_->build("bloom-up", kFullscreenVert, kBloomUpFrag);
    ok = ok && composite_->build("composite", kFullscreenVert, withPalette(kCompositeFrag));
    for (auto* p : {&combine_, &taa_, &reduce_, &adapt_, &flare_, &streak_, &dof_, &dofCombine_})
        *p = std::make_unique<Shader>();
    ok = ok && combine_->build("combine", kFullscreenVert, withPalette(kCombineFrag));
    ok = ok && taa_->build("taa", kFullscreenVert, withPalette(kTaaFrag));
    ok = ok && reduce_->build("reduce", kFullscreenVert, kReduceFrag);
    ok = ok && adapt_->build("adapt", kFullscreenVert, kAdaptFrag);
    ok = ok && flare_->build("flare", kFullscreenVert, kFlareFrag);
    ok = ok && streak_->build("streak", kFullscreenVert, kStreakFrag);
    ok = ok && dof_->build("dof", kFullscreenVert, withPalette(kDofFrag));
    ok = ok && dofCombine_->build("dof-combine", kFullscreenVert, withPalette(kDofCombineFrag));
    ok = ok && hud_.init();
    ok = ok && grass_.init();
    ok = ok && rain_.init();
#ifdef TS_HAS_FOLIAGE
    ok = ok && foliage_.init();
#endif
#ifdef TS_HAS_PARTICLES
    ok = ok && particles_.init();
#endif
#ifdef TS_HAS_EFFECTS
    ok = ok && effects_.init();
#endif
    if (!ok) return false;

    // Fixed texture units for the lighting samplers (different sampler types must never share a unit).
    for (Shader* sh : {flat_.get(), gouraud_.get(), phong_.get()}) {
        sh->use();
        sh->set("uMoonShadow", kUnitMoonShadow);
        sh->set("uSpotShadow", kUnitSpotShadow);
        sh->set("uAO", kUnitAO);
        sh->set("uGroundMap", kUnitGround);
        sh->set("uHeatTrail", kUnitTrail);
    }
    const unsigned char white = 255;
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    shadows_.ensure(1024, 1);
    GLint samples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    maxSamples_ = samples;

    // Each primitive is generated once and uploaded once; every component reuses it.
    const std::pair<PrimitiveType, MeshData> data[] = {
        {PrimitiveType::Cube, createCube()},
        {PrimitiveType::Sphere, createSphere(36, 18)},
        {PrimitiveType::Cylinder, createCylinder(36)},
        {PrimitiveType::BeamCone, createCylinder(36, 0.03f, 0.5f, false)},
        {PrimitiveType::TerrainDisc, createTerrainDisc()},
        {PrimitiveType::Cone, createCylinder(36, 0.5f, 0.0f, true)},  // lab cylinder with top radius 0
    };
    for (const auto& [type, mesh] : data) {
        meshes_[type] = std::make_unique<Mesh>();
        meshes_[type]->upload(mesh);
    }
    glGenVertexArrays(1, &emptyVao_);
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(sizeof(Vec3)));
    glBindVertexArray(0);
    return cvTarget_.create(kCvWidth, kCvHeight, 0, true);
}

bool Renderer::inFrustum(const SceneNode& n, const Frustum& f) {
    const Vec3 centre = transformPoint(n.world, {0, 0, 0});
    const float r = n.primitive == PrimitiveType::TerrainDisc ? kTerrainRadius + 2.0f : unitPrimitiveRadius(n.world);
    return f.intersects(centre, r);
}

void Renderer::uploadHeatTrail(const Scene& scene) {
    const HeatTrail& trail = scene.heatTrail();
    if (!trailTex_) {
        glGenTextures(1, &trailTex_);
        glBindTexture(GL_TEXTURE_2D, trailTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, HeatTrail::kSize, HeatTrail::kSize, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (trail.version() == trailVersion_) return;  // the UAV camera and the main view share one upload
    trailVersion_ = trail.version();
    glBindTexture(GL_TEXTURE_2D, trailTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, HeatTrail::kSize, HeatTrail::kSize, GL_RED, GL_FLOAT, trail.cells().data());
}

Shader& Renderer::programFor(const SceneNode& n, const RenderSettings& s) {
    ShadingModel m = n.shading;
    if (s.shading == ShadingOverride::Flat) m = ShadingModel::Flat;
    else if (s.shading == ShadingOverride::Gouraud) m = ShadingModel::Gouraud;
    else if (s.shading == ShadingOverride::Phong) m = ShadingModel::Phong;
    if (m == ShadingModel::Flat) return *flat_;
    if (m == ShadingModel::Gouraud) return *gouraud_;
    return *phong_;
}

void Renderer::setFrameUniforms(Shader& sh, const Scene& scene, const CameraView& cam, int w, int h,
                                const RenderSettings& s) {
    const LightRig& L = scene.lights;
    const bool diag = s.view == ViewMode::Diagnostic;
    sh.set("uView", cam.view);
    sh.set("uProj", cam.proj);
    sh.set("uCamPos", cam.position);
    sh.set("uAmbient", diag ? Vec3{0.38f, 0.38f, 0.40f} : L.ambient);
    sh.set("uMoonDir", L.moon.direction);
    const bool hdrView = s.cinematic && s.view == ViewMode::Visible;
    const float moonScale = hdrView ? 0.85f * (1.0f - 0.55f * s.rain) : (diag ? 1.1f : 1.0f);  // overcast in rain
    const Vec3 moonTint = hdrView ? Vec3{0.85f, 0.95f, 1.2f} : Vec3{1, 1, 1};  // cooler moonlight in the graded view
    sh.set("uMoonColor", mul(L.moon.color, moonTint) * (s.moonOn ? L.moon.intensity * moonScale : 0.0f));
    sh.set("uBeaconPos", L.beacon.position);
    sh.set("uBeaconColor", L.beacon.color * (s.beaconOn && !diag ? L.beacon.intensity : 0.0f));
    sh.set("uBeaconAtt", L.beacon.linear, L.beacon.quadratic);
    sh.set("uSpotPos", L.spot.position);
    sh.set("uSpotDir", L.spot.direction);
    sh.set("uSpotColor", L.spot.color * (s.spotOn && !diag ? L.spot.intensity : 0.0f));
    sh.set("uSpotCos", std::cos(radians(L.spot.innerDeg)), std::cos(radians(L.spot.outerDeg)));
    sh.set("uSpotAtt", L.spot.linear, L.spot.quadratic);
    sh.set("uViewMode", static_cast<int>(s.view));
    sh.set("uPalette", s.palette);
    const bool hdr = s.cinematic && s.view == ViewMode::Visible && s.edges == EdgeMode::Solid;
    sh.set("uHdr", hdr ? 1 : 0);
    sh.set("uPbr", hdr && s.pbr ? 1 : 0);
    sh.set("uSkyZenith", kSkyAmbient);
    sh.set("uGroundBounce", kGroundBounce);
    sh.set("uFogColor", kFogColor);
    sh.set("uEmissiveScale", hdr ? 8.0f : 1.0f);
    sh.set("uWorldRadius", layout::kBaseRadius);
    setShadowUniforms(sh, s);
    sh.set("uGroundExtent", GroundMap::extent());
    sh.set("uTrailExtent", HeatTrail::kHalfExtent);
    sh.set("uTrailHeat", HeatTrail::kMaxExtraHeat);
    sh.set("uWetness", scene.wetness);
    sh.set("uRain", s.rain);
    sh.set("uGroundDetail", qualityProfile(s.quality).groundDetail ? 1 : 0);
    setPracticalUniforms(sh, hdr && s.practicalsOn);
    setIndoorUniforms(sh);
    sh.set("uFogDensity", hdr ? 0.018f : 0.028f * 10.0f / kTerrainRadius);
    sh.set("uTime", scene.time());
    sh.set("uGrain", s.grain);
    sh.set("uResolution", static_cast<float>(w), static_cast<float>(h));
    sh.set("uNearFar", cam.nearZ, cam.farZ);
}

void Renderer::setObjectUniforms(Shader& sh, const SceneNode& n, const Mat4& world, const RenderSettings& s) {
    const bool diag = s.view == ViewMode::Diagnostic;
    const Material& m = n.material;
    sh.set("uModel", world);
    sh.set("uNormalMat", normalMatrix(world));
    sh.set("uBaseColor", diag ? primitiveColor(n.primitive) : m.baseColor);
    sh.set("uHeat", m.heat);
    const float overcast = n.name == "Moon" ? 1.0f - 0.95f * s.rain : 1.0f;  // clouds hide the moon in rain
    sh.set("uEmissive", diag ? 0.0f : m.emissive * overcast);
    sh.set("uSpecular", diag ? 0.25f : m.specular);
    sh.set("uShininess", m.shininess);
    sh.set("uRoughness", pbrRoughness(m));
    sh.set("uMetalness", diag ? 0.0f : m.metalness);
    sh.set("uSurface", diag ? 0 : static_cast<int>(m.surface));
    sh.set("uHeatVariation", n.primitive == PrimitiveType::TerrainDisc ? 0.11f : 0.015f);
    sh.set("uFogged", (m.fogged && !diag) ? 1.0f : 0.0f);
    sh.set("uOpacity", diag ? 1.0f : m.opacity);
    sh.set("uPartCentre", transformPoint(world, {0, 0, 0}));
    sh.set("uPartRadius", unitPrimitiveRadius(world) * 0.7071f);
}

void Renderer::drawSky(const CameraView& cam, int w, int h, const RenderSettings& s, float time) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    sky_->use();
    sky_->set("uInvViewProj", inverse(cam.proj * cam.view));
    sky_->set("uCamPos", cam.position);
    sky_->set("uViewMode", static_cast<int>(s.view));
    sky_->set("uPalette", s.palette);
    sky_->set("uRain", s.rain);
    sky_->set("uHdr", s.cinematic && s.view == ViewMode::Visible && s.edges == EdgeMode::Solid ? 1 : 0);
    sky_->set("uFogColor", kFogColor);
    sky_->set("uTime", time);
    sky_->set("uMoonDir", moonDir_);
    sky_->set("uResolution", static_cast<float>(w), static_cast<float>(h));
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
}

void Renderer::drawSolids(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                          FrameStats& st) {
    // Bucket the leaves by shader program so each program is bound (and its frame uniforms set) once.
    // Fixed order Gouraud (ground, props) -> Flat (bunker) -> Phong (vehicles): with the depth test
    // switched off this is a plain painter's order, which makes the missing hidden-surface removal visible.
    std::vector<std::pair<Shader*, std::vector<const SceneNode*>>> buckets = {
        {gouraud_.get(), {}}, {flat_.get(), {}}, {phong_.get(), {}}};
    const Frustum frustum(cam.proj * cam.view);
    auto bucketOf = [&buckets](Shader* sh) -> std::vector<const SceneNode*>& {
        for (auto& b : buckets)
            if (b.first == sh) return b.second;
        return buckets.back().second;
    };
    for (const SceneNode* n : scene.leaves()) {
        if (!n->visible || n->primitive == PrimitiveType::BeamCone) continue;
        if (!s.isolate.empty() && n->object != s.isolate) continue;
        // The gimbal camera hangs below the airframe, so the UAV's own parts are outside its view.
        if ((s.view == ViewMode::HeatRaw || s.hideUav) && n->object == "Autonomous UAV") continue;
        if (!inFrustum(*n, frustum)) continue;
        if (n->material.opacity < 1.0f && s.view != ViewMode::Diagnostic) continue;  // translucent pass
        bucketOf(&programFor(*n, s)).push_back(n);
    }
    for (auto& [program, nodes] : buckets) {
        if (nodes.empty()) continue;
        program->use();
        setFrameUniforms(*program, scene, cam, w, h, s);
        for (const SceneNode* n : nodes) {
            const Mesh& mesh = *meshes_.at(n->primitive);
            setObjectUniforms(*program, *n, n->world, s);
            mesh.bind(s.indexed);
            mesh.draw(s.indexed);
            ++st.drawCalls;
            ++st.primitives;
            st.triangles += mesh.indexCount() / 3;
            st.vertices += s.indexed ? mesh.vertexCount() : mesh.indexCount();
        }
    }
}

void Renderer::drawTranslucent(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                               FrameStats& st) {
    // Rotor blur discs: blended over the opaque scene, depth-tested but not written; visible light only.
    if (s.view != ViewMode::Visible || !s.isolate.empty() || s.hideUav) return;
    const Frustum frustum(cam.proj * cam.view);
    Shader* bound = nullptr;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    for (const SceneNode* n : scene.leaves()) {
        if (!n->visible || n->material.opacity >= 1.0f || !inFrustum(*n, frustum)) continue;
        Shader& sh = programFor(*n, s);
        if (&sh != bound) {
            sh.use();
            setFrameUniforms(sh, scene, cam, w, h, s);
            bound = &sh;
        }
        setObjectUniforms(sh, *n, n->world, s);
        const Mesh& mesh = *meshes_.at(n->primitive);
        mesh.bind(s.indexed);
        mesh.draw(s.indexed);
        ++st.drawCalls;
        ++st.primitives;
        st.triangles += mesh.indexCount() / 3;
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Renderer::drawBeam(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st) {
    if (!s.isolate.empty()) return;
    if (!s.spotOn || s.view != ViewMode::Visible || s.hideUav) return;   // an LWIR camera does not see the beam
    if (s.view == ViewMode::Visible && s.cinematic && frame_.fog) return;  // the volumetric pass draws the shaft
    const SceneNode& beam = *scene.uav().beam;
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    beam_->use();
    beam_->set("uView", cam.view);
    beam_->set("uProj", cam.proj);
    beam_->set("uModel", beam.world);
    beam_->set("uNormalMat", normalMatrix(beam.world));
    beam_->set("uCamPos", cam.position);
    // A thermal camera does not see visible light: the cone is only a faint cue in thermal view.
    const bool thermal = s.view == ViewMode::Thermal;
    beam_->set("uBeamColor", thermal ? Vec3{0.55f, 0.45f, 0.75f} : Vec3{1.0f, 0.93f, 0.78f});
    beam_->set("uBeamIntensity", thermal ? 0.10f : 0.30f);
    const Mesh& mesh = *meshes_.at(PrimitiveType::BeamCone);
    mesh.bind(s.indexed);
    mesh.draw(s.indexed);
    ++st.drawCalls;
    ++st.primitives;
    st.triangles += mesh.indexCount() / 3;
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void Renderer::drawWire(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st) {
    const bool xray = s.edges == EdgeMode::XRay;
    const bool diag = s.view == ViewMode::Diagnostic;
    line_->use();
    line_->set("uUseVertexColor", 0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_BLEND);
    if (xray) {  // every edge, including the ones hidden behind other surfaces
        glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDisable(GL_CULL_FACE);
    } else {     // hidden-line: depth-tested edges drawn on top of the (offset) solid surfaces
        glDepthFunc(GL_LEQUAL);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    const Mat4 vp = cam.proj * cam.view;
    const Frustum frustum(vp);
    for (const SceneNode* n : scene.leaves()) {
        if (!n->visible || n->primitive == PrimitiveType::BeamCone || n->name == "Moon") continue;
        if (!inFrustum(*n, frustum)) continue;
        if (!s.isolate.empty() && n->object != s.isolate) continue;
        const bool terrain = n->primitive == PrimitiveType::TerrainDisc;
        Vec3 c = primitiveColor(n->primitive);
        if (!xray) c = diag ? c * 0.35f : Vec3{0.85f, 0.95f, 1.0f};
        line_->set("uColor", c);
        line_->set("uAlpha", xray ? (terrain ? 0.10f : 0.55f) : (diag ? 0.9f : (terrain ? 0.18f : 0.45f)));
        line_->set("uMVP", vp * n->world);
        const Mesh& mesh = *meshes_.at(n->primitive);
        mesh.bind(s.indexed);
        mesh.draw(s.indexed);
        ++st.drawCalls;
    }
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    if (s.depthTest) glEnable(GL_DEPTH_TEST);
}

void Renderer::surfaceWrites(bool on) const {
    if (!mrt_) return;  // only the main scene target has the surface attachment
    const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    const GLenum colourOnly[2] = {GL_COLOR_ATTACHMENT0, GL_NONE};
    glDrawBuffers(2, on ? both : colourOnly);
}

void Renderer::drawScene(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                         FrameStats& st) {
    glClearColor(0, 0, 0, 1);
    surfaceWrites(true);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (mrt_) {
        const GLfloat neutral[4] = {0.5f, 1.0f, 0.5f, 0.0f};  // up normal, no reflection
        glClearBufferfv(GL_COLOR, 1, neutral);
    }
    surfaceWrites(false);
    moonDir_ = scene.lights.moon.direction;
    uploadHeatTrail(scene);
    if (!groundBuilt_) {  // needs the world matrices, so it is built on the first frame
        ground_.build(scene);
        grass_.setDensityMap(&ground_);
        groundBuilt_ = true;
    }
    drawSky(cam, w, h, s, scene.time());
    bindLightingTextures();

    if (s.depthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);  // counter-clockwise triangles are front faces (Lab-04 p.1)
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    const bool overlay = s.edges == EdgeMode::HiddenLine || s.view == ViewMode::Diagnostic;
    if (s.edges != EdgeMode::XRay) {
        if (overlay) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
        }
        surfaceWrites(true);
        drawSolids(scene, cam, w, h, s, st);
        glDisable(GL_POLYGON_OFFSET_FILL);
        if (s.edges == EdgeMode::Solid) {
            drawGrass(scene, cam, w, h, s, st);
            drawFoliage(scene, cam, w, h, s, st);
        }
        surfaceWrites(false);
        if (s.edges == EdgeMode::Solid) drawEffects(scene, cam, w, h, s, st, false);
        drawTranslucent(scene, cam, w, h, s, st);
        drawRain(scene, cam, w, h, s, st);
        drawParticles(scene, cam, w, h, s, st);
        if (s.edges == EdgeMode::Solid) drawEffects(scene, cam, w, h, s, st, true);
        drawBeam(scene, cam, s, st);
    }
    surfaceWrites(false);
    if (overlay || s.edges == EdgeMode::XRay) drawWire(scene, cam, s, st);
}

void Renderer::captureUavCamera(const Scene& scene, std::vector<uint8_t>& heat, FrameStats& stats) {
    timer_.begin("uav camera (CV)");
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    cvTarget_.bind();
    RenderSettings s;
    s.view = ViewMode::HeatRaw;
    s.grain = 0.0f;
    const bool mrt = mrt_;
    mrt_ = false;
    const CameraView cam = uavCameraView(scene, static_cast<float>(kCvWidth) / kCvHeight, kCvFovYDeg);
    drawScene(scene, cam, kCvWidth, kCvHeight, s, stats);
    cvTarget_.read(heat);
    mrt_ = mrt;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    timer_.end();
}

void Renderer::drawPrimitiveLegend(int x, int y, int size, float time, FrameStats& st, bool wire) {
    const PrimitiveType types[kLegendCount] = {PrimitiveType::Cube, PrimitiveType::Sphere, PrimitiveType::Cylinder,
                                               PrimitiveType::Cone};
    phong_->use();
    const Vec3 eye{0, 0.7f, 3.1f};
    phong_->set("uView", lookAt(eye, {0, 0, 0}, {0, 1, 0}));
    phong_->set("uProj", perspective(radians(35.0f), 1.0f, 0.1f, 10.0f));
    phong_->set("uCamPos", eye);
    phong_->set("uAmbient", Vec3{0.30f, 0.30f, 0.32f});
    phong_->set("uMoonDir", normalize(Vec3{-0.4f, -0.7f, -0.6f}));
    phong_->set("uMoonColor", Vec3{0.9f, 0.9f, 0.9f});
    phong_->set("uBeaconColor", Vec3{0, 0, 0});
    phong_->set("uSpotColor", Vec3{0, 0, 0});
    phong_->set("uViewMode", static_cast<int>(ViewMode::Diagnostic));
    phong_->set("uHdr", 0);
    phong_->set("uPbr", 0);
    phong_->set("uShadows", 0);
    phong_->set("uUseAO", 0);
    phong_->set("uPracticalCount", 0);
    phong_->set("uSurface", 0);
    phong_->set("uMetalness", 0.0f);
    bindLightingTextures();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    for (int i = 0; i < kLegendCount; ++i) {
        glViewport(x + i * size, y, size, size);
        glClear(GL_DEPTH_BUFFER_BIT);
        const Mat4 model = rotateY(time * 0.8f) * rotateX(radians(-25.0f));
        phong_->set("uModel", model);
        phong_->set("uNormalMat", normalMatrix(model));
        phong_->set("uBaseColor", primitiveColor(types[i]));
        phong_->set("uEmissive", 0.0f);
        phong_->set("uSpecular", 0.4f);
        phong_->set("uShininess", 32.0f);
        phong_->set("uFogged", 0.0f);
        const Mesh& mesh = *meshes_.at(types[i]);
        if (wire) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
        }
        mesh.bind(true);
        mesh.draw(true);
        ++st.drawCalls;
        glDisable(GL_POLYGON_OFFSET_FILL);
        if (wire) {  // triangle edges on top: shows the vertex/face structure of each primitive
            line_->use();
            line_->set("uUseVertexColor", 0);
            line_->set("uColor", primitiveColor(types[i]) * 0.3f);
            line_->set("uAlpha", 1.0f);
            line_->set("uMVP", perspective(radians(35.0f), 1.0f, 0.1f, 10.0f) * lookAt(eye, {0, 0, 0}, {0, 1, 0}) * model);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDepthFunc(GL_LEQUAL);
            mesh.draw(true);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDepthFunc(GL_LESS);
            phong_->use();
        }
    }
}

void Renderer::drawLines(const std::vector<LineVertex>& verts, GLenum mode, const Mat4& viewProj, float alpha,
                         bool depth) {
    if (verts.empty()) return;
    line_->use();
    line_->set("uMVP", viewProj);
    line_->set("uUseVertexColor", 1);
    line_->set("uAlpha", alpha);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (depth) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(LineVertex), verts.data(), GL_STREAM_DRAW);
    glPointSize(4.0f);
    glDrawArrays(mode, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

}  // namespace ts
