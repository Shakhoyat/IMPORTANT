// v10 M3 capture modes (see CaptureParts.h): part tiles, build stages and the primitive library, rendered through the
// real Renderer (the same 10:30 daylight, exposure and TAA accumulation as the iso_* hero shots).
#include "app/CaptureParts.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>

#include "objects/Terrain.h"
#include "primitives/Library.h"
#include "rendering/ImageWriter.h"
#include "rendering/LabStage.h"
#include "rendering/RenderTarget.h"
#include "scene/Camera.h"
#include "scene/PartOrder.h"

namespace ts::partscap {

namespace {

constexpr int kTile = 512;                 // part tiles
constexpr int kBuildW = 1600, kBuildH = 1200;
constexpr int kPrimSize = 800;
constexpr float kFill = 0.7f;              // bounding sphere / projected box share of the frame
constexpr float kHeroYaw = 35.0f, kHeroPitch = 16.0f, kHeroFov = 50.0f;   // the iso_* hero shots
constexpr float kTileYaw = 35.0f, kTilePitch = 25.0f, kTileFov = 30.0f;   // part / primitive tiles (look down: the
                                                                          // background is the even haze below the horizon)
constexpr int kFrames = 8;                 // TAA accumulation of the frozen frame (as the hero shots)

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGB, rows top to bottom
};

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

std::string esc(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') o += '\\';
        o += ch;
    }
    return o;
}

void logLine(Ctx& c, const std::string& s) {
    if (c.log) *c.log << s << "\n";
}

RenderSettings baseSettings(const Ctx& c, const std::string& isolate) {
    RenderSettings s;
    s.view = ViewMode::Visible;
    s.quality = c.quality;
    s.exposure = 2.0f;          // as the hero shots: the object alone against the bright day sky (+1 EV)
    s.isolate = isolate;        // grass, foliage, rain and the combat effects stay off
    s.showLegend = false;
    s.flareOn = false;
    s.taaOn = true;
    s.autoExposureOn = false;   // one fixed exposure: every tile / stage of a set has the same background brightness
    return s;
}

Image renderImage(Ctx& c, const CameraView& cam, const RenderSettings& s, int w, int h) {
    static RenderTarget target;
    static int tw = 0, th = 0;
    if (w != tw || h != th) {
        target.create(w, h, 0, false);
        tw = w;
        th = h;
    }
    c.renderer->resetHistory();
    for (int f = 0; f < kFrames; ++f) {
        target.bind();
        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        FrameStats st;
        c.renderer->beginFrame(*c.scene, cam, w, h, s, st);
        c.renderer->endFrame(static_cast<GLuint>(fbo), w, h, c.scene->time());
    }
    Image img;
    img.w = w;
    img.h = h;
    target.read(img.px);
    return img;
}

bool save(Ctx& c, const std::string& path, const Image& img) {
    const bool ok = writePng(path, img.w, img.h, img.px);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", path.c_str(), img.w, img.h);
    logLine(c, std::string(ok ? "wrote " : "FAILED ") + path);
    return ok;
}

CameraView viewFrom(const Vec3& target, const Vec3& dir, float dist, float fovDeg, float aspect) {
    CameraView v;
    v.position = target + dir * dist;
    v.view = lookAt(v.position, target, {0, 1, 0});
    v.nearZ = std::max(0.02f, dist * 0.02f);
    v.farZ = 600.0f;
    v.proj = perspective(radians(fovDeg), aspect, v.nearZ, v.farZ);
    return v;
}

Vec3 dirFrom(float yawDeg, float pitchDeg) {
    return {std::cos(radians(pitchDeg)) * std::sin(radians(yawDeg)), std::sin(radians(pitchDeg)),
            std::cos(radians(pitchDeg)) * std::cos(radians(yawDeg))};
}

// Of front + yaw and front - yaw, the side the sun lights (the hero shots' rule).
float sunnyYaw(const Ctx& c, float front, float yaw) {
    auto facing = [&](float y) { return std::sin(radians(y)) * c.sun.x + std::cos(radians(y)) * c.sun.z; };
    return facing(front - yaw) > facing(front + yaw) ? front - yaw : front + yaw;
}

// One throwaway frame of the real scene first: the renderer builds its ground maps from the unmodified scene.
void warmUp(Ctx& c) {
    static bool done = false;
    if (done) return;
    done = true;
    RenderSettings s = baseSettings(c, "");
    CameraView v = viewFrom({0, 1, 0}, dirFrom(0, 30), 30.0f, 50.0f, 1.0f);
    renderImage(c, v, s, 64, 64);
}

// ------------------------------------------------------------------------------------------------ wire overlay
// Triangle edges drawn over a rendered image: the mesh is rasterised into a CPU depth buffer (1 / w interpolated in
// screen space) with the same matrix, so edges behind the surface are drawn faint (hidden-line view).
void overlayWire(Image& img, const MeshData& mesh, const Mat4& mvp) {
    const int W = img.w, H = img.h;
    struct Sv {
        float x, y, iw;   // screen position, 1 / clip w
        bool ok;
    };
    std::vector<Sv> sv(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3& p = mesh.vertices[i].position;
        float clip[4];
        for (int r = 0; r < 4; ++r) clip[r] = mvp.at(r, 0) * p.x + mvp.at(r, 1) * p.y + mvp.at(r, 2) * p.z + mvp.at(r, 3);
        sv[i].ok = clip[3] > 1e-4f;
        const float iw = sv[i].ok ? 1.0f / clip[3] : 0.0f;
        sv[i].x = (clip[0] * iw * 0.5f + 0.5f) * static_cast<float>(W);
        sv[i].y = (1.0f - (clip[1] * iw * 0.5f + 0.5f)) * static_cast<float>(H);
        sv[i].iw = iw;
    }
    std::vector<float> depth(static_cast<size_t>(W) * H, 0.0f);   // largest 1 / w = nearest
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const Sv& a = sv[mesh.indices[t]];
        const Sv& b = sv[mesh.indices[t + 1]];
        const Sv& d = sv[mesh.indices[t + 2]];
        if (!a.ok || !b.ok || !d.ok) continue;
        const float area = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
        if (std::fabs(area) < 1e-8f) continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, d.x}))));
        const int x1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, d.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, d.y}))));
        const int y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, d.y}))));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float w0 = ((b.x - px) * (d.y - py) - (b.y - py) * (d.x - px)) / area;
                const float w1 = ((d.x - px) * (a.y - py) - (d.y - py) * (a.x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float iw = w0 * a.iw + w1 * b.iw + w2 * d.iw;
                float& z = depth[static_cast<size_t>(y) * W + x];
                z = std::max(z, iw);
            }
    }
    // Unique edges by quantised end positions (flat-shaded faces repeat their corners).
    auto q = [](const Vec3& p) { return std::make_tuple(std::lround(p.x * 1e4f), std::lround(p.y * 1e4f), std::lround(p.z * 1e4f)); };
    std::map<std::pair<decltype(q(Vec3{})), decltype(q(Vec3{}))>, std::pair<uint32_t, uint32_t>> edges;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
        for (int e = 0; e < 3; ++e) {
            uint32_t i = mesh.indices[t + e], j = mesh.indices[t + (e + 1) % 3];
            auto ki = q(mesh.vertices[i].position), kj = q(mesh.vertices[j].position);
            if (kj < ki) {
                std::swap(ki, kj);
                std::swap(i, j);
            }
            if (ki != kj) edges.emplace(std::make_pair(ki, kj), std::make_pair(i, j));
        }
    std::vector<float> cover(static_cast<size_t>(W) * H, 0.0f);
    for (const auto& kv : edges) {
        const Sv& a = sv[kv.second.first];
        const Sv& b = sv[kv.second.second];
        if (!a.ok || !b.ok) continue;
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        const int n = std::max(2, static_cast<int>(len * 2.0f));
        for (int k = 0; k <= n; ++k) {
            const float u = static_cast<float>(k) / static_cast<float>(n);
            const float x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u, iw = a.iw + (b.iw - a.iw) * u;
            const int cx = static_cast<int>(x), cy = static_cast<int>(y);
            if (cx < 0 || cy < 0 || cx >= W || cy >= H) continue;
            const float zb = depth[static_cast<size_t>(cy) * W + cx];
            const bool visible = zb <= 0.0f || iw >= zb * 0.995f;   // 1/w: nearer = larger (0.5 % depth tolerance)
            const float strength = visible ? 0.9f : 0.12f;
            for (int yy = cy - 1; yy <= cy + 1; ++yy)
                for (int xx = cx - 1; xx <= cx + 1; ++xx) {
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    const float dd = std::hypot(xx + 0.5f - x, yy + 0.5f - y);
                    const float a2 = std::clamp(1.25f - dd, 0.0f, 1.0f) * strength;
                    float& cv = cover[static_cast<size_t>(yy) * W + xx];
                    cv = std::max(cv, a2);
                }
        }
    }
    const float line[3] = {0.06f, 0.08f, 0.14f};
    for (size_t i = 0; i < cover.size(); ++i) {
        const float a = cover[i];
        if (a <= 0.0f) continue;
        for (int k = 0; k < 3; ++k) {
            uint8_t& v = img.px[i * 3 + k];
            v = static_cast<uint8_t>(std::lround(v * (1.0f - a) + 255.0f * line[k] * a));
        }
    }
}

// ------------------------------------------------------------------------------------------------ helpers
const SceneNode* findRig(const Scene& scene, const std::string& object) {
    for (const SceneNode* n : scene.leaves())
        if (n->object == object) return rigRoot(*n);
    return nullptr;
}

// Bounding sphere of a mesh under matrix m (centre of the box, largest distance).
void meshSphere(const MeshData& mesh, const Mat4& m, Vec3& centre, float& radius) {
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    std::vector<Vec3> pts;
    pts.reserve(mesh.vertices.size());
    for (const Vertex& v : mesh.vertices) {
        const Vec3 p = transformPoint(m, v.position);
        pts.push_back(p);
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    centre = (lo + hi) * 0.5f;
    radius = 1e-6f;
    for (const Vec3& p : pts) radius = std::max(radius, length(p - centre));
}

}  // namespace

bool handles(const std::vector<std::string>& names) {
    if (names.empty()) return false;
    for (const std::string& n : names)
        if (n != "prims" && n.rfind("parts:", 0) != 0 && n.rfind("build:", 0) != 0) return false;
    return true;
}

bool run(Ctx& c, const std::vector<std::string>& names) {
    warmUp(c);
    bool ok = true;
    for (const std::string& n : names) {
        if (n == "prims") ok = prims(c) && ok;
        else if (n.rfind("parts:", 0) == 0) ok = parts(c, n.substr(6)) && ok;
        else if (n.rfind("build:", 0) == 0) ok = build(c, n.substr(6)) && ok;
    }
    return ok;
}

// ================================================================================================ parts
bool parts(Ctx& c, const std::string& object) {
    const SceneNode* rig = findRig(*c.scene, object);
    if (!rig) {
        std::fprintf(stderr, "parts: no object '%s'\n", object.c_str());
        logLine(c, "parts: no object '" + object + "'");
        return false;
    }
    const std::string dir = c.dir + "/parts_" + slug(object);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::vector<PartKind> kinds = partKinds(*rig);
    const Vec3 target{0.0f, 3.0f, 0.0f};
    const Vec3 dir3 = dirFrom(sunnyYaw(c, 0.0f, kTileYaw), kTilePitch);
    const float dist = 1.0f / (kFill * std::tan(radians(kTileFov) * 0.5f));   // unit bounding sphere fills kFill
    const CameraView cam = viewFrom(target, dir3, dist, kTileFov, 1.0f);
    RenderSettings s = baseSettings(c, LabStage::kObject);
    s.fogOn = false;
    s.practicalsOn = false;
    s.spotOn = false;
    std::string json = "{\n  \"object\": \"" + esc(object) + "\", \"rig_root\": \"" + esc(rig->id) + "\", \"tile\": " +
                       std::to_string(kTile) + ", \"fill\": " + num(kFill) + ",\n  \"parts\": [\n";
    bool ok = true;
    for (size_t i = 0; i < kinds.size(); ++i) {
        const PartKind& k = kinds[i];
        const SceneNode& n = *k.first;
        const Vec3 S = restScaleOf(n), R = n.local.rotationDeg;
        const MeshData mesh = sceneMesh(n.primitive);
        Vec3 centre;
        float radius;
        meshSphere(mesh, rotateEulerYXZ(R) * scale(S), centre, radius);
        const float kNorm = 1.0f / radius;   // the part's bounding sphere -> radius 1
        LabStage st;
        // A tile shows the part's shape: translucent parts (rotor blur, blade fade, glass) are drawn opaque, and the
        // light-beam frustum (an additive volume in the scene) is drawn as a shaded solid through the cylinder slot.
        Material m = n.material;
        std::string note;
        if (m.opacity < 1.0f) {
            m.opacity = 1.0f;
            note = "drawn opaque";
        }
        const bool beam = n.primitive == PrimitiveType::BeamCone;
        if (beam) {
            c.renderer->labOverrideMesh(PrimitiveType::Cylinder, mesh);
            m.emissive = 0.0f;
            note = "light beam drawn as a solid";
        }
        SceneNode* node = st.add(beam ? PrimitiveType::Cylinder : n.primitive, m, target - centre * kNorm, S * kNorm, R,
                                 beam ? ShadingModel::Phong : n.shading, n.name);
        node->restScale = S;
        st.install(*c.scene);
        c.renderer->labInvalidateCaches();
        const Image img = renderImage(c, cam, s, kTile, kTile);
        st.uninstall(*c.scene);
        if (beam) c.renderer->labOverrideMesh(PrimitiveType::Cylinder, MeshData{});
        char file[160];
        std::snprintf(file, sizeof(file), "%02zu_%s.png", i + 1, slug(k.name).c_str());
        ok = save(c, dir + "/" + file, img) && ok;
        json += std::string(i ? ",\n" : "") + "    {\"number\": " + std::to_string(i + 1) + ", \"part\": \"" + esc(k.name) +
                "\", \"primitive\": \"" + primitiveName(k.primitive) + "\", \"count\": " + std::to_string(k.count) +
                ", \"S_cm\": [" + num(S.x * 100.0f) + ", " + num(S.y * 100.0f) + ", " + num(S.z * 100.0f) + "], \"R_deg\": [" +
                num(R.x) + ", " + num(R.y) + ", " + num(R.z) + "], \"first_id\": \"" + esc(n.id) + "\", \"note\": \"" + note +
                "\", \"file\": \"" + file + "\"}";
    }
    c.renderer->labInvalidateCaches();
    json += "\n  ]\n}\n";
    std::ofstream(dir + "/index.json") << json;
    logLine(c, "parts " + object + ": " + std::to_string(kinds.size()) + " part kinds -> " + dir);
    return ok;
}

// ================================================================================================ build stages
bool build(Ctx& c, const std::string& object) {
    const SceneNode* rig = findRig(*c.scene, object);
    if (!rig) {
        std::fprintf(stderr, "build: no object '%s'\n", object.c_str());
        logLine(c, "build: no object '" + object + "'");
        return false;
    }
    const std::vector<const SceneNode*> order = rigLeavesDfs(*rig);
    // Hero camera fitted to the whole (visible) rig, exactly as the iso_* shots: front three-quarter on the sunny side,
    // the projected box of the parts' unit-cube corners spanning kFill of the frame, centred.
    std::vector<Vec3> corners;
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (const SceneNode* n : order) {
        if (hiddenAtRest(*n)) continue;
        for (int k = 0; k < 8; ++k) {
            const Vec3 p = transformPoint(n->world, {(k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f, (k & 4) ? 0.5f : -0.5f});
            corners.push_back(p);
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
    }
    const Vec3 f = transformDirection(rig->world, {0, 0, 1});
    const float front = length(Vec3{f.x, 0, f.z}) > 1e-4f ? degrees(std::atan2(f.x, f.z)) : 0.0f;
    const float yaw = sunnyYaw(c, front, kHeroYaw);
    const Vec3 dir = dirFrom(yaw, kHeroPitch);
    const float aspect = static_cast<float>(kBuildW) / static_cast<float>(kBuildH), th = std::tan(radians(kHeroFov) * 0.5f);
    const Vec3 fwd = dir * -1.0f, right = normalize(cross(fwd, Vec3{0, 1, 0})), up = cross(right, fwd);
    Vec3 target = (lo + hi) * 0.5f;
    float ndcCx = 0, ndcCy = 0;
    auto span = [&](float d) {
        const Vec3 eye = target + dir * d;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (const Vec3& p : corners) {
            const Vec3 v = p - eye;
            const float z = std::max(dot(v, fwd), 0.05f);
            const float x = dot(v, right) / (z * th * aspect), y = dot(v, up) / (z * th);
            x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        ndcCx = 0.5f * (x0 + x1);
        ndcCy = 0.5f * (y0 + y1);
        return std::max(0.5f * (x1 - x0), 0.5f * (y1 - y0));
    };
    const float r = 0.5f * length(hi - lo);
    float dLo = 0.5f * r, dHi = 20.0f * r;
    for (int it = 0; it < 40; ++it) {
        const float mid = 0.5f * (dLo + dHi);
        (span(mid) > kFill ? dLo : dHi) = mid;
    }
    span(dHi);
    target = target + right * (ndcCx * dHi * th * aspect) + up * (ndcCy * dHi * th);
    const CameraView cam = viewFrom(target, dir, dHi, kHeroFov, aspect);
    RenderSettings s = baseSettings(c, object);

    // Other rigs with the same object name (the second UAV, rovers 2-6) are hidden while the stages render.
    std::vector<std::pair<SceneNode*, bool>> saved;
    for (SceneNode* n : c.scene->leaves())
        if (n->object == object) saved.emplace_back(n, n->visible);
    for (auto& [n, vis] : saved)
        if (rigRoot(*n) != rig) n->visible = false;
    const std::string sl = slug(object);
    std::string json = "{\n  \"object\": \"" + esc(object) + "\", \"rig_root\": \"" + esc(rig->id) + "\", \"parts\": " +
                       std::to_string(order.size()) + ", \"camera\": {\"yaw_deg\": " + num(yaw) + ", \"pitch_deg\": " +
                       num(kHeroPitch) + ", \"fov_deg\": " + num(kHeroFov) + ", \"distance\": " + num(dHi) + "},\n  \"stages\": [\n";
    bool ok = true;
    for (int k = 0; k < 4; ++k) {
        const int pct = kBuildStagePct[k], shown = buildStageCount(static_cast<int>(order.size()), pct);
        for (size_t i = 0; i < order.size(); ++i) {
            SceneNode* n = const_cast<SceneNode*>(order[i]);
            bool vis = false;
            for (auto& [m, v] : saved)
                if (m == n) vis = v;
            n->visible = vis && static_cast<int>(i) < shown;
        }
        c.renderer->labInvalidateCaches();
        const Image img = renderImage(c, cam, s, kBuildW, kBuildH);
        char file[160];
        std::snprintf(file, sizeof(file), "build_%s_%d.png", sl.c_str(), pct);
        ok = save(c, c.dir + "/" + file, img) && ok;
        json += std::string(k ? ",\n" : "") + "    {\"pct\": " + std::to_string(pct) + ", \"parts\": " + std::to_string(shown) +
                ", \"last_part\": \"" + esc(order[static_cast<size_t>(shown - 1)]->name) + "\", \"file\": \"" + file + "\"}";
    }
    for (auto& [n, vis] : saved) n->visible = vis;
    c.renderer->labInvalidateCaches();
    json += "\n  ]\n}\n";
    std::ofstream(c.dir + "/build_" + sl + ".json") << json;
    return ok;
}

// ================================================================================================ primitive library
bool prims(Ctx& c) {
    const std::string dir = c.dir + "/prims";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::vector<LibraryEntry> lib = primitiveLibrary();
    const Vec3 target{0.0f, 3.0f, 0.0f};
    const Vec3 dir3 = dirFrom(sunnyYaw(c, 0.0f, kTileYaw), kTilePitch);
    const float dist = 0.8660254f / (0.75f * std::tan(radians(kTileFov) * 0.5f));   // unit cube's sphere fills 75 %
    const CameraView cam = viewFrom(target, dir3, dist, kTileFov, 1.0f);
    RenderSettings s = baseSettings(c, LabStage::kObject);
    s.fogOn = false;
    s.practicalsOn = false;
    s.spotOn = false;
    std::string json = "{\n  \"camera\": {\"yaw_deg\": " + num(sunnyYaw(c, 0.0f, kTileYaw)) + ", \"pitch_deg\": " + num(kTilePitch) +
                       ", \"fov_deg\": " + num(kTileFov) + ", \"distance\": " + num(dist) + "},\n  \"primitives\": [\n";
    bool ok = true;
    for (size_t i = 0; i < lib.size(); ++i) {
        const LibraryEntry& e = lib[i];
        // Every primitive at its unit size, centred; the cuboid is the cube with S = (1, 0.45, 0.7); the terrain disc
        // (world units) is scaled into the unit cube.
        // The capsule is shown at S = (0.5, 0.5, 1), where its caps are true hemispheres (S.z h = S.x / 2).
        Vec3 S{1, 1, 1};
        std::string note;
        if (e.cuboid) S = {1.0f, 0.45f, 0.7f};
        if (e.type == PrimitiveType::Capsule) S = {0.5f, 0.5f, 1.0f};
        Vec3 centre;
        float radius;
        meshSphere(e.mesh, Mat4::identity(), centre, radius);
        if (e.type == PrimitiveType::TerrainDisc) {
            const float k = 0.5f / radius;   // bounding sphere radius 0.5; heights x4 so the relief shows
            S = {k, 4.0f * k, k};
            note = "heights x4";
        }
        // The light-beam frustum (drawn by the additive beam pass in the scene) and the terrain disc (terrain shading)
        // are drawn here as plain shaded solids: their meshes stand in for the cylinder's for this one tile.
        const bool standIn = e.type == PrimitiveType::BeamCone || e.type == PrimitiveType::TerrainDisc;
        const PrimitiveType drawAs = standIn ? PrimitiveType::Cylinder : e.type;
        if (standIn) c.renderer->labOverrideMesh(PrimitiveType::Cylinder, e.mesh);
        meshSphere(e.mesh, scale(S), centre, radius);
        Material m;
        const Vec3 col = primitiveColor(e.type);
        m.baseColor = col * 0.65f + Vec3{0.62f, 0.62f, 0.62f} * 0.35f;
        m.roughness = 0.55f;
        m.metalness = 0.0f;
        m.heat = 0.2f;
        LabStage st;
        st.add(drawAs, m, target - centre, S, {0, 0, 0}, ShadingModel::Phong, e.name);
        st.install(*c.scene);
        c.renderer->labInvalidateCaches();
        Image img = renderImage(c, cam, s, kPrimSize, kPrimSize);
        st.uninstall(*c.scene);
        if (standIn) c.renderer->labOverrideMesh(PrimitiveType::Cylinder, MeshData{});
        char base[160];
        std::string nm = slug(e.name);
        std::snprintf(base, sizeof(base), "prim_%02d_%s", e.number, nm.c_str());
        ok = save(c, dir + "/" + base + ".png", img) && ok;
        const Mat4 mvp = cam.proj * cam.view * translate(target - centre) * scale(S);
        if (e.type == PrimitiveType::TerrainDisc) {
            // ~10^5 triangles cannot be drawn as lines at 800 px: the overlay shows the same generator at 1/10 of the
            // rings and 1/8 of the slices (noted in prims.json).
            overlayWire(img, createTerrainDisc(kTerrainRings / 10, kTerrainSlices / 8), mvp);
            note += std::string(note.empty() ? "" : "; ") + "wire overlay: createTerrainDisc(" + std::to_string(kTerrainRings / 10) +
                    ", " + std::to_string(kTerrainSlices / 8) + ")";
        } else {
            overlayWire(img, e.mesh, mvp);
        }
        ok = save(c, dir + "/" + base + "_wire.png", img) && ok;
        json += std::string(i ? ",\n" : "") + "    {\"number\": " + std::to_string(e.number) + ", \"name\": \"" + esc(e.name) +
                "\", \"type\": \"" + esc(e.cuboid ? std::string("Cuboid") : std::string(primitiveName(e.type))) +
                "\", \"function\": \"" + esc(e.call) + "\", \"V\": " + std::to_string(e.mesh.vertexCount()) + ", \"T\": " +
                std::to_string(e.mesh.triangleCount()) + ", \"formula_V\": \"" + esc(e.vFormula) + "\", \"formula_T\": \"" +
                esc(e.tFormula) + "\", \"S\": [" + num(S.x) + ", " + num(S.y) + ", " + num(S.z) + "], \"note\": \"" + note +
                "\", \"file\": \"" + base +
                ".png\", \"wire\": \"" + base + "_wire.png\"}";
    }
    c.renderer->labInvalidateCaches();
    json += "\n  ]\n}\n";
    std::ofstream(dir + "/prims.json") << json;
    return ok;
}

}  // namespace ts::partscap
