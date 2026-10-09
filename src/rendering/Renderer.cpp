#include "rendering/Renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "objects/BaseLayout.h"

#include "ai/DroneController.h"
#include "objects/Terrain.h"
#include "objects/Uav.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/Sphere.h"
#include "rendering/RayTracerGL.h"
#include "rendering/SceneAccess.h"
#include "rendering/ShaderSources.h"

namespace ts {

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
    gtao_ = std::make_unique<Shader>();      // v9 M8
    contact_ = std::make_unique<Shader>();
    ok = ok && gtao_->build("gtao", kFullscreenVert, withPalette(kGtaoFrag));
    ok = ok && contact_->build("contact-shadows", kFullscreenVert, withPalette(kContactFrag));
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
    // v7 WP-Q: instanced variants of the part programs (one draw per mesh instead of one per part). Optional: if the
    // buffer textures or the programs are not available the per-part path is used (also with TS_NO_INSTANCING=1).
    const char* noInst = std::getenv("TS_NO_INSTANCING");
    if (!(noInst && *noInst == '1') && instances_.init()) {
        for (auto* p : {&flatI_, &gouraudI_, &phongI_, &depthI_, &prepassI_}) *p = std::make_unique<Shader>();
        const InstancedSources fl = instancedSources(ShadingKind::Flat), go = instancedSources(ShadingKind::Gouraud),
                               ph = instancedSources(ShadingKind::Phong);
        bool inst = flatI_->build("flat-instanced", fl.vert, fl.frag, fl.geom);
        inst = inst && gouraudI_->build("gouraud-instanced", go.vert, go.frag);
        inst = inst && phongI_->build("phong-instanced", ph.vert, ph.frag);
        inst = inst && depthI_->build("depth-instanced", kDepthVertI, kDepthFrag);
        inst = inst && prepassI_->build("prepass-instanced", kPrepassVertI, kPrepassFrag);
        instancing_ = inst;
        if (!inst) std::fprintf(stderr, "Instanced part programs unavailable: one draw per part\n");
    }
    rt_->init();   // v7 WP-H: false = no compute shaders (CPU fallback for the ray-traced modes), never fatal

    // Fixed texture units for the lighting samplers (different sampler types must never share a unit).
    for (Shader* sh : {flat_.get(), gouraud_.get(), phong_.get(), flatI_.get(), gouraudI_.get(), phongI_.get()}) {
        if (!sh) continue;
        sh->use();
        sh->set("uMoonShadow", kUnitMoonShadow);
        sh->set("uSpotShadow", kUnitSpotShadow);
        sh->set("uAO", kUnitAO);
        sh->set("uGroundMap", kUnitGround);
        sh->set("uHeatTrail", kUnitTrail);
    }
    if (instancing_)
        for (Shader* sh : {flatI_.get(), gouraudI_.get(), phongI_.get(), depthI_.get(), prepassI_.get()}) {
            sh->use();
            sh->set("uInstances", kUnitInstances);
        }
    const unsigned char white = 255;
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    shadows_.ensure(1024, 1);
    // v9 M8: a sampler object reads the same cascade texture as raw depth (no comparison) for the PCSS blocker search.
    glGenSamplers(1, &moonDepthSampler_);
    glSamplerParameteri(moonDepthSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(moonDepthSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(moonDepthSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(moonDepthSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(moonDepthSampler_, GL_TEXTURE_COMPARE_MODE, GL_NONE);
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
    const Atmosphere& A = L.atmo;
    const bool hdrView = s.cinematic && s.view == ViewMode::Visible;
    const float moonScale = hdrView ? 0.85f * (1.0f - 0.55f * s.rain) : (diag ? 1.1f : 1.0f);  // overcast in rain
    // Cooler moonlight in the graded view (the sun keeps its own air-mass colour).
    const Vec3 moonTint = hdrView && !A.keyIsSun ? Vec3{0.85f, 0.95f, 1.2f} : Vec3{1, 1, 1};
    sh.set("uMoonColor", mul(L.moon.color, moonTint) * (s.moonOn && L.moon.enabled ? L.moon.intensity * moonScale : 0.0f));
    sh.set("uKeyIsSun", A.keyIsSun ? 1 : 0);
    sh.set("uSolar", A.solar);
    sh.set("uCloudShadowOn", A.keyIsSun && !diag ? 1.0f : 0.0f);
    sh.set("uDayWeight", A.day);
    sh.set("uBeaconPos", L.beacon.position);
    const bool beacon = s.beaconOn && L.beacon.enabled && !diag;
    sh.set("uBeaconColor", L.beacon.color * (beacon ? L.beacon.intensity * A.practicalScale : 0.0f));
    sh.set("uBeaconAtt", L.beacon.linear, L.beacon.quadratic);
    sh.set("uSpotPos", L.spot.position);
    sh.set("uSpotDir", L.spot.direction);
    sh.set("uSpotColor", L.spot.color * (s.spotOn && L.spot.enabled && !diag ? L.spot.intensity : 0.0f));
    sh.set("uSpotCos", std::cos(radians(L.spot.innerDeg)), std::cos(radians(L.spot.outerDeg)));
    sh.set("uSpotAtt", L.spot.linear, L.spot.quadratic);
    sh.set("uViewMode", static_cast<int>(s.view));
    sh.set("uPalette", s.palette);
    const bool hdr = s.cinematic && s.view == ViewMode::Visible && s.edges == EdgeMode::Solid;
    sh.set("uHdr", hdr ? 1 : 0);
    sh.set("uPbr", hdr && s.pbr ? 1 : 0);
    sh.set("uSkyZenith", A.skyZenith);
    sh.set("uGroundBounce", A.groundBounce);
    sh.set("uSkyHorizon", A.skyHorizon);
    setSkyUniforms(sh);
    sh.set("uFogColor", A.fogColor);
    sh.set("uEmissiveScale", hdr ? 8.0f : 1.0f);
    sh.set("uWorldRadius", layout::kBaseRadius);
    setShadowUniforms(sh, s);
    sh.set("uGroundExtent", GroundMap::extent());
    sh.set("uTrailExtent", HeatTrail::kHalfExtent);
    sh.set("uTrailHeat", HeatTrail::kMaxExtraHeat);
    sh.set("uWetness", scene.wetness);
    sh.set("uLightTerms", s.lightTerms);   // v10 teaching view
    sh.set("uRain", s.rain);
    sh.set("uGroundDetail", qualityProfile(s.quality).groundDetail ? 1 : 0);
    sh.set("uBumpOff", s.bumpOn ? 0 : 1);
    setPracticalUniforms(sh, hdr && s.practicalsOn);
    setIndoorUniforms(sh);
    sh.set("uFogDensity", hdr ? A.fogDensity : 0.028f * 10.0f / kTerrainRadius);
    sh.set("uTime", scene.time());
    sh.set("uGrain", s.grain);
    sh.set("uResolution", static_cast<float>(w), static_cast<float>(h));
    sh.set("uNearFar", cam.nearZ, cam.farZ);
}

Renderer::ObjectParams Renderer::objectParams(const SceneNode& n, const Mat4& world, const RenderSettings& s) const {
    const bool diag = s.view == ViewMode::Diagnostic;
    const Material& m = n.material;
    ObjectParams p;
    p.model = world;
    p.normal = normalMatrix(world);
    p.baseColor = diag ? primitiveColor(n.primitive) : m.baseColor;
    p.heat = m.heat;
    // Clouds hide the moon / sun in rain; the moon glows by night, the sun by day; outdoor lamps go off by day.
    float emissive = m.emissive;
    if (n.name == "Moon") emissive = m.emissive * (1.0f - 0.95f * s.rain) * atmo_.night;
    else if (n.name == "Sun") emissive = m.emissive * (1.0f - 0.95f * s.rain) * atmo_.day;
    else if (m.lightIntensity > 0.0f) emissive = m.emissive * practicalFactor(n);
    p.emissive = diag ? 0.0f : emissive;
    p.specular = diag ? 0.25f : m.specular;
    p.shininess = m.shininess;
    p.roughness = specularLod(n, world, pbrRoughness(m));
    p.metalness = diag ? 0.0f : m.metalness;
    p.surface = diag ? 0 : static_cast<int>(m.surface);
    p.heatVariation = n.primitive == PrimitiveType::TerrainDisc ? 0.11f : 0.015f;
    p.fogged = (m.fogged && !diag) ? 1.0f : 0.0f;
    p.opacity = diag ? 1.0f : m.opacity;
    p.centre = transformPoint(world, {0, 0, 0});
    p.radius = unitPrimitiveRadius(world) * 0.7071f;
    return p;
}

float Renderer::specularLod(const SceneNode& n, const Mat4& world, float roughness) const {
    if (specLodPixel_ <= 0.0f || roughness >= 0.85f) return roughness;
    float thick;   // diameter of the curved cross-section (the unit primitives are 1 m across)
    const float sx = length(Vec3{world.at(0, 0), world.at(1, 0), world.at(2, 0)});
    const float sy = length(Vec3{world.at(0, 1), world.at(1, 1), world.at(2, 1)});
    const float sz = length(Vec3{world.at(0, 2), world.at(1, 2), world.at(2, 2)});
    switch (n.primitive) {
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone: thick = std::min(sx, sy); break;
        case PrimitiveType::Sphere: thick = std::min(sx, std::min(sy, sz)); break;
        default: return roughness;   // flat faces have one normal: no sub-pixel highlight to alias
    }
    const float footprint = length(transformPoint(world, {0, 0, 0}) - specLodEye_) * specLodPixel_;
    const float k = clampf((footprint * 4.0f / std::max(thick, 1e-4f) - 0.25f) / 0.75f, 0.0f, 1.0f);   // < ~4 px across
    return lerpf(roughness, std::max(roughness, 0.85f), k * k * (3.0f - 2.0f * k));
}

void Renderer::setObjectUniforms(Shader& sh, const SceneNode& n, const Mat4& world, const RenderSettings& s) {
    const ObjectParams p = objectParams(n, world, s);
    sh.set("uModel", p.model);
    sh.set("uNormalMat", p.normal);
    sh.set("uBaseColor", p.baseColor);
    sh.set("uHeat", p.heat);
    sh.set("uEmissive", p.emissive);
    sh.set("uSpecular", p.specular);
    sh.set("uShininess", p.shininess);
    sh.set("uRoughness", p.roughness);
    sh.set("uMetalness", p.metalness);
    sh.set("uSurface", p.surface);
    sh.set("uHeatVariation", p.heatVariation);
    sh.set("uFogged", p.fogged);
    sh.set("uOpacity", p.opacity);
    sh.set("uPartCentre", p.centre);
    sh.set("uPartRadius", p.radius);
}

Shader* Renderer::instancedFor(const Shader* program) const {
    if (program == flat_.get()) return flatI_.get();
    if (program == gouraud_.get()) return gouraudI_.get();
    if (program == phong_.get()) return phongI_.get();
    return nullptr;
}

bool Renderer::uploadInstances(const std::vector<const SceneNode*>& nodes, int texelsPerPart, const RenderSettings* s) {
    std::vector<float>& d = instanceScratch_;
    d.clear();
    d.reserve(nodes.size() * static_cast<size_t>(texelsPerPart) * 4);
    auto push4 = [&d](float a, float b, float c, float e) {
        d.push_back(a);
        d.push_back(b);
        d.push_back(c);
        d.push_back(e);
    };
    for (const SceneNode* n : nodes) {
        if (texelsPerPart == 11 && s) {
            const ObjectParams p = objectParams(*n, n->world, *s);
            d.insert(d.end(), p.model.m, p.model.m + 16);
            const float* N = p.normal.m;   // column-major 3x3
            push4(N[0], N[1], N[2], p.heat);
            push4(N[3], N[4], N[5], p.emissive);
            push4(N[6], N[7], N[8], p.specular);
            push4(p.baseColor.x, p.baseColor.y, p.baseColor.z, p.shininess);
            push4(p.roughness, p.metalness, static_cast<float>(p.surface), p.heatVariation);
            push4(p.centre.x, p.centre.y, p.centre.z, p.radius);
            push4(p.fogged, p.opacity, 0.0f, 0.0f);
        } else {
            d.insert(d.end(), n->world.m, n->world.m + 16);
            if (texelsPerPart == 7) {
                const Mat3 nm = normalMatrix(n->world);
                const float* N = nm.m;
                push4(N[0], N[1], N[2], 0.0f);
                push4(N[3], N[4], N[5], 0.0f);
                push4(N[6], N[7], N[8], 0.0f);
            }
        }
    }
    return instances_.upload(d, kUnitInstances);
}

int Renderer::drawInstanced(Shader& sh, const std::vector<const SceneNode*>& nodes, FrameStats* st) {
    int draws = 0;
    for (size_t first = 0; first < nodes.size();) {
        const PrimitiveType type = nodes[first]->primitive;
        size_t last = first + 1;
        while (last < nodes.size() && nodes[last]->primitive == type) ++last;
        const GLsizei count = static_cast<GLsizei>(last - first);
        const Mesh& mesh = *meshes_.at(type);
        mesh.bind(true);
        sh.set("uInstanceBase", static_cast<int>(first));
        glDrawElementsInstanced(GL_TRIANGLES, mesh.indexCount(), GL_UNSIGNED_INT, nullptr, count);
        ++draws;
        if (st) {
            ++st->drawCalls;
            st->primitives += count;
            st->triangles += static_cast<long long>(count) * (mesh.indexCount() / 3);
            st->vertices += static_cast<long long>(count) * mesh.vertexCount();
        }
        first = last;
    }
    return draws;
}

namespace {

// Groups the parts by mesh (stable: the parts of one mesh keep the scene order) for the instanced draws.
void groupByMesh(std::vector<const SceneNode*>& nodes) {
    std::stable_sort(nodes.begin(), nodes.end(), [](const SceneNode* a, const SceneNode* b) {
        return static_cast<int>(a->primitive) < static_cast<int>(b->primitive);
    });
}

}  // namespace

void Renderer::setSkyUniforms(Shader& sh) const {
    const SkyModel& m = skyModel_;
    const char* perez[5] = {"uSkyPerez[0]", "uSkyPerez[1]", "uSkyPerez[2]", "uSkyPerez[3]", "uSkyPerez[4]"};
    for (int i = 0; i < 5; ++i) sh.set(perez[i], m.perez[i]);
    sh.set("uSkyZenithN", m.zenith);
    sh.set("uSkySun", m.toSun);
    sh.set("uSkyScale", m.scale);
    float sh9[27];
    for (int i = 0; i < 9; ++i) {
        sh9[3 * i] = atmo_.skySH[i].x;
        sh9[3 * i + 1] = atmo_.skySH[i].y;
        sh9[3 * i + 2] = atmo_.skySH[i].z;
    }
    sh.setVec3Array("uSkySH", sh9, 9);
}

void Renderer::drawSky(const CameraView& cam, int w, int h, const RenderSettings& s, float time) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    sky_->use();
    setSkyUniforms(*sky_);
    sky_->set("uInvViewProj", inverse(cam.proj * cam.view));
    sky_->set("uCamPos", cam.position);
    sky_->set("uViewMode", static_cast<int>(s.view));
    sky_->set("uPalette", s.palette);
    sky_->set("uRain", s.rain);
    sky_->set("uHdr", s.cinematic && s.view == ViewMode::Visible && s.edges == EdgeMode::Solid ? 1 : 0);
    sky_->set("uFogColor", atmo_.fogColor);
    sky_->set("uDay", atmo_.day);
    sky_->set("uNight", atmo_.night);
    sky_->set("uSunDir", atmo_.sunDir);
    sky_->set("uSunColor", atmo_.sunColor);
    sky_->set("uTwilight", atmo_.twilight);
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
        if (atmo_.night <= 0.0f && n->name == "Moon") continue;   // sun above the horizon: no lit moon ball in the sky
        if (!s.isolate.empty() && n->object != s.isolate) continue;
        // The gimbal camera hangs below the airframe, so the UAV's own parts are outside its view.
        if ((s.view == ViewMode::HeatRaw || s.hideUav) && n->object == "Autonomous UAV") continue;
        if (!inFrustum(*n, frustum)) continue;
        if (n->material.opacity < 1.0f && s.view != ViewMode::Diagnostic) continue;  // translucent pass
        bucketOf(&programFor(*n, s)).push_back(n);
    }
    for (auto& [program, nodes] : buckets) {
        if (nodes.empty()) continue;
        // v7 WP-Q: one instanced draw per mesh (the parts' parameters come from the instance buffer).
        if (Shader* inst = useInstancing(s) ? instancedFor(program) : nullptr) {
            groupByMesh(nodes);
            if (uploadInstances(nodes, 11, &s)) {
                inst->use();
                setFrameUniforms(*inst, scene, cam, w, h, s);
                drawInstanced(*inst, nodes, &st);
                continue;
            }
        }
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
    // Rotor blur discs (and v9 M5: blades fading out as a rotor spins up): blended over the opaque scene, depth-tested
    // but not written; visible light and (v9 M5) the thermal view, so a spinning rotor looks the same in both.
    if ((s.view != ViewMode::Visible && s.view != ViewMode::Thermal) || !s.isolate.empty() || s.hideUav) return;
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
        sh.set("uBlurDisc", n->name == "Rotor blur disc" ? 1.0f : 0.0f);
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
    if (!beam.visible || !scene.lights.spot.enabled) return;              // searchlights off by day
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
        if (!n->visible || n->primitive == PrimitiveType::BeamCone || n->name == "Moon" || n->name == "Sun") continue;
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
    atmo_ = scene.lights.atmo;
    skyModel_ = preethamSky(atmo_.sunDir * -1.0f);   // v9 M8: the frame's analytic sky (the model DayCycle's ambient uses)
    walk_ = sceneaccess::walkSpace(scene);
    uploadHeatTrail(scene);
    if (!groundBuilt_) {  // needs the world matrices, so it is built on the first frame
        ground_.build(scene);
        grass_.setDensityMap(&ground_);
        groundBuilt_ = true;
    }
    drawSky(cam, w, h, s, scene.time());
    if (skyOnly_ && mrt_) return;   // v7 WP-H: fully ray-traced frame (the tracer reads this sky for primary misses)
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

void Renderer::captureUavCamera(const Scene& scene, std::vector<uint8_t>& heat, FrameStats& stats, int uav,
                                const Mat4* cameraOverride) {
    timer_.begin("uav camera (CV)");
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    renderUavCamera(scene, stats, uav, cameraOverride);
    cvTarget_.read(heat);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    timer_.end();
}

void Renderer::beginUavCamera(const Scene& scene, FrameStats& stats, int uav, const Mat4* cameraOverride, int slot) {
    timer_.begin("uav camera (CV)");
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    renderUavCamera(scene, stats, uav, cameraOverride);
    cvTarget_.readAsync(slot);
    // Submit now: the copy then completes ahead of the main frame, and on Mesa d3d12 (which tracks buffer use per
    // command batch) collecting it waits only for this batch, not for the whole frame queued after it.
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));
    timer_.end();
}

void Renderer::renderUavCamera(const Scene& scene, FrameStats& stats, int uav, const Mat4* cameraOverride) {
    cvTarget_.bind();
    RenderSettings s;
    s.view = ViewMode::HeatRaw;
    s.grain = 0.0f;
    const bool mrt = mrt_;
    mrt_ = false;
    CameraView cam = uavCameraView(scene, static_cast<float>(kCvWidth) / kCvHeight, kCvFovYDeg, uav);
    if (cameraOverride) {  // same lens geometry as uavCameraView, from the given (jittered) gimbal pose
        const Mat4& m = *cameraOverride;
        cam.position = transformPoint(m, {0, 0, kLampFrontOffset + 0.03f});
        const Vec3 forward = normalize(transformDirection(m, {0, 0, 1}));
        const Vec3 up = normalize(transformDirection(m, {0, 1, 0}));
        cam.view = lookAt(cam.position, cam.position + forward, up);
    }
    drawScene(scene, cam, kCvWidth, kCvHeight, s, stats);
    mrt_ = mrt;
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
    phong_->set("uCloudShadowOn", 0.0f);
    phong_->set("uSolar", 0.0f);
    phong_->set("uKeyIsSun", 0);
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
