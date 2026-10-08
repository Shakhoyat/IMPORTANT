#include "raytrace/SceneBridge.h"

#include <algorithm>
#include <cmath>

#include "objects/BaseLayout.h"
#include "scene/Scene.h"

namespace ts::rt {

namespace {

float det3(const Mat4& m) {
    return m.at(0, 0) * (m.at(1, 1) * m.at(2, 2) - m.at(1, 2) * m.at(2, 1)) -
           m.at(0, 1) * (m.at(1, 0) * m.at(2, 2) - m.at(1, 2) * m.at(2, 0)) +
           m.at(0, 2) * (m.at(1, 0) * m.at(2, 1) - m.at(1, 1) * m.at(2, 0));
}

Shape shapeOf(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::Sphere: return Shape::Sphere;
        case PrimitiveType::Cylinder: return Shape::Cylinder;
        case PrimitiveType::Cone: return Shape::Cone;
        default: return Shape::Box;
    }
}

// Leaves that are not solid geometry for the tracer: the additive searchlight cone (volumetric light), the
// translucent rotor blur discs, and the terrain disc (the height field).
bool traced(const SceneNode& n) {
    return n.primitive != PrimitiveType::BeamCone && n.primitive != PrimitiveType::TerrainDisc &&
           n.primitive != PrimitiveType::None && n.material.opacity >= 1.0f;
}

bool insideAny(const std::vector<Box>& boxes, const Vec3& p) {
    for (const Box& b : boxes)
        if (p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y && p.z >= b.min.z && p.z <= b.max.z)
            return true;
    return false;
}

Vec3 toLinear(const Vec3& c) { return {std::pow(c.x, 2.2f), std::pow(c.y, 2.2f), std::pow(c.z, 2.2f)}; }

}  // namespace

RtMaterial materialFromScene(const Material& m, float emissiveFactor) {
    RtMaterial r;
    r.albedo = toLinear(m.baseColor);
    r.roughness = clampf(pbrRoughness(m), 0.05f, 1.0f);
    r.metalness = m.metalness;
    r.surface = static_cast<int>(m.surface);
    r.emission = r.albedo * (emissiveFactor * 8.0f);   // the raster's albedo * uEmissive * uEmissiveScale (HDR)
    // Glass panes and lenses that do not glow are clear dielectrics; their (dark) base colour becomes a tint.
    if (m.surface == Surface::Glass && emissiveFactor <= 0.02f) {
        r.transmission = 1.0f;
        r.ior = 1.5f;
        const float mx = std::max({r.albedo.x, r.albedo.y, r.albedo.z, 1e-4f});
        r.tint = r.albedo / mx * 0.8f + Vec3{0.12f, 0.12f, 0.12f};
        r.roughness = 0.05f;
        r.metalness = 0.0f;
    }
    return r;
}

float SceneBridge::emissiveFactor(const Scene& scene, const SceneNode& n, float rain) const {
    const Material& m = n.material;
    const Atmosphere& A = scene.lights.atmo;
    if (n.name == "Moon") return m.emissive * (1.0f - 0.95f * rain) * A.night;
    if (n.name == "Sun") return m.emissive * (1.0f - 0.95f * rain) * A.day;
    if (m.lightIntensity <= 0.0f || A.practicalScale >= 1.0f) return m.emissive;
    if (n.name == "Fire embers" || n.name == "Strobe" || n.name.rfind("Muzzle", 0) == 0) return m.emissive;
    if (insideAny(scene.walkSpace().indoorVolumes(), transformPoint(n.world, {0, 0, 0}))) return m.emissive;
    return m.emissive * A.practicalScale;
}

void SceneBridge::rebuild(const Scene& scene, RtScene& out) {
    const std::vector<SceneNode*>& lv = scene.leaves();
    leaves_.assign(lv.begin(), lv.end());
    out.prims.clear();
    out.materials.clear();
    const SceneNode* terrainNode = nullptr;
    for (size_t i = 0; i < lv.size(); ++i) {
        const SceneNode& n = *lv[i];
        if (n.primitive == PrimitiveType::TerrainDisc) terrainNode = &n;
        if (!traced(n)) continue;
        uint32_t flags = n.dynamic ? kDynamic : 0u;
        if (n.name == "Moon" || n.name == "Sun") flags |= kSkyBody | kNoShadow;
        if (n.material.lightIntensity > 0.0f) flags |= kLightProxy;
        if (n.material.surface == Surface::Foliage) flags |= kFoliage;
        if (n.object == "Autonomous UAV") flags |= kDynamic | kLightHolder;
        out.addPrim(shapeOf(n.primitive), n.world, materialFromScene(n.material, n.material.emissive), flags,
                    static_cast<int>(i), n.name.c_str());
    }
    // Per-frame state (visibility, emission) before the build so hidden parts get empty boxes.
    for (RtPrim& p : out.prims) {
        const SceneNode& n = *leaves_[p.node];
        const bool hide = !n.visible || std::fabs(det3(n.world)) < 1e-12f ||
                          (n.name == "Moon" && scene.lights.atmo.night <= 0.0f);
        p.flags = hide ? (p.flags | kHidden) : (p.flags & ~kHidden);
    }
    out.build();
    if (hf_.empty()) hf_.buildTerrain(kHeightfieldSamples, layout::kBaseRadius);
    if (terrainNode) out.setTerrain(&hf_, materialFromScene(terrainNode->material, 0.0f));
    else out.setTerrain(nullptr, RtMaterial{});
    ++rebuilds_;
    ratio_ = 1.0f;
}

bool SceneBridge::sync(const Scene& scene, const LightOptions& lo, RtScene& out) {
    const std::vector<SceneNode*>& lv = scene.leaves();
    bool built = false;
    const bool same = lv.size() == leaves_.size() && std::equal(lv.begin(), lv.end(), leaves_.begin());
    if (!same || out.prims.empty() != leaves_.empty()) {
        rebuild(scene, out);
        built = true;
    }
    for (size_t k = 0; k < out.prims.size(); ++k) {
        RtPrim& p = out.prims[k];
        const SceneNode& n = *leaves_[p.node];
        p.world = n.world;
        if (traced(n)) p.shape = shapeOf(n.primitive);   // v7 WP-P: the first-person viewmodel re-shapes hidden leaves per frame
        const bool hide = !n.visible || std::fabs(det3(n.world)) < 1e-12f ||
                          (n.name == "Moon" && scene.lights.atmo.night <= 0.0f);
        p.flags = hide ? (p.flags | kHidden) : (p.flags & ~kHidden);
        out.materials[k] = materialFromScene(n.material, emissiveFactor(scene, n, lo.rain));
    }
    if (!built) {
        ratio_ = out.refit();
        if (ratio_ > kRebuildRatio) {
            rebuild(scene, out);
            built = true;
        }
    }
    lightsFrom(scene, lo, out);
    return built;
}

const SceneNode* SceneBridge::leafOf(const RtScene& rs, int prim) const {
    if (prim < 0 || prim >= static_cast<int>(rs.prims.size())) return nullptr;
    const int node = rs.prims[prim].node;
    return node >= 0 && node < static_cast<int>(leaves_.size()) ? leaves_[node] : nullptr;
}

void SceneBridge::lightsFrom(const Scene& scene, const LightOptions& lo, RtScene& out) const {
    const LightRig& L = scene.lights;
    const Atmosphere& A = L.atmo;
    out.lights.clear();
    // Sky for secondary rays, fog for primary hits
    out.env.zenith = A.skyZenith;
    out.env.bounce = A.groundBounce;
    out.env.fogColor = A.fogColor;
    out.env.fogDensity = A.fogDensity;
    // L0 key light: same colour as the raster's uMoonColor in the graded (HDR) view.
    {
        RtLight k;
        k.type = LightType::Directional;
        k.direction = normalize(L.moon.direction);
        const float scale = 0.85f * (1.0f - 0.55f * lo.rain);
        const Vec3 tint = A.keyIsSun ? Vec3{1, 1, 1} : Vec3{0.85f, 0.95f, 1.2f};
        k.color = mul(L.moon.color, tint) * (lo.moonOn && L.moon.enabled ? L.moon.intensity * scale : 0.0f);
        k.radius = radians(A.keyIsSun ? 0.6f : 0.8f);   // visible penumbrae (sun 0.27 deg, exaggerated a little)
        if (luminance(k.color) > 0.0f) out.lights.push_back(k);
    }
    if (lo.beaconOn && L.beacon.enabled && A.practicalScale > 0.0f) {
        RtLight b;
        b.type = LightType::Point;
        b.position = L.beacon.position;
        b.color = L.beacon.color * (L.beacon.intensity * A.practicalScale);
        b.linear = L.beacon.linear;
        b.quadratic = L.beacon.quadratic;
        b.radius = 0.08f;
        out.lights.push_back(b);
    }
    if (lo.spotOn && L.spot.enabled) {
        RtLight s;
        s.type = LightType::Spot;
        s.position = L.spot.position;
        s.direction = normalize(L.spot.direction);
        s.color = L.spot.color * L.spot.intensity;
        s.linear = L.spot.linear;
        s.quadratic = L.spot.quadratic;
        s.cosInner = std::cos(radians(L.spot.innerDeg));
        s.cosOuter = std::cos(radians(L.spot.outerDeg));
        s.radius = 0.08f;
        out.lights.push_back(s);
    }
    if (!lo.practicalsOn) return;
    // Practical lamps + transient combat lights, ranked as in Renderer::gatherPracticals (16 kept).
    struct Cand {
        float score;
        RtLight l;
    };
    std::vector<Cand> ranked;
    const std::vector<Box>& indoor = scene.walkSpace().indoorVolumes();
    for (const SceneNode* n : scene.practicalLights()) {
        const Material& m = n->material;
        if (!n->visible) continue;
        float factor = 1.0f;
        if (A.practicalScale < 1.0f && !(n->name == "Fire embers" || n->name == "Strobe" || n->name.rfind("Muzzle", 0) == 0) &&
            !insideAny(indoor, transformPoint(n->world, {0, 0, 0})))
            factor = A.practicalScale;
        const float k = m.lightIntensity * clampf(m.emissive, 0.0f, 1.5f) * factor;
        if (k <= 0.0f) continue;
        RtLight l;
        l.type = LightType::Practical;
        l.position = transformPoint(n->world, {0, 0, 0});
        l.color = toLinear(m.baseColor) * k;
        l.range = m.lightRange;
        l.direction = normalize(transformDirection(n->world, {0, 0, 1}));
        l.cosOuter = m.lightConeDeg > 0.0f ? std::cos(radians(m.lightConeDeg)) : -2.0f;
        l.radius = 0.05f;
        ranked.push_back({length(l.position - lo.camera) / std::max(m.lightRange, 0.1f), l});
    }
    for (const DynamicLight& d : scene.combat().lights()) {
        RtLight l;
        l.type = LightType::Practical;
        l.position = d.position;
        l.color = d.color;
        l.range = d.range;
        l.direction = d.direction;
        l.cosOuter = d.coneCos;
        l.radius = 0.1f;
        ranked.push_back({0.5f * length(d.position - lo.camera) / std::max(d.range, 0.1f), l});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
    for (size_t i = 0; i < ranked.size() && i < 16; ++i) out.lights.push_back(ranked[i].l);
}

}  // namespace ts::rt
