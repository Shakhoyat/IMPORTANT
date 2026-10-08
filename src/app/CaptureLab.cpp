// Lab figures (see CaptureLab.h). Everything here renders through the real Renderer; the 2D annotations (boxes, axes,
// labels, histograms) are drawn with the Hud API on a plain RenderTarget and composed on the CPU.
#include "app/CaptureLab.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>

#include "ai/DroneController.h"
#include "rendering/FrameExplorer.h"
#include "rendering/ImageWriter.h"
#include "rendering/LabStage.h"
#include "rendering/RenderTarget.h"
#include "scene/Camera.h"
#include "scene/SceneApi.h"
#include "scene/HudMath.h"

namespace ts::lab {

namespace {

// ------------------------------------------------------------------------------------------------ images
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // RGB, rows top to bottom
    Image() = default;
    Image(int W, int H, uint8_t v = 0) : w(W), h(H), px(static_cast<size_t>(W) * H * 3, v) {}
};

Image fromLab(const LabImage& l) {
    Image i;
    i.w = l.w;
    i.h = l.h;
    i.px = l.rgb;
    return i;
}

// Area-average downscale (or nearest upscale) to dw x dh.
Image resized(const Image& s, int dw, int dh) {
    Image d(dw, dh);
    for (int y = 0; y < dh; ++y) {
        const int y0 = y * s.h / dh, y1 = std::max(y0 + 1, (y + 1) * s.h / dh);
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * s.w / dw, x1 = std::max(x0 + 1, (x + 1) * s.w / dw);
            int acc[3] = {0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1 && yy < s.h; ++yy)
                for (int xx = x0; xx < x1 && xx < s.w; ++xx) {
                    const uint8_t* p = &s.px[(static_cast<size_t>(yy) * s.w + xx) * 3];
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2];
                    ++n;
                }
            uint8_t* o = &d.px[(static_cast<size_t>(y) * dw + x) * 3];
            for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(acc[k] / std::max(n, 1));
        }
    }
    return d;
}

// Fits 's' into a dw x dh box keeping the aspect ratio (black bars).
Image fitted(const Image& s, int dw, int dh) {
    const float k = std::min(static_cast<float>(dw) / s.w, static_cast<float>(dh) / s.h);
    const int w = std::max(1, static_cast<int>(s.w * k)), h = std::max(1, static_cast<int>(s.h * k));
    const Image r = resized(s, w, h);
    Image d(dw, dh);
    const int ox = (dw - w) / 2, oy = (dh - h) / 2;
    for (int y = 0; y < h; ++y)
        std::copy(r.px.begin() + static_cast<size_t>(y) * w * 3, r.px.begin() + static_cast<size_t>(y + 1) * w * 3,
                  d.px.begin() + (static_cast<size_t>(y + oy) * dw + ox) * 3);
    return d;
}

void blit(Image& dst, const Image& src, int ox, int oy) {
    for (int y = 0; y < src.h; ++y) {
        const int yy = y + oy;
        if (yy < 0 || yy >= dst.h) continue;
        for (int x = 0; x < src.w; ++x) {
            const int xx = x + ox;
            if (xx < 0 || xx >= dst.w) continue;
            std::copy_n(&src.px[(static_cast<size_t>(y) * src.w + x) * 3], 3, &dst.px[(static_cast<size_t>(yy) * dst.w + xx) * 3]);
        }
    }
}

bool save(Ctx& c, const std::string& name, const Image& imgIn) {
    // report contract: PNGs are at most 1600 px wide
    const Image img = imgIn.w > 1600 ? resized(imgIn, 1600, imgIn.h * 1600 / imgIn.w) : imgIn;
    const std::string path = c.dir + "/" + name;
    const bool ok = writePng(path, img.w, img.h, img.px);
    std::printf("%s %s (%dx%d)\n", ok ? "wrote" : "FAILED", path.c_str(), img.w, img.h);
    if (c.log) *c.log << "lab figure " << name << " " << img.w << "x" << img.h << "\n";
    return ok;
}

// ------------------------------------------------------------------------------------------------ canvas
// A plain RGB render target for Hud drawing; several begin / end layers can be stacked (rects, then image, then lines).
struct Canvas {
    explicit Canvas(Renderer& r) : r_(r) {}
    void open(int W, int H, Rgba bg = {0, 0, 0, 1}) {
        if (W != w || H != h) {
            t.create(W, H, 0, false);
            w = W;
            h = H;
        }
        t.bind();
        glClearColor(bg.r, bg.g, bg.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void layer(const std::function<void(Hud&)>& f) {
        t.bind();
        r_.hud().begin(w, h);
        f(r_.hud());
        r_.hud().end();
    }
    Image read() {
        Image i;
        i.w = w;
        i.h = h;
        t.read(i.px);
        return i;
    }
    RenderTarget t;
    int w = 0, h = 0;

private:
    Renderer& r_;
};

struct Label {
    float x, y;
    std::string text;
    float size;
    Rgba color;
};

float textW(Renderer& r, const std::string& s, float size) { return r.hud().textWidth(s, size); }

// Draws labels onto a CPU image (dark backing box, then the glyphs through a mask render).
void overlayText(Ctx& c, Canvas& cv, Image& img, const std::vector<Label>& labels, float backing = 0.45f) {
    for (const Label& l : labels) {
        const int x0 = std::max(0, static_cast<int>(l.x) - 4), y0 = std::max(0, static_cast<int>(l.y) - 2);
        const int x1 = std::min(img.w, static_cast<int>(l.x + textW(*c.renderer, l.text, l.size)) + 5);
        const int y1 = std::min(img.h, static_cast<int>(l.y + l.size) + 3);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                uint8_t* p = &img.px[(static_cast<size_t>(y) * img.w + x) * 3];
                for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(p[k] * backing);
            }
    }
    for (const Label& l : labels) {
        cv.open(img.w, img.h);
        cv.layer([&](Hud& h) { h.text(l.x, l.y, l.text, l.size, {1, 1, 1, 1}); });
        const Image mask = cv.read();
        for (size_t i = 0; i < img.px.size(); i += 3) {
            const float a = static_cast<float>(mask.px[i + 1]) / 255.0f;
            if (a <= 0.0f) continue;
            img.px[i] = static_cast<uint8_t>(img.px[i] * (1 - a) + 255.0f * l.color.r * a);
            img.px[i + 1] = static_cast<uint8_t>(img.px[i + 1] * (1 - a) + 255.0f * l.color.g * a);
            img.px[i + 2] = static_cast<uint8_t>(img.px[i + 2] * (1 - a) + 255.0f * l.color.b * a);
        }
    }
}

// ------------------------------------------------------------------------------------------------ rendering
CameraView makeView(const Vec3& eye, const Vec3& aim, float fovDeg, float aspect, float nearZ = 0.05f) {
    CameraView v;
    v.position = eye;
    v.view = lookAt(eye, aim, {0, 1, 0});
    v.nearZ = nearZ;
    v.farZ = 600.0f;
    v.proj = perspective(radians(fovDeg), aspect, v.nearZ, v.farZ);
    return v;
}

Quality stageQuality(const Ctx& c) { return c.qualityForced ? c.quality : std::min(c.quality, Quality::High); }

RenderSettings stageSettings(const Ctx& c) {
    RenderSettings s;
    s.view = ViewMode::Visible;
    s.quality = stageQuality(c);
    s.isolate = LabStage::kObject;  // grass, foliage, rain and the combat effects stay off for isolated figures
    s.fogOn = false;
    s.taaOn = false;
    s.spotOn = false;
    s.practicalsOn = false;
    s.foliageOn = false;
    s.particlesOn = false;
    s.flareOn = false;  // lens ghosts of the lamp would clutter the test objects
    return s;
}

// Renders 'frames' frames of the scene, then lets 'after' draw / read, and returns the target as an image.
Image renderView(Ctx& c, Canvas& cv, const CameraView& cam, const RenderSettings& s, int w, int h, int frames,
                 const std::function<void(Hud&)>& overlay = {}, const std::function<void()>& after = {}) {
    cv.open(w, h);
    c.renderer->resetHistory();
    FrameStats st;
    for (int f = 0; f < frames; ++f) {
        cv.t.bind();
        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        st = {};
        c.renderer->beginFrame(*c.scene, cam, w, h, s, st);
        c.renderer->endFrame(static_cast<GLuint>(fbo), w, h, c.scene->time());
    }
    if (after) after();
    if (overlay) {
        cv.t.bind();
        c.renderer->hud().begin(w, h);
        overlay(c.renderer->hud());
        c.renderer->hud().end();
    }
    return cv.read();
}

Vec3 normalized(const Vec3& v) { return normalize(v); }

void lightRig(Scene& sc, const Vec3& moonTravel, float moonIntensity, const Vec3& lampPos, const Vec3& lampColor, float lampIntensity) {
    sc.lights.moon.direction = normalized(moonTravel);
    sc.lights.moon.intensity = moonIntensity;
    sc.lights.beacon.position = lampPos;
    sc.lights.beacon.color = lampColor;
    sc.lights.beacon.intensity = lampIntensity;
    sc.lights.beacon.linear = 0.02f;
    sc.lights.beacon.quadratic = 0.012f;
}

Rgba grey(float v, float a = 1.0f) { return {v, v, v, a}; }

}  // namespace

// ================================================================================================ 2. material grid
bool materials(Ctx& c) {
    Scene& sc = *c.scene;
    Canvas cv(*c.renderer);
    LabStage st;
    const Vec3 copper{0.80f, 0.46f, 0.28f};
    const float rough[5] = {0.1f, 0.3f, 0.5f, 0.7f, 0.9f}, metal[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float pitch = 1.2f;
    Material wall;
    wall.baseColor = {0.20f, 0.21f, 0.23f};
    wall.roughness = 0.9f;
    st.add(PrimitiveType::Cube, wall, {0.0f, 2.4f, -1.3f}, {16.0f, 10.0f, 0.2f}, {0, 0, 0}, ShadingModel::Phong, "backdrop");
    for (int r = 0; r < 5; ++r)
        for (int k = 0; k < 5; ++k) {
            Material m;
            m.baseColor = copper;
            m.roughness = rough[k];
            m.metalness = metal[r];
            m.heat = 0.2f;
            st.add(PrimitiveType::Sphere, m, {(k - 2) * pitch, 4.8f - r * pitch, 0.0f}, {1.0f, 1.0f, 1.0f}, {0, 0, 0},
                   ShadingModel::Phong, "grid sphere");
        }
    // Second stage: Blinn-Phong row (uPbr = 0): specular 0.6, shininess 4 / 16 / 64 / 256.
    LabStage row;
    row.add(PrimitiveType::Cube, wall, {0.0f, 1.0f, -1.3f}, {16.0f, 6.0f, 0.2f}, {0, 0, 0}, ShadingModel::Phong, "backdrop");
    const float shin[4] = {4.0f, 16.0f, 64.0f, 256.0f};
    for (int k = 0; k < 4; ++k) {
        Material m;
        m.baseColor = copper;
        m.specular = 0.6f;
        m.shininess = shin[k];
        row.add(PrimitiveType::Sphere, m, {(k - 1.5f) * pitch, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0, 0, 0}, ShadingModel::Phong, "row sphere");
    }
    RenderSettings s = stageSettings(c);
    const int W = 1500, H = 1200;
    Image grid, rowImg;
    {
        st.install(sc);
        c.renderer->labInvalidateCaches();
        lightRig(sc, {0.45f, -0.55f, -0.7f}, 3.0f, {3.2f, 4.6f, 6.0f}, {1.0f, 0.80f, 0.60f}, 9.0f);
        const CameraView cam = makeView({0.0f, 2.4f, 19.0f}, {0.0f, 2.4f, 0.0f}, 24.0f, static_cast<float>(W) / H);
        const Mat4 vp = cam.proj * cam.view;
        std::vector<Label> labels;
        labels.push_back({20, 14, "PBR GGX / Cook-Torrance: roughness (columns) x metalness (rows), albedo copper", 26, grey(1)});
        labels.push_back({20, 48, "lights: moon (directional, cool) + one point lamp (warm, right); uPbr = 1", 20, grey(0.8f)});
        for (int k = 0; k < 5; ++k) {
            const MarkerProjection p = projectMarker(vp, cam.position, {(k - 2) * pitch, 4.8f + 0.6f, 0}, W, H);
            char b[40];
            std::snprintf(b, sizeof(b), "rough %.1f", rough[k]);
            labels.push_back({p.x - 48, p.y - 18, b, 22, {1, 0.9f, 0.5f, 1}});
        }
        for (int r = 0; r < 5; ++r) {
            const MarkerProjection p = projectMarker(vp, cam.position, {-2.0f * pitch - 0.62f, 4.8f - r * pitch, 0}, W, H);
            char b[40];
            std::snprintf(b, sizeof(b), "metal %.2f", metal[r]);
            labels.push_back({p.x - 130, p.y - 10, b, 22, {0.6f, 0.9f, 1, 1}});
        }
        grid = renderView(c, cv, cam, s, W, H, 1);
        overlayText(c, cv, grid, labels);
        st.uninstall(sc);
    }
    {
        row.install(sc);
        c.renderer->labInvalidateCaches();
        lightRig(sc, {0.45f, -0.55f, -0.7f}, 3.0f, {3.2f, 4.6f, 6.0f}, {1.0f, 0.80f, 0.60f}, 9.0f);
        RenderSettings sb = s;
        sb.pbr = false;
        const int H2 = 420;
        const CameraView cam = makeView({0.0f, 1.0f, 19.0f}, {0.0f, 1.0f, 0.0f}, 8.4f, static_cast<float>(W) / H2);
        const Mat4 vp = cam.proj * cam.view;
        std::vector<Label> labels;
        labels.push_back({20, 12, "Blinn-Phong (uPbr = 0): specular 0.6, shininess n = 4, 16, 64, 256", 24, grey(1)});
        for (int k = 0; k < 4; ++k) {
            const MarkerProjection p = projectMarker(vp, cam.position, {(k - 1.5f) * pitch, 0.0f, 0}, W, H2);
            char b[40];
            std::snprintf(b, sizeof(b), "n = %d", static_cast<int>(shin[k]));
            labels.push_back({p.x - 40, H2 - 44.0f, b, 24, {1, 0.9f, 0.5f, 1}});
        }
        rowImg = renderView(c, cv, cam, sb, W, H2, 1);
        overlayText(c, cv, rowImg, labels);
        row.uninstall(sc);
    }
    c.renderer->labInvalidateCaches();
    Image both(W, grid.h + rowImg.h);
    blit(both, grid, 0, 0);
    blit(both, rowImg, 0, grid.h);
    bool ok = save(c, "materials_pbr_grid.png", grid);
    ok = save(c, "materials_blinnphong_row.png", rowImg) && ok;
    ok = save(c, "materials_grid.png", both) && ok;
    ok = save(c, "material_grid.png", both) && ok;
    return ok;
}

// ================================================================================================ 3. shading models
bool shading(Ctx& c) {
    Scene& sc = *c.scene;
    Canvas cv(*c.renderer);
    bool ok = true;
    const char* names[4] = {"FLAT: one normal per triangle, light evaluated once per face",
                            "GOURAUD: light evaluated per vertex, colours interpolated",
                            "PHONG: normals interpolated, light evaluated per pixel", "WIREFRAME: the triangles (hidden-line)"};
    const ShadingOverride modes[4] = {ShadingOverride::Flat, ShadingOverride::Gouraud, ShadingOverride::Phong, ShadingOverride::PerObject};
    // ---- A. the UAV (a hierarchical composite of unit primitives)
    {
        const int W = 900, H = 675;
        Camera cam;
        cam.mode = CameraMode::Orbit;
        cam.target = transformPoint(sc.uav().root->world, {0, 0, 0});
        cam.yawDeg = 150.0f;
        cam.pitchDeg = 24.0f;
        cam.distance = 0.72f;
        const CameraView view = cam.compute(sc, static_cast<float>(W) / H);
        const Vec3 fwd = normalize(cam.target - view.position);
        const Vec3 right = normalize(cross(fwd, {0, 1, 0}));
        const LightRig saved = sc.lights;
        lightRig(sc, fwd + right * 0.55f + Vec3{0, -0.65f, 0}, 2.2f, view.position + Vec3{0, 0.6f, 0} - right * 0.9f,
                 {1.0f, 0.85f, 0.7f}, 1.2f);
        Image panels[4];
        for (int i = 0; i < 4; ++i) {
            RenderSettings s;
            s.view = ViewMode::Visible;
            s.quality = stageQuality(c);
            s.isolate = "Autonomous UAV";
            s.fogOn = false;
            s.taaOn = false;
            s.spotOn = false;
            s.practicalsOn = false;
            s.flareOn = false;
            s.shading = modes[i];
            if (i == 3) {
                s.view = ViewMode::Diagnostic;
                s.edges = EdgeMode::HiddenLine;
            }
            c.renderer->labInvalidateCaches();
            panels[i] = renderView(c, cv, view, s, W, H, 1);
            overlayText(c, cv, panels[i], {{14, 10, names[i], 21, grey(1)}});
        }
        sc.lights = saved;
        Image m(2 * W, 2 * H);
        for (int i = 0; i < 4; ++i) blit(m, panels[i], (i % 2) * W, (i / 2) * H);
        ok = save(c, "shading_uav_flat.png", panels[0]) && ok;
        ok = save(c, "shading_uav_gouraud.png", panels[1]) && ok;
        ok = save(c, "shading_uav_phong.png", panels[2]) && ok;
        ok = save(c, "shading_uav_wire.png", panels[3]) && ok;
        ok = save(c, "shading_uav_compare.png", m) && ok;
        ok = save(c, "shading_models.png", m) && ok;
    }
    // ---- B. a coarse sphere (10 x 6) and the stock sphere (36 x 18): Mach bands, facets, highlight
    {
        const int W = 640, H = 640;
        Image tiles[8];
        const int res[2][2] = {{10, 6}, {36, 18}};
        for (int row = 0; row < 2; ++row) {
            c.renderer->labSphereResolution(res[row][0], res[row][1]);
            LabStage st;
            Material m;
            m.baseColor = {0.55f, 0.62f, 0.72f};
            m.specular = 0.9f;
            m.shininess = 48.0f;
            m.roughness = 0.35f;
            st.add(PrimitiveType::Sphere, m, {0, 0, 0}, {2.0f, 2.0f, 2.0f}, {0, 0, 0}, ShadingModel::Phong, "sphere");
            Material bg;
            bg.baseColor = {0.05f, 0.05f, 0.06f};
            bg.roughness = 1.0f;
            st.add(PrimitiveType::Cube, bg, {0, 0, -2.4f}, {12.0f, 12.0f, 0.2f}, {0, 0, 0}, ShadingModel::Phong, "backdrop");
            st.install(sc);
            lightRig(sc, {0.35f, -0.45f, -0.8f}, 2.4f, {1.7f, 1.6f, 2.6f}, {1.0f, 0.95f, 0.85f}, 3.0f);
            const CameraView view = makeView({0, 0.2f, 6.2f}, {0, 0, 0}, 30.0f, 1.0f);
            for (int i = 0; i < 4; ++i) {
                RenderSettings s = stageSettings(c);
                s.pbr = false;  // classic Blinn-Phong terms: the highlight is the clearest test of the interpolation
                s.shading = modes[i];
                if (i == 3) s.edges = EdgeMode::HiddenLine;
                c.renderer->labInvalidateCaches();
                char b[64];
                std::snprintf(b, sizeof(b), "%dx%d sphere: %s", res[row][0], res[row][1], i == 0 ? "flat" : i == 1 ? "Gouraud" : i == 2 ? "Phong" : "triangles");
                tiles[row * 4 + i] = renderView(c, cv, view, s, W, H, 1);
                overlayText(c, cv, tiles[row * 4 + i], {{12, 10, b, 24, grey(1)}});
            }
            st.uninstall(sc);
        }
        c.renderer->labSphereResolution(0, 0);
        c.renderer->labInvalidateCaches();
        Image m(4 * W, 2 * H);
        for (int i = 0; i < 8; ++i) blit(m, tiles[i], (i % 4) * W, (i / 4) * H);
        ok = save(c, "shading_sphere_compare.png", m) && ok;
        // The coarse sphere on its own, larger, for the Mach-band close-up
        ok = save(c, "shading_sphere_lowpoly_gouraud.png", tiles[1]) && ok;
        ok = save(c, "shading_sphere_lowpoly_flat.png", tiles[0]) && ok;
        ok = save(c, "shading_sphere_lowpoly_phong.png", tiles[2]) && ok;
    }
    return ok;
}

// ================================================================================================ 4. surface relief
bool relief(Ctx& c) {
    Scene& sc = *c.scene;
    Canvas cv(*c.renderer);
    LabStage st;
    struct Slab { const char* name; Surface surf; Vec3 colour; float rough, metal; };
    const Slab slabs[6] = {{"corrugated metal", Surface::Corrugated, {0.30f, 0.33f, 0.27f}, 0.45f, 0.6f},
                           {"steel plate (seams, rivets)", Surface::SteelPlate, {0.50f, 0.52f, 0.55f}, 0.5f, 0.3f},
                           {"HESCO (mesh + bags)", Surface::Hesco, {0.45f, 0.38f, 0.26f}, 0.9f, 0.0f},
                           {"concrete", Surface::Concrete, {0.30f, 0.30f, 0.29f}, 0.85f, 0.0f},
                           {"rock", Surface::Rock, {0.25f, 0.23f, 0.21f}, 0.9f, 0.0f},
                           {"asphalt", Surface::Asphalt, {0.25f, 0.25f, 0.27f}, 0.85f, 0.0f}};
    const float pitchX = 2.35f, pitchY = 1.75f;
    for (int i = 0; i < 6; ++i) {
        Material m;
        m.baseColor = slabs[i].colour;
        m.roughness = slabs[i].rough;
        m.metalness = slabs[i].metal;
        m.surface = slabs[i].surf;
        st.add(PrimitiveType::Cube, m, {((i % 3) - 1) * pitchX, 3.4f - (i / 3) * pitchY, 0.0f}, {2.1f, 1.6f, 0.15f}, {0, 0, 0},
               ShadingModel::Phong, slabs[i].name);
    }
    Material bg;
    bg.baseColor = {0.03f, 0.03f, 0.035f};
    bg.roughness = 1.0f;
    st.add(PrimitiveType::Cube, bg, {0, 2.0f, -0.4f}, {14.0f, 9.0f, 0.2f}, {0, 0, 0}, ShadingModel::Phong, "backdrop");
    st.install(sc);
    c.renderer->labInvalidateCaches();
    // Raking light from the left (a low sun / lamp shows relief best), plus a weak lamp from the right.
    lightRig(sc, {0.88f, -0.22f, -0.42f}, 3.4f, {-10.0f, 3.4f, 2.0f}, {1.0f, 0.85f, 0.65f}, 2.5f);
    const int W = 1500, H = 900;
    const CameraView cam = makeView({0.0f, 3.0f, 8.6f}, {0.0f, 2.6f, 0.0f}, 36.0f, static_cast<float>(W) / H);
    Image img[2];
    for (int on = 0; on < 2; ++on) {
        RenderSettings s = stageSettings(c);
        s.bumpOn = on == 1;
        s.shadowsOn = false;  // the slabs would shadow each other; only the relief is under test
        s.ssaoOn = false;
        img[on] = renderView(c, cv, cam, s, W, H, 1);
        std::vector<Label> labels;
        labels.push_back({16, 10, on ? "procedural surface relief ON (surfaceBump)" : "relief OFF (flat faces, albedo pattern only)", 28, on ? Rgba{0.6f, 1, 0.6f, 1} : Rgba{1, 0.7f, 0.5f, 1}});
        const Mat4 vp = cam.proj * cam.view;
        for (int i = 0; i < 6; ++i) {
            const MarkerProjection p = projectMarker(vp, cam.position, {((i % 3) - 1) * pitchX - 0.95f, 3.4f - (i / 3) * pitchY - 0.72f, 0.1f}, W, H);
            labels.push_back({p.x, p.y - 22, slabs[i].name, 20, grey(1)});
        }
        overlayText(c, cv, img[on], labels);
    }
    st.uninstall(sc);
    c.renderer->labInvalidateCaches();
    Image both(W, 2 * H);
    blit(both, img[0], 0, 0);
    blit(both, img[1], 0, H);
    bool ok = save(c, "relief_off.png", img[0]);
    ok = save(c, "relief_on.png", img[1]) && ok;
    ok = save(c, "relief_compare.png", both) && ok;
    ok = save(c, "bump_on_off.png", both) && ok;
    ok = save(c, "surface_patterns.png", img[0]) && ok;
    return ok;
}

// ================================================================================================ 5. reflections
bool reflections(Ctx& c) {
    Scene& sc = *c.scene;
    Canvas cv(*c.renderer);
    const float wasWet = sc.wetness;
    sc.wetness = 1.0f;
    const int W = 1280, H = 720;
    const Quality q = c.qualityForced ? c.quality : Quality::Ultra;  // SSR needs ULTRA / EPIC
    const CameraView cam = makeView({0.6f, 0.5f, 31.0f}, {0.5f, 0.45f, 16.0f}, 62.0f, static_cast<float>(W) / H);
    Image off, on, refl, norm;
    for (int pass = 0; pass < 2; ++pass) {
        RenderSettings s;
        s.view = ViewMode::Visible;
        s.quality = q;
        s.rain = 1.0f;
        s.ssrOn = pass == 1;
        c.renderer->labInvalidateCaches();
        Image im = renderView(c, cv, cam, s, W, H, 4, {}, [&]() {
            if (pass == 1) {
                LabImage li;
                if (c.renderer->labReadBuffer(LabBuffer::Reflectivity, li)) refl = fromLab(li);
                if (c.renderer->labReadBuffer(LabBuffer::SurfaceNormal, li)) norm = fromLab(li);
            }
        });
        (pass ? on : off) = im;
    }
    sc.wetness = wasWet;
    c.renderer->labInvalidateCaches();
    // Difference (ON - OFF), amplified: where the reflections add light
    Image diff(W, H);
    for (size_t i = 0; i < diff.px.size(); ++i) {
        const int d = std::abs(static_cast<int>(on.px[i]) - static_cast<int>(off.px[i]));
        diff.px[i] = static_cast<uint8_t>(std::min(255, d * 4));
    }
    overlayText(c, cv, off, {{14, 10, "screen-space reflections OFF (same frame)", 28, grey(1)}});
    overlayText(c, cv, on, {{14, 10, "screen-space reflections ON (rain, wet ground)", 28, grey(1)}});
    overlayText(c, cv, diff, {{14, 10, "difference |ON - OFF| x 4", 28, grey(1)}});
    if (!refl.px.empty()) overlayText(c, cv, refl, {{14, 10, "reflectivity buffer (surface target alpha)", 28, {1, 0.9f, 0.4f, 1}}});
    Image m(2 * W, 2 * H);
    blit(m, off, 0, 0);
    blit(m, on, W, 0);
    blit(m, refl.px.empty() ? diff : refl, 0, H);
    blit(m, diff, W, H);
    bool ok = save(c, "reflections_ssr_off.png", off);
    ok = save(c, "reflections_ssr_on.png", on) && ok;
    if (!refl.px.empty()) ok = save(c, "reflections_reflectivity_buffer.png", refl) && ok;
    ok = save(c, "reflections_difference.png", diff) && ok;
    ok = save(c, "reflections_compare.png", m) && ok;
    ok = save(c, "ssr_on_off.png", m) && ok;
    return ok;
}

// ================================================================================================ 7. pipeline buffers
bool buffers(Ctx& c) {
    Scene& sc = *c.scene;
    Canvas cv(*c.renderer);
    const int W = 1600, H = 900;
    const Quality q = c.qualityForced ? c.quality : Quality::Ultra;
    // Camera on the compound's motor-pool side: moon shadows, practical lamps, fog, a wet metal roof, bloom.
    const CameraView cam = makeView({-9.0f, 6.5f, 40.0f}, {3.0f, 0.5f, 12.0f}, 58.0f, static_cast<float>(W) / H);
    RenderSettings s;
    s.view = ViewMode::Visible;
    s.quality = q;
    struct Tile { LabBuffer b; const char* file; const char* title; };
    const Tile tiles[] = {{LabBuffer::ShadowMoonNear, "buffer_01_shadow_near.png", "1 moon shadow map, near cascade (depth)"},
                          {LabBuffer::ShadowMoonWorld, "buffer_02_shadow_world.png", "2 moon shadow map, world cascade (depth)"},
                          {LabBuffer::Depth, "buffer_03_depth.png", "3 scene depth (resolved)"},
                          {LabBuffer::SurfaceNormal, "buffer_04_normals.png", "4 surface target: world normal"},
                          {LabBuffer::Ssao, "buffer_05_ssao.png", "5 SSAO (blurred)"},
                          {LabBuffer::FogInscatter, "buffer_06_fog_inscatter.png", "6 volumetric fog: in-scattered light"},
                          {LabBuffer::FogTransmittance, "buffer_07_fog_transmittance.png", "7 volumetric fog: transmittance"},
                          {LabBuffer::HdrColour, "buffer_08_hdr_colour.png", "8 HDR scene (before tone mapping)"},
                          {LabBuffer::HdrLogLuminance, "buffer_09_hdr_loglum.png", "9 HDR log2 luminance, false colour"},
                          {LabBuffer::Bloom, "buffer_10_bloom.png", "10 bloom (quarter-res mip 0)"},
                          {LabBuffer::Reflectivity, "buffer_11_reflectivity.png", "11 surface target: reflectivity"}};
    std::vector<std::pair<Tile, Image>> got;
    const float wasWet = sc.wetness;
    sc.wetness = 0.6f;  // damp ground: some reflectivity to show
    c.renderer->labInvalidateCaches();
    Image fin = renderView(c, cv, cam, s, W, H, 8, {}, [&]() {
        for (const Tile& t : tiles) {
            LabImage li;
            if (c.renderer->labReadBuffer(t.b, li)) {
                Image im = fromLab(li);
                if (c.log) *c.log << "buffer " << t.file << ": " << li.note << "\n";
                std::printf("  %s: %s\n", t.file, li.note.c_str());
                got.push_back({t, im});
            }
        }
    });
    sc.wetness = wasWet;
    bool ok = save(c, "buffer_12_final.png", fin);
    // each buffer at (at most) 1600 px wide for the individual files; the shadow maps are square (downscaled)
    for (auto& [t, im] : got) {
        Image out = im;
        const int maxw = 1600;
        if (im.w > maxw) out = resized(im, maxw, im.h * maxw / im.w);
        ok = save(c, t.file, out) && ok;
    }
    // overview: 4 x 3 tiles of 560 x 315 with titles
    const int TW = 560, TH = 315;
    Image m(4 * TW, 3 * TH);
    std::vector<Label> labels;
    int idx = 0;
    for (auto& [t, im] : got) {
        const int ox = (idx % 4) * TW, oy = (idx / 4) * TH;
        blit(m, fitted(im, TW, TH), ox, oy);
        labels.push_back({static_cast<float>(ox + 8), static_cast<float>(oy + 6), t.title, 17, grey(1)});
        ++idx;
    }
    blit(m, fitted(fin, TW, TH), (idx % 4) * TW, (idx / 4) * TH);
    labels.push_back({static_cast<float>((idx % 4) * TW + 8), static_cast<float>((idx / 4) * TH + 6), "12 final frame (tone mapped, TAA, flare, grain)", 17, grey(1)});
    // 1px tile borders
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x)
            if (x % TW == 0 || y % TH == 0) std::fill_n(&m.px[(static_cast<size_t>(y) * m.w + x) * 3], 3, 90);
    overlayText(c, cv, m, labels);
    ok = save(c, "buffers_overview.png", m) && ok;
    ok = save(c, "pipeline_buffers.png", m) && ok;
    // shadow cascades side by side
    Image near_, world_;
    for (auto& [t, im] : got) {
        if (t.b == LabBuffer::ShadowMoonNear) near_ = im;
        if (t.b == LabBuffer::ShadowMoonWorld) world_ = im;
    }
    if (!near_.px.empty() && !world_.px.empty()) {
        Image sc2(1600, 800);
        blit(sc2, fitted(near_, 800, 800), 0, 0);
        blit(sc2, fitted(world_, 800, 800), 800, 0);
        overlayText(c, cv, sc2, {{10, 8, "moon shadow map, near cascade (follows the camera, 0-14 m)", 22, grey(1)}, {810, 8, "moon shadow map, world cascade (whole compound, static cache)", 22, grey(1)}});
        ok = save(c, "shadow_cascades.png", sc2) && ok;
    }
    return ok;
}

// ================================================================================================ 1. CV pipeline
namespace {

constexpr int kW = 320, kH = 240;

Rgba hashColour(int label) {
    uint32_t h = static_cast<uint32_t>(label) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    const float r = 0.25f + 0.75f * static_cast<float>(h & 255) / 255.0f, g = 0.25f + 0.75f * static_cast<float>((h >> 8) & 255) / 255.0f,
                b = 0.25f + 0.75f * static_cast<float>((h >> 16) & 255) / 255.0f;
    return {r, g, b, 1.0f};
}

GLuint heatTexture(const std::vector<uint8_t>& img) {
    std::vector<uint8_t> flipped(img.size());
    for (int y = 0; y < kH; ++y) std::copy_n(&img[static_cast<size_t>(y) * kW], kW, &flipped[static_cast<size_t>(kH - 1 - y) * kW]);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kW, kH, 0, GL_RED, GL_UNSIGNED_BYTE, flipped.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

void thickFrame(Hud& h, float x, float y, float w, float hh, Rgba c, int n = 2) {
    for (int i = 0; i < n; ++i) h.frame(x - i, y - i, w + 2 * i, hh + 2 * i, c);
}

void crossMark(Hud& h, float x, float y, float r, Rgba c) {
    h.line(x - r, y, x + r, y, c);
    h.line(x, y - r, x, y + r, c);
}

struct Moments {
    double cx = 0, cy = 0, l1 = 0, l2 = 0, theta = 0;
    bool ok = false;
};

Moments momentsOf(const cv::CvDebug& d, const cv::CvComponent& comp) {
    Moments m;
    double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    for (int y = comp.minY; y <= comp.maxY; ++y)
        for (int x = comp.minX; x <= comp.maxX; ++x)
            if (d.labels[static_cast<size_t>(y) * kW + x] == comp.label) {
                n += 1; sx += x; sy += y; sxx += static_cast<double>(x) * x; syy += static_cast<double>(y) * y; sxy += static_cast<double>(x) * y;
            }
    if (n <= 0) return m;
    m.cx = sx / n;
    m.cy = sy / n;
    const double mu20 = sxx / n - m.cx * m.cx, mu02 = syy / n - m.cy * m.cy, mu11 = sxy / n - m.cx * m.cy;
    const double tr = mu20 + mu02, det = mu20 * mu02 - mu11 * mu11, disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
    m.l1 = std::max(tr / 2 + disc, 1.0 / 12);
    m.l2 = std::max(tr / 2 - disc, 1.0 / 12);
    m.theta = 0.5 * std::atan2(2 * mu11, mu20 - mu02);
    m.ok = true;
    return m;
}

enum class Panel { RawIron, RawGrey, Hist, Mask, Despeckle, Labels, Overlay };

const char* panelTitle(Panel p) {
    switch (p) {
        case Panel::RawIron: return "a  raw heat image (ironbow)";
        case Panel::RawGrey: return "a  raw heat image (grey, 0..255)";
        case Panel::Hist: return "b0  histogram, log counts: Otsu + fixed floor";
        case Panel::Mask: return "b  threshold mask";
        case Panel::Despeckle: return "c  despeckle (removed = red)";
        case Panel::Labels: return "d  connected components (8-conn.)";
        case Panel::Overlay: return "e  centroid + box + principal axes";
    }
    return "";
}

Image cvPanel(Ctx& c, Canvas& cv, const CvView& v, Panel p, int S) {
    const int W = kW * S, H = kH * S;
    const float fs = static_cast<float>(S);
    cv.open(W, H);
    const CvFrameStats& st = v.dbg.stats;
    GLuint tex = 0;
    if (p == Panel::RawIron || p == Panel::RawGrey || p == Panel::Overlay) tex = heatTexture(v.img);
    // background / layers
    if (p == Panel::RawIron || p == Panel::RawGrey || p == Panel::Overlay) {
        cv.layer([&](Hud& h) {
            h.setPalette(p == Panel::RawGrey ? 1 : 0);
            h.heatImage(0, 0, static_cast<float>(W), static_cast<float>(H), tex);
        });
        if (p == Panel::Overlay) {  // dim the image so the overlay reads
            cv.layer([&](Hud& h) { h.rect(0, 0, static_cast<float>(W), static_cast<float>(H), {0, 0, 0, 0.35f}); });
        }
    } else if (p == Panel::Mask || p == Panel::Despeckle || p == Panel::Labels) {
        cv.layer([&](Hud& h) {
            h.rect(0, 0, static_cast<float>(W), static_cast<float>(H), {0.03f, 0.03f, 0.04f, 1});
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x) {
                    const size_t i = static_cast<size_t>(y) * kW + x;
                    Rgba col{0, 0, 0, 0};
                    if (p == Panel::Mask) {
                        if (v.dbg.mask[i]) col = {0.92f, 0.92f, 0.92f, 1};
                    } else if (p == Panel::Despeckle) {
                        if (v.dbg.clean[i]) col = {0.92f, 0.92f, 0.92f, 1};
                        else if (v.dbg.removed[i]) col = {1.0f, 0.1f, 0.1f, 1};
                    } else if (v.dbg.labels[i]) {
                        col = hashColour(v.dbg.labels[i]);
                    }
                    if (col.a > 0) h.rect(x * fs, y * fs, fs, fs, col);
                }
        });
    }
    std::vector<Label> texts;
    cv.layer([&](Hud& h) {
        if (p == Panel::Hist) {
            h.rect(0, 0, static_cast<float>(W), static_cast<float>(H), {0.05f, 0.05f, 0.07f, 1});
            double hist[256] = {};
            for (uint8_t px : v.img) hist[px] += 1;
            double mx = 1;
            for (double b : hist) mx = std::max(mx, std::log10(1 + b));
            const float x0 = 20 * fs / 2, x1 = W - 20 * fs / 2, y0 = H - 26 * fs / 2, y1 = 46 * fs / 2;
            for (int b = 0; b < 256; ++b) {
                const float x = x0 + (x1 - x0) * b / 256.0f, w = (x1 - x0) / 256.0f;
                const float hh = static_cast<float>(std::log10(1 + hist[b]) / mx) * (y0 - y1);
                h.rect(x, y0 - hh, std::max(w, 1.0f), hh, b >= st.threshold ? Rgba{1, 0.55f, 0.1f, 1} : Rgba{0.4f, 0.55f, 0.9f, 1});
            }
            auto vline = [&](int level, Rgba col) {
                const float x = x0 + (x1 - x0) * (level + 0.5f) / 256.0f;
                h.line(x, y1 - 10, x, y0, col);
                h.line(x + 1, y1 - 10, x + 1, y0, col);
            };
            vline(CvConfig{}.floor, {1, 1, 0.2f, 1});
            vline(st.otsu, {0.3f, 1, 0.5f, 1});
            vline(st.threshold, {1, 1, 1, 1});
            h.line(x0, y0, x1, y0, {0.8f, 0.8f, 0.8f, 1});
        }
    });
    if (p == Panel::Hist) {
        const float ts = 8.0f + 4.0f * fs;
        char b[200];
        std::snprintf(b, sizeof(b), "yellow: fixed floor %d   green: Otsu %d   white: used t = max(floor, Otsu+1) = %d", CvConfig{}.floor, st.otsu, st.threshold);
        texts.push_back({10.0f * fs / 2, H - 20.0f * fs / 2 - ts * 0.2f - 6, b, ts * 0.8f, grey(1)});
        std::snprintf(b, sizeof(b), "pixels >= t: %d of %d  (bins >= t in orange)", st.hotPixels, kW * kH);
        texts.push_back({10.0f * fs / 2, 30.0f * fs / 2, b, ts * 0.8f, {1, 0.7f, 0.3f, 1}});
    }
    if (p == Panel::Overlay) {
        cv.layer([&](Hud& h) {
            for (const cv::CvComponent& comp : v.dbg.components) {
                const float x = comp.minX * fs, y = comp.minY * fs, w = (comp.maxX - comp.minX + 1) * fs, hh = (comp.maxY - comp.minY + 1) * fs;
                const Moments m = momentsOf(v.dbg, comp);
                if (comp.accepted) {
                    thickFrame(h, x - 2, y - 2, w + 4, hh + 4, {0.1f, 0.9f, 1, 1}, 2);
                    if (m.ok) {
                        const float cx = (static_cast<float>(m.cx) + 0.5f) * fs, cy = (static_cast<float>(m.cy) + 0.5f) * fs;
                        const float a1 = 2.0f * static_cast<float>(std::sqrt(m.l1)) * fs, a2 = 2.0f * static_cast<float>(std::sqrt(m.l2)) * fs;
                        const float ct = static_cast<float>(std::cos(m.theta)), sn = static_cast<float>(std::sin(m.theta));
                        for (int k = -1; k <= 1; ++k) {  // 3 px thick axes
                            h.line(cx - a1 * ct + k, cy - a1 * sn, cx + a1 * ct + k, cy + a1 * sn, {1, 0.95f, 0.1f, 1});
                            h.line(cx + a2 * sn + k, cy - a2 * ct, cx - a2 * sn + k, cy + a2 * ct, {1, 0.2f, 0.9f, 1});
                        }
                        crossMark(h, cx, cy, 4 * fs / 2 + 2, {1, 1, 1, 1});
                    }
                } else if (comp.area >= 2) {
                    thickFrame(h, x - 1, y - 1, w + 2, hh + 2, {1, 0.2f, 0.2f, 1}, 1);
                }
            }
        });
        const float ts = 8.0f + 4.0f * fs;
        for (const cv::CvComponent& comp : v.dbg.components) {
            if (comp.accepted) {
                for (const Measurement2D& m : v.dbg.targets)
                    if (m.minX == comp.minX && m.minY == comp.minY) {
                        char b[80];
                        std::snprintf(b, sizeof(b), "area %d  elong %.2f  ang %.0f", m.area, m.elongation, m.orientationDeg);
                        texts.push_back({comp.minX * fs, std::max(2.0f, comp.minY * fs - ts - 8), b, ts * 0.8f, {0.4f, 1, 1, 1}});
                    }
            } else if (comp.area >= 2) {
                texts.push_back({comp.maxX * fs + 6, comp.minY * fs, "rejected", ts * 0.7f, {1, 0.5f, 0.5f, 1}});
            }
        }
    }
    {
        const float ts = 8.0f + 4.0f * fs;
        char b[160];
        if (p == Panel::Mask) {
            std::snprintf(b, sizeof(b), "threshold t = max(%d, Otsu %d + 1) = %d   hot pixels %d", CvConfig{}.floor, st.otsu, st.threshold, st.hotPixels);
            texts.push_back({8, H - ts - 8.0f, b, ts * 0.85f, {1, 0.9f, 0.4f, 1}});
        } else if (p == Panel::Despeckle) {
            std::snprintf(b, sizeof(b), "removed %d isolated pixels (< 2 hot 8-neighbours)", st.removedSpeckles);
            texts.push_back({8, H - ts - 8.0f, b, ts * 0.85f, {1, 0.5f, 0.5f, 1}});
        } else if (p == Panel::Labels) {
            std::snprintf(b, sizeof(b), "%d components, %d pass the target filter", st.components, st.targets);
            texts.push_back({8, H - ts - 8.0f, b, ts * 0.85f, grey(1)});
            for (const cv::CvComponent& comp : v.dbg.components)
                if (comp.area >= 3) {
                    char l[16];
                    std::snprintf(l, sizeof(l), "%d", comp.label);
                    texts.push_back({comp.maxX * fs + 4, comp.minY * fs - 4, l, ts * 0.8f, grey(1)});
                }
        }
        std::snprintf(b, sizeof(b), "UAV %d  t = %.2f s  %s", v.uav + 1, v.time, panelTitle(p));
        texts.insert(texts.begin(), Label{8, 6, b, ts, grey(1)});
    }
    Image img = cv.read();
    overlayText(c, cv, img, texts);
    if (tex) glDeleteTextures(1, &tex);
    return img;
}

void writeJsonNum(std::ostream& o, const char* k, double v, bool last = false) {
    char b[80];
    std::snprintf(b, sizeof(b), "\"%s\": %.4f%s", k, v, last ? "" : ", ");
    o << b;
}

}  // namespace

int CvView::bestTrack(const Measurement2D& m, Vec3* ground, float* range) const {
    Vec3 hit;
    if (!DroneController::imagePointToGround(m.u, m.v, camera, hit)) return -1;
    if (ground) *ground = hit;
    if (range) *range = length(hit - camPos);
    int best = -1;
    float bd = 3.0f;
    for (const TrackInfo& t : tracks) {
        const float d = std::hypot(t.pos.x - hit.x, t.pos.z - hit.z);
        if (d < bd) { bd = d; best = t.id; }
    }
    return best;
}

CvView makeCvView(const Scene& scene, int uav, const std::vector<uint8_t>& img, float time) {
    CvView v;
    v.uav = uav;
    v.time = time;
    v.img = img;
    v.camera = scene.cameraWorld(uav);
    v.camPos = transformPoint(v.camera, {0, 0, 0});
    v.dbg = cv::detectTargetsDebug(img, kW, kH);
    for (const Track& t : scene.squad().tracker().tracks()) {
        TrackInfo ti;
        ti.id = t.id;
        ti.state = static_cast<int>(t.state);
        ti.pos = t.position();
        ti.vel = t.velocity();
        ti.sigma = t.positionSigma();
        ti.residual = t.triResidual;
        ti.triangulated = t.triangulated;
        ti.seenNow = t.seenNow;
        float best = 1e9f;
        for (const auto& in : scene.intruders())
            if (in->life == Intruder::Life::Alive) best = std::min(best, std::hypot(in->state.position.x - ti.pos.x, in->state.position.z - ti.pos.z));
        ti.nearestTruth = best < 1e8f ? best : -1.0f;
        v.tracks.push_back(ti);
    }
    return v;
}

bool cvFrame(Ctx& c, const CvView views[2]) {
    Canvas cv(*c.renderer);
    bool ok = true;
    Scene& sc = *c.scene;
    // pick the UAV with the biggest accepted blob as the "main" view (for the external camera)
    int mainUav = 0, mainArea = -1;
    for (int u = 0; u < 2; ++u)
        for (const Measurement2D& m : views[u].dbg.targets)
            if (m.area > mainArea) { mainArea = m.area; mainUav = u; }
    // ---- panels per UAV
    const Panel order[7] = {Panel::RawIron, Panel::RawGrey, Panel::Hist, Panel::Mask, Panel::Despeckle, Panel::Labels, Panel::Overlay};
    const char* tags[7] = {"a_raw_ironbow", "a_raw_grey", "b0_histogram", "b_mask", "c_despeckle", "d_components", "e_overlay"};
    Image extern_;
    // external view first (it needs the scene state of this very frame)
    {
        const CvView& mv = views[mainUav];
        Vec3 target = {0, 0, 0};
        int trackId = -1;
        if (!mv.dbg.targets.empty()) {
            Vec3 g;
            float range = 0;
            trackId = mv.bestTrack(mv.dbg.targets[0], &g, &range);
            target = g;
        }
        for (const TrackInfo& t : mv.tracks)
            if (t.id == trackId) target = t.pos;
        const Vec3 mid = (views[0].camPos + views[1].camPos) * 0.5f;
        Vec3 away = target - mid;
        away.y = 0;
        away = length(away) > 0.1f ? normalize(away) : Vec3{0, 0, 1};
        const Vec3 side{-away.z, 0, away.x};
        const Vec3 eye = target + side * 5.0f + away * 2.0f + Vec3{0, 2.0f, 0};
        const Vec3 aim = lerp(target, mid, 0.12f) + Vec3{0, 0.2f, 0};
        const int W = 1280, H = 720;
        const CameraView cam = makeView(eye, aim, 52.0f, static_cast<float>(W) / H);
        RenderSettings s;
        s.view = ViewMode::Visible;
        s.quality = c.qualityForced ? c.quality : std::min(c.quality, Quality::Ultra);
        c.renderer->labInvalidateCaches();
        const Mat4 vp = cam.proj * cam.view;
        extern_ = renderView(c, cv, cam, s, W, H, 4, [&](Hud& h) {
            for (int u = 0; u < 2; ++u) {
                const MarkerProjection p = projectMarker(vp, cam.position, views[u].camPos, W, H);
                if (p.onScreen) {
                    h.brackets(p.x - 20, p.y - 20, 40, 40, 9, u == 0 ? Rgba{0.3f, 1, 0.4f, 1} : Rgba{1, 0.6f, 0.2f, 1});
                    char b[24];
                    std::snprintf(b, sizeof(b), "UAV %d", u + 1);
                    h.text(p.x + 26, p.y - 12, b, 22, u == 0 ? Rgba{0.3f, 1, 0.4f, 1} : Rgba{1, 0.6f, 0.2f, 1});
                }
            }
            for (const TrackInfo& t : mv.tracks) {
                const MarkerProjection p = projectMarker(vp, cam.position, t.pos, W, H);
                if (!p.onScreen) continue;
                const float half = markerHalfSize(p.dist, H, 1.0f, 52.0f);
                const Rgba col = t.state == 0 ? Rgba{1, 0.9f, 0.3f, 1} : Rgba{1, 0.25f, 0.2f, 1};
                h.brackets(p.x - half, p.y - half, 2 * half, 2 * half, half * 0.35f, col);
                char b[96];
                std::snprintf(b, sizeof(b), "TRACK %d  %s  +/-%.2f m%s", t.id, t.state == 0 ? "tentative" : "confirmed", t.sigma, t.triangulated ? "  2-view" : "");
                h.text(p.x - half, p.y + half + 6, b, 22, col);
                for (int u = 0; u < 2; ++u) {  // pixel ray of each UAV that measured it
                    if (!((t.seenNow >> u) & 1u)) continue;
                    const MarkerProjection q = projectMarker(vp, cam.position, views[u].camPos, W, H);
                    if (q.front && p.front) h.line(q.x, q.y, p.x, p.y, u == 0 ? Rgba{0.3f, 1, 0.4f, 0.8f} : Rgba{1, 0.6f, 0.2f, 0.8f});
                }
            }
        });
        overlayText(c, cv, extern_, {{14, 10, "f  the same frame, external view: tracks and the pixel rays of both UAVs", 26, grey(1)}});
        ok = save(c, "cv_f_external.png", extern_) && ok;
        ok = save(c, "cv_tracks.png", extern_) && ok;
    }
    // ---- per-UAV panels
    for (int u = 0; u < 2; ++u) {
        const CvView& v = views[u];
        const std::string pre = "cv_u" + std::to_string(u + 1) + "_";
        const bool has = !v.dbg.targets.empty();
        const int nPanels = has ? 7 : 2;
        Image tiles[7];
        for (int i = 0; i < nPanels; ++i) {
            const Image big = cvPanel(c, cv, v, order[i], 3);
            ok = save(c, pre + tags[i] + ".png", big) && ok;
            if (u == mainUav) {  // fixed names the report loads
                if (i == 0) ok = save(c, "cv_thermal.png", big) && ok;
                if (i == 3) ok = save(c, "cv_threshold.png", big) && ok;
                if (i == 4) ok = save(c, "cv_despeckle.png", big) && ok;
                if (i == 5) ok = save(c, "cv_labels.png", big) && ok;
            }
            tiles[i] = cvPanel(c, cv, v, order[i], 2);
        }
        if (!has) continue;
        // montage: 4 x 2 tiles of 640 x 480: a ironbow, a grey, histogram, mask / despeckle, components, overlay, external
        Image m(3 * 533, 3 * 400);
        for (int i = 0; i < 7; ++i) blit(m, resized(tiles[i], 533, 400), (i % 3) * 533, (i / 3) * 400);
        blit(m, fitted(extern_, 533, 400), 1 * 533, 2 * 400);
        ok = save(c, "cv_pipeline_uav" + std::to_string(u + 1) + ".png", m) && ok;
    }
    // ---- numbers
    std::ofstream js(c.dir + "/cv_frame.json");
    js << "{\n  \"simulation_time_s\": " << views[0].time << ",\n  \"image\": {\"width\": " << kW << ", \"height\": " << kH << ", \"fov_y_deg\": " << kCvFovYDeg << "},\n";
    js << "  \"config\": {\"floor\": " << int(CvConfig{}.floor) << ", \"core\": " << int(CvConfig{}.core) << ", \"min_area\": " << CvConfig{}.minArea
       << ", \"max_area\": " << CvConfig{}.maxArea << ", \"max_elongation\": " << CvConfig{}.maxElongation << ", \"despeckle_min_neighbours\": 2},\n";
    js << "  \"uavs\": [\n";
    for (int u = 0; u < 2; ++u) {
        const CvView& v = views[u];
        const CvFrameStats& st = v.dbg.stats;
        js << "    {\"uav\": " << u + 1 << ", \"camera_position\": [" << v.camPos.x << ", " << v.camPos.y << ", " << v.camPos.z << "],\n";
        js << "     \"otsu\": " << int(st.otsu) << ", \"threshold_used\": " << int(st.threshold) << ", \"hot_pixels\": " << st.hotPixels
           << ", \"removed_speckles\": " << st.removedSpeckles << ", \"components\": " << st.components << ", \"targets\": " << st.targets << ",\n";
        js << "     \"blobs\": [\n";
        for (size_t i = 0; i < v.dbg.targets.size(); ++i) {
            const Measurement2D& m = v.dbg.targets[i];
            Vec3 g{0, 0, 0};
            float range = 0;
            const int tid = v.bestTrack(m, &g, &range);
            js << "       {";
            writeJsonNum(js, "area_px", m.area);
            writeJsonNum(js, "centroid_x_px", m.cx);
            writeJsonNum(js, "centroid_y_px", m.cy);
            writeJsonNum(js, "u", m.u);
            writeJsonNum(js, "v", m.v);
            writeJsonNum(js, "orientation_deg", m.orientationDeg);
            writeJsonNum(js, "elongation", m.elongation);
            writeJsonNum(js, "mean_heat", m.meanHeat);
            writeJsonNum(js, "peak", m.peak);
            js << "\"bbox\": [" << m.minX << ", " << m.minY << ", " << m.maxX << ", " << m.maxY << "], ";
            js << "\"ground_point\": [" << g.x << ", " << g.y << ", " << g.z << "], ";
            writeJsonNum(js, "range_m", range);
            js << "\"track_id\": " << tid << "}" << (i + 1 < v.dbg.targets.size() ? "," : "") << "\n";
        }
        js << "     ],\n     \"rejected_components\": [";
        bool first = true;
        for (const cv::CvComponent& comp : v.dbg.components)
            if (!comp.accepted && comp.area >= 2) {
                js << (first ? "" : ", ") << "{\"label\": " << comp.label << ", \"area_px\": " << comp.area << ", \"bbox\": [" << comp.minX << ", " << comp.minY << ", " << comp.maxX << ", " << comp.maxY << "]}";
                first = false;
            }
        js << "]}" << (u == 0 ? "," : "") << "\n";
    }
    js << "  ],\n  \"tracks\": [\n";
    const std::vector<TrackInfo>& tr = views[0].tracks;
    for (size_t i = 0; i < tr.size(); ++i) {
        const TrackInfo& t = tr[i];
        js << "    {\"id\": " << t.id << ", \"state\": \"" << (t.state == 0 ? "tentative" : t.state == 1 ? "confirmed" : "coasting") << "\", ";
        js << "\"position\": [" << t.pos.x << ", " << t.pos.y << ", " << t.pos.z << "], \"velocity\": [" << t.vel.x << ", " << t.vel.z << "], ";
        writeJsonNum(js, "position_sigma_m", t.sigma);
        js << "\"seen_by_uav1\": " << ((t.seenNow & 1u) ? "true" : "false") << ", \"seen_by_uav2\": " << ((t.seenNow & 2u) ? "true" : "false") << ", ";
        js << "\"triangulated\": " << (t.triangulated ? "true" : "false") << ", ";
        writeJsonNum(js, "triangulation_residual_m", t.residual);
        writeJsonNum(js, "error_vs_nearest_rover_m", t.nearestTruth, true);
        js << "}" << (i + 1 < tr.size() ? "," : "") << "\n";
    }
    js << "  ]\n}\n";
    (void)sc;
    return ok && js.good();
}

bool cvStrip(Ctx& c, const std::vector<CvView>& frames, int uav) {
    Canvas cv(*c.renderer);
    const int S = 2, TW = kW * S, TH = kH * S;
    Image m(3 * TW, 2 * TH);
    std::vector<std::pair<float, float>> trail;  // centroids so far (pixels)
    bool ok = true;
    for (size_t i = 0; i < frames.size() && i < 6; ++i) {
        const CvView& v = frames[i];
        const GLuint tex = heatTexture(v.img);
        cv.open(TW, TH);
        cv.layer([&](Hud& h) {
            h.setPalette(0);
            h.heatImage(0, 0, static_cast<float>(TW), static_cast<float>(TH), tex);
        });
        std::vector<Label> texts;
        char b[120];
        std::snprintf(b, sizeof(b), "UAV %d  t = %.2f s  frame %zu/6", uav + 1, v.time, i + 1);
        texts.push_back({8, 6, b, 18, grey(1)});
        for (const Measurement2D& mm : v.dbg.targets) {
            trail.push_back({(mm.cx + 0.5f) * S, (mm.cy + 0.5f) * S});
            Vec3 g;
            float range = 0;
            const int tid = v.bestTrack(mm, &g, &range);
            cv.layer([&](Hud& h) {
                thickFrame(h, (mm.minX - 2) * S, (mm.minY - 2) * S, (mm.maxX - mm.minX + 5) * S, (mm.maxY - mm.minY + 5) * S, {0.1f, 0.9f, 1, 1}, 2);
            });
            std::snprintf(b, sizeof(b), "track %d  area %d px  range %.1f m", tid, mm.area, range);
            texts.push_back({std::min((mm.minX - 2) * static_cast<float>(S), static_cast<float>(TW) - 330), std::max(26.0f, (mm.minY - 2) * static_cast<float>(S) - 24), b, 16, {0.4f, 1, 1, 1}});
        }
        if (v.dbg.targets.empty()) texts.push_back({8, 30, "no detection", 18, {1, 0.5f, 0.5f, 1}});
        cv.layer([&](Hud& h) {  // centroid trail of all frames so far
            for (size_t k = 0; k < trail.size(); ++k) {
                const float a = 0.35f + 0.65f * static_cast<float>(k + 1) / static_cast<float>(trail.size());
                h.rect(trail[k].first - 3, trail[k].second - 3, 6, 6, {1, 1, 1, a});
                if (k > 0) h.line(trail[k - 1].first, trail[k - 1].second, trail[k].first, trail[k].second, {1, 1, 1, 0.8f});
            }
        });
        Image img = cv.read();
        overlayText(c, cv, img, texts);
        blit(m, img, static_cast<int>(i % 3) * TW, static_cast<int>(i / 3) * TH);
        glDeleteTextures(1, &tex);
    }
    ok = save(c, "cv_strip.png", m) && ok;
    return ok;
}

// ================================================================================================ v6 CV under sensor noise
namespace {

Rgba classColour(BlobClass c) {
    switch (c) {
        case BlobClass::Target: return {0.1f, 0.9f, 1.0f, 1.0f};
        case BlobClass::Person: return {0.35f, 1.0f, 0.45f, 1.0f};
        case BlobClass::HotClutter: return {1.0f, 0.55f, 0.15f, 1.0f};
        case BlobClass::Clutter: return {1.0f, 0.25f, 0.25f, 1.0f};
    }
    return {1, 1, 1, 1};
}

}  // namespace

bool cvNoise(Ctx& c, const NoiseFrame& f) {
    Canvas cv(*c.renderer);
    const int S = 2, TW = kW * S, TH = kH * S;
    Image m(2 * TW, 2 * TH);
    std::vector<uint8_t> diff(f.clean.size());
    double sum = 0.0, sum2 = 0.0;
    for (size_t i = 0; i < diff.size() && i < f.noisy.size(); ++i) {
        const int d = static_cast<int>(f.noisy[i]) - static_cast<int>(f.clean[i]);
        sum += d;
        sum2 += static_cast<double>(d) * d;
        diff[i] = static_cast<uint8_t>(std::clamp(128 + 8 * d, 0, 255));
    }
    const double n = static_cast<double>(std::max<size_t>(diff.size(), 1));
    const double mean = sum / n, sigma = std::sqrt(std::max(0.0, sum2 / n - mean * mean));
    struct Tile { const std::vector<uint8_t>* img; int palette; const std::vector<Measurement2D>* blobs; const char* title; };
    const Tile tiles[4] = {{&f.clean, 0, &f.cleanBlobs, "a  ideal sensor (rendered heat)"},
                           {&f.noisy, 0, nullptr, "b  sensor model: PSF, FPN + NUC residual, NETD, stuck px"},
                           {&diff, 1, nullptr, "c  difference b - a, x8 around mid-grey"},
                           {&f.noisy, 0, &f.noisyBlobs, "d  detectAll on b: target / person / hot clutter / clutter"}};
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        const Tile& t = tiles[i];
        const GLuint tex = heatTexture(*t.img);
        cv.open(TW, TH);
        cv.layer([&](Hud& h) {
            h.setPalette(t.palette);
            h.heatImage(0, 0, static_cast<float>(TW), static_cast<float>(TH), tex);
        });
        std::vector<Label> texts;
        texts.push_back({8, 6, t.title, 18, grey(1)});
        if (t.blobs) {
            for (const Measurement2D& b : *t.blobs) {
                const Rgba col = classColour(b.cls);
                cv.layer([&](Hud& h) {
                    thickFrame(h, (b.minX - 2) * S, (b.minY - 2) * S, (b.maxX - b.minX + 5) * S, (b.maxY - b.minY + 5) * S, col, 2);
                });
                char lb[64];
                std::snprintf(lb, sizeof(lb), "%s %d px", blobClassName(b.cls), b.area);
                texts.push_back({std::min((b.minX - 2) * static_cast<float>(S), static_cast<float>(TW) - 160),
                                 std::max(26.0f, (b.minY - 2) * static_cast<float>(S) - 22), lb, 15, col});
            }
        }
        if (i == 2) {
            char lb[96];
            std::snprintf(lb, sizeof(lb), "mean %+.2f DN  sigma %.2f DN  (NETD %.1f DN)", mean, sigma, f.cfg.netdDn);
            texts.push_back({8, static_cast<float>(TH) - 28, lb, 17, grey(1)});
        }
        Image img = cv.read();
        overlayText(c, cv, img, texts);
        blit(m, img, (i % 2) * TW, (i / 2) * TH);
        glDeleteTextures(1, &tex);
    }
    ok = save(c, "lab_cv_noise.png", m) && ok;
    std::ofstream js(c.dir + "/cv_noise.json");
    const auto count = [](const std::vector<Measurement2D>& v, BlobClass k) {
        int n = 0;
        for (const Measurement2D& b : v) n += b.cls == k;
        return n;
    };
    js << "{\n  \"uav\": " << f.uav << ", \"time\": " << f.time << ", \"seed\": " << f.seed << ",\n  \"config\": {\"netd_dn\": "
       << f.cfg.netdDn << ", \"fpn_gain_sigma\": " << f.cfg.fpnGainSigma << ", \"fpn_offset_sigma\": " << f.cfg.fpnOffsetSigma
       << ", \"column_sigma\": " << f.cfg.columnSigma << ", \"psf\": " << (f.cfg.psf ? "true" : "false") << ", \"jitter_deg\": "
       << f.cfg.jitterDeg << ", \"drop_prob\": " << f.cfg.dropProb << ", \"stuck_frac\": " << f.cfg.stuckFrac
       << ", \"nuc_residual\": " << f.cfg.nucResidual << "},\n  \"difference\": {\"mean_dn\": " << mean << ", \"sigma_dn\": " << sigma
       << "},\n";
    for (int k = 0; k < 2; ++k) {
        const std::vector<Measurement2D>& v = k == 0 ? f.cleanBlobs : f.noisyBlobs;
        js << "  \"" << (k == 0 ? "clean" : "noisy") << "\": {\"blobs\": " << v.size() << ", \"targets\": " << count(v, BlobClass::Target)
           << ", \"persons\": " << count(v, BlobClass::Person) << ", \"hot_clutter\": " << count(v, BlobClass::HotClutter)
           << ", \"clutter\": " << count(v, BlobClass::Clutter) << "}" << (k == 0 ? ",\n" : "\n");
    }
    js << "}\n";
    return ok && js.good();
}

// ================================================================================================ captions
bool writeCaptions(Ctx& c) {
    struct Cap { const char* file; const char* caption; const char* how; };
    const Cap caps[] = {
        {"bump_on_off.png", "Surface relief OFF (top) / ON (bottom) on six materials under raking light.", "Copy of relief_compare.png (bumpOn flag -> uBumpOff)."},
        {"cv_thermal.png", "Real raw 320x240 heat image of the UAV that sees the rover best, x3, ironbow.", "Copy of cv_uN_a_raw_ironbow.png."},
        {"cv_threshold.png", "Threshold mask of that frame with the threshold value printed.", "Copy of cv_uN_b_mask.png."},
        {"cv_despeckle.png", "Despeckle result, removed pixels in red.", "Copy of cv_uN_c_despeckle.png."},
        {"cv_labels.png", "Connected components, a random colour per label.", "Copy of cv_uN_d_components.png."},
        {"cv_tracks.png", "Tracker output overlaid on the same frame seen from outside: tracks, uncertainty, UAV pixel rays.", "Copy of cv_f_external.png."},
        {"material_grid.png", "PBR roughness x metalness grid plus Blinn-Phong row.", "Copy of materials_grid.png."},
        {"pipeline_buffers.png", "Internal render buffers of one frame.", "Copy of buffers_overview.png (scaled to 1600 px)."},
        {"shading_models.png", "UAV with flat / Gouraud / Phong override and wireframe.", "Copy of shading_uav_compare.png (scaled to 1600 px)."},
        {"shadow_cascades.png", "Moon shadow map near cascade and world cascade (depth).", "glGetTexImage of the 2-layer shadow array."},
        {"ssr_on_off.png", "Screen-space reflections off / on, reflectivity buffer, difference.", "Copy of reflections_compare.png (scaled to 1600 px)."},
        {"surface_patterns.png", "Procedural albedo patterns of the surface materials (relief off).", "Copy of relief_off.png."},
        {"cv_pipeline_uav1.png", "Computer-vision stage on UAV 1's real 320x240 thermal frame: raw heat (ironbow and grey), histogram with the Otsu threshold, threshold mask, despeckle, connected components, centroid / box / principal axes, and the same frame seen from outside.",
         "Rendered by the program (--capture --only lab_cv): raw heat from Renderer::captureUavCamera (view HeatRaw), stages from cv::detectTargetsDebug (the same functions as detectTargets), overlays with the Hud API. Moment chosen automatically: first frame with both UAVs holding a confirmed track."},
        {"cv_pipeline_uav2.png", "The same pipeline on UAV 2's frame at the same instant.", "As cv_pipeline_uav1.png."},
        {"cv_u1_a_raw_ironbow.png", "(a) Raw heat image of UAV 1, 320x240 scaled x3 (nearest), ironbow palette.", "Hud::heatImage of the R8 heat texture, nearest filter."},
        {"cv_u1_a_raw_grey.png", "(a) Raw heat image of UAV 1 in grey (white-hot palette).", "As above, palette 1."},
        {"cv_u1_b0_histogram.png", "Histogram (log counts) of the heat image with the fixed floor 204, the Otsu level and the threshold actually used.", "cv::otsuThreshold + Hud rectangles."},
        {"cv_u1_b_mask.png", "(b) Threshold mask; the printed value is t = max(floor 204, Otsu + 1).", "Pixels >= CvFrameStats::threshold."},
        {"cv_u1_c_despeckle.png", "(c) After despeckle: pixels with fewer than 2 hot 8-neighbours are removed (red).", "cv::despeckle on the mask; removed = mask and not cleaned."},
        {"cv_u1_d_components.png", "(d) Connected components (two-pass labelling, union-find, 8-connectivity), one random colour per label.", "cv::labelComponents; colour = hash of the label."},
        {"cv_u1_e_overlay.png", "(e) Accepted blobs: bounding box (cyan), centroid (white cross), principal axes from the second central moments (yellow = major, magenta = minor, length 2 sigma); rejected components in red.", "Moments recomputed from the labelled pixels; the target filter is CvConfig (area, hot core, elongation)."},
        {"cv_u2_a_raw_ironbow.png", "(a) UAV 2 raw heat image (x3, ironbow).", "As cv_u1_a_raw_ironbow.png."},
        {"cv_u2_a_raw_grey.png", "(a) UAV 2 raw heat image (grey).", "As cv_u1_a_raw_grey.png."},
        {"cv_f_external.png", "(f) The same simulation frame from an external camera: both UAVs, the confirmed track with its uncertainty, and each UAV's pixel ray to the target.", "Renderer::beginFrame/endFrame on the live scene, Hud brackets from projectMarker, tracks from the squad's MultiTracker."},
        {"cv_strip.png", "Six frames, 0.15 s apart: the detection box follows the rover; white trail = centroid history.", "Frames captured while stepping the simulation; cv::detectTargets on each; track id = nearest track to the blob's ground point."},
        {"cv_frame.json", "Measurement numbers of the frame: per UAV Otsu / threshold / counts, per blob area, centroid, orientation, elongation, ground point, track id; per track state, sigma, triangulation residual and error against the rover's true position.", "Written by lab::cvFrame."},
        {"lab_cv_noise.png", "The same UAV frame through an ideal sensor and through the v6 sensor model, and what the CV stage makes of each: target, person, hot clutter and clutter blobs.",
         "Renderer::captureUavCamera (clean), SensorModel::apply with a fresh per-run seed (noisy), cv::detectAll on both; numbers in cv_noise.json."},
        {"materials_grid.png", "Material grid: 5 x 5 spheres, roughness 0.1-0.9 (columns) x metalness 0 / 0.25 / 0.5 / 0.75 / 1 (rows), copper albedo, lit by a moon-like directional light and one point lamp; below, the Blinn-Phong row (shininess 4 / 16 / 64 / 256).",
         "Real PBR shader (GGX / Cook-Torrance, uPbr = 1) on a LabStage of unit spheres; the Blinn-Phong row uses RenderSettings::pbr = false. HIGH quality, no fog."},
        {"materials_pbr_grid.png", "The 5 x 5 PBR grid alone.", "As materials_grid.png."},
        {"materials_blinnphong_row.png", "Blinn-Phong row: shininess 4, 16, 64, 256.", "uPbr = 0."},
        {"shading_uav_compare.png", "The UAV (hierarchical composite of unit primitives) with the shading override flat / Gouraud / Phong and a hidden-line wireframe of its triangles.", "RenderSettings::shading = Flat / Gouraud / Phong, isolate = Autonomous UAV; wire: view Diagnostic + EdgeMode::HiddenLine."},
        {"shading_sphere_compare.png", "A coarse 10x6 sphere (top row) and the stock 36x18 sphere (bottom row): flat facets, Gouraud Mach bands and a lost / smeared highlight, Phong, and the triangles.", "Renderer::labSphereResolution swaps the sphere mesh; Blinn-Phong terms (pbr = false) so the highlight shows the interpolation difference."},
        {"relief_compare.png", "Procedural surface relief OFF (top) and ON (bottom) on corrugated metal, steel plate, HESCO, concrete, rock and asphalt under raking light.", "RenderSettings::bumpOn -> uniform uBumpOff in surfaceBump(); cubes on a LabStage, HIGH quality."},
        {"reflections_compare.png", "Wet ground in rain: SSR off, SSR on, the reflectivity buffer (surface target alpha) and the amplified difference.", "RenderSettings::ssrOn on the live scene (wetness 1, rain 1, ULTRA); buffer read with Renderer::labReadBuffer."},
        {"buffers_overview.png", "The render pipeline buffers of one frame: moon shadow maps, depth, normals, SSAO, fog in-scatter and transmittance, HDR colour and its false-colour log luminance, bloom, reflectivity, final.", "glGetTexImage of the renderer's internal textures after endFrame (RendererLab.cpp), mapped to 8-bit for display."},
    };
    std::ofstream js(c.dir + "/captions.json");
    js << "{\n";
    const size_t n = sizeof(caps) / sizeof(caps[0]);
    for (size_t i = 0; i < n; ++i)
        js << "  \"" << caps[i].file << "\": {\"caption\": \"" << caps[i].caption << "\", \"how\": \"" << caps[i].how << "\"}" << (i + 1 < n ? "," : "") << "\n";
    js << "}\n";
    return js.good();
}

// ================================================================================================ v7 frame explorer
namespace {

// The frozen frame at a given hour: the day cycle is set by hand (manual clock) and the light rig re-applied; restored
// by the destructor.
struct ClockOverride {
    Scene& sc;
    DayCycle* clock = nullptr;
    float hour = 0.0f;
    bool autoRun = false;
    LightRig lights;
    ClockOverride(Scene& s, float h) : sc(s), clock(sa::dayCycle(s)), lights(s.lights) {
        if (!clock || h < 0.0f) {
            clock = nullptr;
            return;
        }
        hour = clock->hour();
        autoRun = clock->autoRun();
        clock->setAuto(false);
        clock->setHour(h);
        clock->applyTo(sc.lights);
    }
    ~ClockOverride() {
        if (!clock) return;
        clock->setHour(hour);
        clock->setAuto(autoRun);
        sc.lights = lights;
    }
};

// 4 x 4 montage of the clean stage images, each tile with a number + title band (readable at report column width).
Image explorerMontage(Ctx& c, const std::vector<Image>& clean, const std::string& subtitle) {
    const int W = clean.front().w, H = clean.front().h;
    const int cw = 396, ch = cw * H / W, gap = 4, head = 44;
    Image mont(4 * cw + 3 * gap, head + 4 * ch + 3 * gap, 6);
    std::vector<Label> labels;
    labels.push_back({10, 8, "Frame Explorer: how one frame is made, stage by stage", 24, {1.0f, 0.70f, 0.24f, 1}});
    labels.push_back({10 + textW(*c.renderer, "Frame Explorer: how one frame is made, stage by stage", 24) + 16, 13, subtitle, 17, grey(0.75f)});
    for (int i = 0; i < fx::kStageCount && i < static_cast<int>(clean.size()); ++i) {
        const int x = (i % 4) * (cw + gap), y = head + (i / 4) * (ch + gap);
        Image tile = resized(clean[static_cast<size_t>(i)], cw, ch);
        for (int yy = ch - 36; yy < ch; ++yy)  // title band
            for (int xx = 0; xx < cw; ++xx) {
                uint8_t* p = &tile.px[(static_cast<size_t>(yy) * cw + xx) * 3];
                for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(p[k] * 0.22f + 4);
            }
        blit(mont, tile, x, y);
        static const char* kTileTitle[fx::kStageCount] = {
            "Scene graph", "Vertex transform", "Assembly + culling", "Rasterisation", "Colour interpolation", "Depth test (Z-buffer)",
            "G-buffer", "Shadow map", "Lighting terms", "Reflection (SSR)", "Ambient occlusion", "Fog + volumetrics", "Bloom",
            "Tone mapping", "Temporal AA", "Final + thermal / CV"};
        const std::string title = kTileTitle[i];
        char num[8];
        std::snprintf(num, sizeof(num), "%02d", i + 1);
        labels.push_back({static_cast<float>(x + 8), static_cast<float>(y + ch - 33), num, 26, {1.0f, 0.70f, 0.24f, 1}});
        labels.push_back({static_cast<float>(x + 48), static_cast<float>(y + ch - 29), title, 19, grey(0.96f)});
    }
    Canvas cv(*c.renderer);
    overlayText(c, cv, mont, labels, 1.0f);
    return mont;
}

}  // namespace

bool explorer(Ctx& c, const ExplorerShot& shot) {
    Scene& sc = *c.scene;
    const int W = shot.width, H = shot.height;
    const float savedWet = sc.wetness;
    if (shot.wetness >= 0.0f) sc.wetness = std::max(sc.wetness, shot.wetness);
    const CameraView cam = shot.cam ? *shot.cam : makeView({-15.0f, 5.2f, 21.0f}, {1.0f, 1.2f, 2.0f}, 52.0f, static_cast<float>(W) / H);
    RenderSettings s;
    s.view = ViewMode::Visible;
    s.quality = c.quality;
    RenderTarget rt;
    rt.create(W, H, 0, false);
    const auto readRt = [&]() {
        Image i;
        i.w = W;
        i.h = H;
        rt.read(i.px);
        if (i.px.size() == static_cast<size_t>(W) * H * 4) {  // RGBA read-back: drop alpha
            std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
            for (size_t k = 0; k < static_cast<size_t>(W) * H; ++k) std::copy_n(&i.px[k * 4], 3, &rgb[k * 3]);
            i.px.swap(rgb);
        }
        return i;
    };
    FrameExplorer fe;
    // The progress overlay (shown in the window while open() works): one capture of it for the report.
    bool ok = true, progressSaved = false;
    fe.setProgress([&](float f, const std::string& label) {
        if (progressSaved || label.find("variants 3/") == std::string::npos) return;
        progressSaved = true;
        rt.bind();
        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        fe.drawProgress(*c.renderer, static_cast<GLuint>(fbo), W, H, f, label);
        ok = save(c, shot.prefix + "_progress.png", readRt()) && ok;
    });
    bool opened = false;
    {
        ClockOverride day(sc, shot.hour);
        opened = fe.open(*c.renderer, sc, cam, s, W, H);
    }
    fe.setProgress(nullptr);
    if (!opened) {
        sc.wetness = savedWet;
        std::printf("FAILED explorer: could not open the frozen frame\n");
        return false;
    }
    if (c.log) *c.log << "explorer: open took " << fe.openSeconds() << " s\n";
    std::printf("explorer: frozen frame built in %.2f s\n", fe.openSeconds());
    const auto shotImg = [&](FrameExplorer& e) {
        rt.bind();
        GLint fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        e.draw(*c.renderer, static_cast<GLuint>(fbo), W, H);
        return readRt();
    };
    FrameExplorer::ViewState& v = fe.view();
    const auto stage = [&](FrameExplorer& e, int i, int layer, float anim) {
        e.setStage(i);
        e.setTransition(1.0f);
        e.view().layer = layer;
        e.setAnimation(anim);
    };
    const auto cleanSet = [&](FrameExplorer& e) {
        std::vector<Image> clean;
        e.view().captions = false;
        e.view().tag = false;
        for (int i = 0; i < fx::kStageCount; ++i) {
            stage(e, i, 0, i == 3 ? e.rasterProbeTime() : 9.95f);
            clean.push_back(shotImg(e));
        }
        e.view().captions = true;
        e.view().tag = true;
        return clean;
    };
    for (int i = 0; i < fx::kStageCount; ++i) {
        stage(fe, i, 0, i == 3 ? fe.rasterProbeTime() : 9.95f);   // raster: the scan at the probe; others: animations done
        char name[96];
        std::string shortName = fx::stageInfo(i).shortName;
        for (char& ch : shortName) ch = static_cast<char>(ch == '-' ? '_' : std::tolower(static_cast<unsigned char>(ch)));
        std::snprintf(name, sizeof(name), "%s_stage_%02d_%s.png", shot.prefix.c_str(), i + 1, shortName.c_str());
        ok = save(c, name, shotImg(fe)) && ok;
    }
    char sub[64];
    std::snprintf(sub, sizeof(sub), "(%02d:%02d, %s)", static_cast<int>(shot.hour >= 0 ? shot.hour : 23.0f),
                  static_cast<int>(std::fmod(shot.hour >= 0 ? shot.hour : 23.0f, 1.0f) * 60.0f), qualityName(c.quality));
    ok = save(c, shot.prefix + "_montage.png", explorerMontage(c, cleanSet(fe), sub)) && ok;
    // extras
    const auto extra = [&](const std::string& n) { ok = save(c, shot.prefix + "_" + n + ".png", shotImg(fe)) && ok; };
    stage(fe, 3, 0, 3.3f);
    extra("raster_scan");
    stage(fe, 4, 1, 9.95f);
    extra("interp_gouraud_phong");
    stage(fe, 4, 0, 9.95f);
    extra("interp_affine_perspective");
    stage(fe, 6, 1, 9.95f);
    v.inspector = true;
    v.magnifier = true;
    v.zoom = 10.0f;
    v.cursorX = W * 0.36f;
    v.cursorY = H * 0.47f;
    extra("inspector");
    v.inspector = v.magnifier = false;
    v.cursorX = v.cursorY = -1.0f;
    stage(fe, 10, 0, 9.95f);  // SSAO buffer wiped against the reflection stage's colour frame
    v.wipe = true;
    v.wipeX = 0.45f;
    extra("wipe");
    v.wipe = false;
    stage(fe, 9, 1, 9.95f);
    extra("reflection_mask");
    // v7 WP-H: SSR vs the BVH ray tracer (only when the renderer has one)
    const auto layerNamed = [&](int st, const char* key) {
        for (int l = 0; l < fe.layerCount(st); ++l)
            if (std::string(fe.layerName(st, l)).find(key) != std::string::npos) return l;
        return -1;
    };
    if (const int l = layerNamed(9, "SSR | ray traced"); l >= 0) {
        stage(fe, 9, l, 9.95f);
        extra("reflection_ssr_vs_rt");
    }
    if (const int l = layerNamed(9, "RT - SSR"); l >= 0) {
        stage(fe, 9, l, 9.95f);
        extra("reflection_rt_difference");
    }
    stage(fe, 15, std::min(2, fe.layerCount(15) - 1), 9.95f);
    extra("final_cv");
    v.lab = true;
    v.kernel = static_cast<int>(fx::KernelType::Sobel);
    v.gamma = 1.0f;
    v.sampleFactor = 6;
    extra("imagelab_sobel");
    v.kernel = static_cast<int>(fx::KernelType::Gaussian);
    v.gamma = 1.6f;
    v.sampleFactor = 8;
    extra("imagelab_gauss_gamma");
    v.lab = false;
    // stage list + numbers for the report / workbook
    std::ofstream js(c.dir + "/" + shot.prefix + "_stages.json");
    const auto esc = [](std::string t) {
        std::string o;
        for (char ch : t) {
            if (ch == '"' || ch == '\\') o += '\\';
            o += ch;
        }
        return o;
    };
    js << "{\n  \"stages\": [\n";
    for (int i = 0; i < fx::kStageCount; ++i) {
        const fx::StageInfo& in = fx::stageInfo(i);
        js << "    {\"index\": " << i + 1 << ", \"short\": \"" << in.shortName << "\", \"title\": \"" << esc(in.title) << "\", \"caption\": \""
           << esc(in.caption) << "\", \"formula\": \"" << esc(in.formula) << "\", \"concepts\": \"" << esc(in.concepts) << "\", \"layers\": [";
        for (int l = 0; l < fe.layerCount(i); ++l) js << (l ? ", " : "") << "\"" << esc(fe.layerName(i, l)) << "\"";
        js << "]}" << (i + 1 < fx::kStageCount ? "," : "") << "\n";
    }
    js << "  ],\n  \"open_seconds\": " << fe.openSeconds() << ",\n  \"hour\": " << shot.hour << ",\n  \"size\": [" << W << ", " << H << "]\n}\n";
    std::printf("wrote %s/%s_stages.json\n", c.dir.c_str(), shot.prefix.c_str());
    // the same view at night: a second montage
    if (shot.nightMontage) {
        FrameExplorer night;
        bool nightOpen = false;
        {
            ClockOverride n(sc, 23.0f);
            nightOpen = night.open(*c.renderer, sc, cam, s, W, H);
        }
        if (nightOpen) ok = save(c, shot.prefix + "_montage_night.png", explorerMontage(c, cleanSet(night), "(23:00, night)")) && ok;
    }
    sc.wetness = savedWet;
    c.renderer->labInvalidateCaches();
    return ok && js.good();
}

}  // namespace ts::lab
