#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "math/Frustum.h"
#include "math/ShadowMath.h"
#include "rendering/FrameBuffers.h"
#include "rendering/GpuTimer.h"
#include "rendering/Grass.h"
#include "rendering/GroundMap.h"
#include "rendering/Rain.h"
#include "rendering/Hud.h"
#include "rendering/Mesh.h"
#include "rendering/RenderTarget.h"
#include "rendering/Shader.h"
#include "scene/Camera.h"

namespace ts {

enum class ViewMode { Thermal = 0, Visible = 1, Diagnostic = 2, Depth = 3, HeatRaw = 4 };
enum class EdgeMode { Solid, HiddenLine, XRay };
enum class ShadingOverride { PerObject, Flat, Gouraud, Phong };
enum class Quality { Low = 0, High = 1, Ultra = 2 };

// What each quality preset turns on (see docs/PLAN_V2.md, A8).
struct QualityProfile {
    int msaa = 4;
    int moonShadowRes = 2048, spotShadowRes = 1024;  // 0 = off
    bool ssao = true;
    int ssaoSamples = 8;
    int pcfTaps = 8;             // shadow filter taps (Poisson disc)
    bool moonShafts = false;     // moon shadows inside the fog (god rays)
    int grassBlades = 35000;     // instanced unit-cube blades
    bool groundDetail = true;    // realistic terrain surface (materials, relief, wet specular)
    int rainDrops = 14000;       // instanced streaks at full rain
    bool volumetric = true;
    int fogSteps = 24;
    int fogDivisor = 4;          // fog buffer = screen / divisor
    float cascadeSplit = 12.0f;  // metres: near cascade covers the view up to this depth
};
QualityProfile qualityProfile(Quality q);
const char* qualityName(Quality q);

const char* viewModeName(ViewMode m);
const char* edgeModeName(EdgeMode m);
const char* overrideName(ShadingOverride s);
Vec3 primitiveColor(PrimitiveType p);  // diagnostic colour coding

struct RenderSettings {
    ViewMode view = ViewMode::Visible;
    EdgeMode edges = EdgeMode::Solid;
    ShadingOverride shading = ShadingOverride::PerObject;
    bool depthTest = true;
    bool indexed = true;
    bool moonOn = true, beaconOn = true, spotOn = true;
    bool showAxes = false;
    bool showPlan = false;
    bool showLegend = true;
    std::string isolate;  // if set, only this composite object is drawn (construction figures)
    float grain = 0.035f;
    // v2 pipeline
    Quality quality = Quality::High;
    bool cinematic = true;   // HDR + shadows + SSAO + fog + bloom + tone mapping (N toggles the v1 look)
    bool pbr = true;         // GGX / Cook-Torrance; false = classic Blinn-Phong (B)
    float exposure = 1.0f;
    float bloom = 0.8f;
    float fade = 1.0f;       // cinematic cut fade
    int palette = 0;         // thermal colour map: 0 ironbow, 1 white-hot, 2 rainbow (T)
    float rain = 0.0f;       // rain intensity 0..1 (R); surface wetness is scene state (Scene::wetness)
    float letterbox = 0.0f;  // cinematic 2.39:1 bars (fraction of the height per bar)
    // Feature switches (benchmark ablation: cost of each feature = frame time with it minus without it)
    bool practicalsOn = true;  // lamps, fire barrel, floodlight (key 4)
    bool shadowsOn = true, spotShadowOn = true, ssaoOn = true, fogOn = true, bloomOn = true, msaaOn = true;
};

struct FrameStats {
    int drawCalls = 0;
    long long triangles = 0;
    long long vertices = 0;  // vertices submitted (index count for indexed draws)
    int primitives = 0;      // primitive instances drawn
    int shadowDraws = 0;     // depth-only draws into the shadow maps
    int prepassDraws = 0;    // depth + normal pre-pass draws
    int grassBlades = 0;     // instanced blades submitted
    int rainDrops = 0;       // instanced rain streaks submitted
};

struct LineVertex {
    Vec3 position;
    Vec3 color;
};

class Renderer {
public:
    bool init();
    // v2 frame: shadow maps -> pre-pass -> SSAO -> volumetric fog -> HDR scene (left bound for overlays).
    void beginFrame(const Scene& scene, const CameraView& cam, int width, int height, const RenderSettings& s,
                    FrameStats& stats);
    // Resolve -> bloom -> composite (tone mapping) into 'targetFbo'.
    void endFrame(GLuint targetFbo, int width, int height, float time);
    GpuTimer& timer() { return timer_; }
    int maxSamples() const { return maxSamples_; }

    // Geometry of the current view into the bound framebuffer (sky, solids, beam, wireframe).
    void drawScene(const Scene& scene, const CameraView& cam, int width, int height, const RenderSettings& s,
                   FrameStats& stats);
    // Renders the UAV camera view as a raw heat image into an off-screen target and reads it back.
    void captureUavCamera(const Scene& scene, std::vector<uint8_t>& heat, FrameStats& stats);
    GLuint uavCameraTexture() const { return cvTarget_.texture(); }
    // Small rotating cube / sphere / cylinder / cone in corner viewports (diagnostic legend).
    static constexpr int kLegendCount = 4;
    void drawPrimitiveLegend(int x, int y, int size, float time, FrameStats& stats, bool wire = false);
    void drawLines(const std::vector<LineVertex>& verts, GLenum mode, const Mat4& viewProj, float alpha, bool depth);
    Hud& hud() { return hud_; }

private:
    Shader& programFor(const SceneNode& n, const RenderSettings& s);
    void setFrameUniforms(Shader& sh, const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s);
    void setObjectUniforms(Shader& sh, const SceneNode& n, const Mat4& world, const RenderSettings& s);
    void drawSky(const CameraView& cam, int w, int h, const RenderSettings& s, float time);
    void drawSolids(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st);
    void drawTranslucent(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                         FrameStats& st);
    void drawBeam(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st);
    void drawWire(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st);

    static bool inFrustum(const SceneNode& n, const Frustum& f);
    // RendererFrame.cpp
    struct FrameState {
        LightMatrix moon[2], spot;
        float split = 12.0f;
        int pcfTaps = 8;
        int shadowMode = 0;        // 0 none, 1 moon, 2 moon + searchlight
        bool ao = false, fog = false, hdr = false, thermalPost = false;
        RenderSettings settings;
        CameraView cam;
        float time = 0.0f;
    };
    void computeLightMatrices(const Scene& scene, const CameraView& cam, const QualityProfile& q);
    enum class Casters { All, Static, Dynamic, AllButUav };
    void drawDepthOnly(const Scene& scene, const Mat4& lightVP, Casters which, FrameStats& st);
    void renderShadowMaps(const Scene& scene, FrameStats& st);
    void renderPrepass(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st);
    void renderSsao(const CameraView& cam, const QualityProfile& q);
    void renderFog(const Scene& scene, const CameraView& cam, const RenderSettings& s, const QualityProfile& q);
    void renderBloom();
    void bindLightingTextures();
    void setShadowUniforms(Shader& sh, const RenderSettings& s);
    void gatherPracticals(const Scene& scene, const CameraView& cam);
    void setPracticalUniforms(Shader& sh, bool enabled);
    static constexpr int kMaxPracticals = 8;
    int practicalCount_ = 0;
    float practicalPos_[4 * kMaxPracticals] = {}, practicalColor_[4 * kMaxPracticals] = {},
          practicalDir_[4 * kMaxPracticals] = {};

    std::unique_ptr<Shader> flat_, gouraud_, phong_, beam_, line_, sky_;
    std::unique_ptr<Shader> depth_, prepass_, ssao_, blur_, fog_, bloomDown_, bloomUp_, composite_;
    void drawGrass(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st);
    GrassField grass_;
    RainField rain_;
    void drawRain(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st);
    GroundMap ground_;
    bool groundBuilt_ = false;
    FrameBuffers fb_;
    ShadowMaps shadows_;
    GpuTimer timer_;
    FrameState frame_;
    GLuint whiteTex_ = 0;
    Vec3 moonDir_{0, -1, 0};
    static constexpr int kUnitMoonShadow = 5, kUnitSpotShadow = 6, kUnitAO = 7, kUnitGround = 8, kUnitTrail = 9;
    GLuint trailTex_ = 0;
    uint32_t trailVersion_ = 0xFFFFFFFFu;
    void uploadHeatTrail(const Scene& scene);
    int maxSamples_ = 4;
    std::map<PrimitiveType, std::unique_ptr<Mesh>> meshes_;
    GLuint emptyVao_ = 0, lineVao_ = 0, lineVbo_ = 0;
    RenderTarget cvTarget_;
    Hud hud_;
};

}  // namespace ts
