#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "math/Frustum.h"
#include "math/ShadowMath.h"
// Foliage / particles are being written in parallel; the renderer compiles without them until they exist.
#if __has_include("rendering/Foliage.h")
#include "rendering/Foliage.h"
#define TS_HAS_FOLIAGE 1
#endif
#if __has_include("rendering/Particles.h")
#include "rendering/Particles.h"
#define TS_HAS_PARTICLES 1
#endif
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
enum class Quality { Low = 0, High = 1, Ultra = 2, Epic = 3 };
constexpr int kQualityCount = 4;

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
    // v3
    int prepassDivisor = 2;      // pre-pass / SSAO resolution = screen / divisor
    bool taa = true;             // temporal anti-aliasing
    bool ssr = false;            // screen-space reflections (puddles, wet ground)
    int ssrSteps = 48;
    bool flare = true;           // lens flare + anamorphic streak
    bool dof = false;            // depth of field (cinematic shots)
    bool motionBlur = false;     // camera motion blur (cinematic / video)
    float foliageDensity = 0.5f; // foliage cards on trees and bushes (fraction of the EPIC count)
    int fireflies = 200;         // instanced fireflies and drifting motes
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
    // v3 post-processing switches (the quality preset decides what is available)
    bool taaOn = true, ssrOn = true, flareOn = true, autoExposureOn = true, foliageOn = true, particlesOn = true;
    float dof = 0.0f;             // depth-of-field strength (cinematic shots); 0 = off
    float focusDistance = -1.0f;  // metres; < 0 = autofocus on the frame centre
    float motionBlur = 0.0f;      // camera motion blur, shutter fraction (0.5 = 180 degrees); 0 = off
    bool hideUav = false;         // first-person UAV camera: the airframe is behind the lens
    float nucShutter = 0.0f;      // thermal sensor: non-uniformity-correction shutter closed (0..1)
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
    // Forget the temporal history (TAA, exposure / sensor gain): at camera cuts and for new capture shots.
    void resetHistory() { historyValid_ = false; adaptValid_ = false; }
    // Adapted frame statistics (min, max, mean, mean of squares) of log2 luminance (visible) or heat
    // (thermal); read back from the GPU, for logs and calibration only.
    void adaptedStats(float out[4]) const;
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
    void surfaceWrites(bool on) const;  // enable / disable the surface render target (main scene only)

    static bool inFrustum(const SceneNode& n, const Frustum& f);
    // RendererFrame.cpp
    struct FrameState {
        LightMatrix moon[2], spot;
        float split = 12.0f;
        int pcfTaps = 8;
        int shadowMode = 0;        // 0 none, 1 moon, 2 moon + searchlight
        bool ao = false, fog = false, hdr = false, thermalPost = false;
        bool taa = false, ssr = false, flare = false, dof = false;
        int ssrSteps = 48;
        Mat4 viewProj, viewProjJ;   // unjittered / jittered (what the depth buffer holds)
        float dt = 1.0f / 60.0f;    // seconds since the previous frame (temporal adaptation)
        float jitterUV[2] = {0.0f, 0.0f};  // screen offset of the current TAA jitter (UV units)
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
    void renderBloom(GLuint source);
    // v3 post chain (RendererFrame.cpp)
    GLuint renderCombine();                   // fog + SSR -> lit
    GLuint renderTaa(GLuint current);         // -> history target (or 'current' when TAA is off)
    void renderStatistics(GLuint image);      // exposure / AGC statistics -> adapt_
    void renderFlare();
    GLuint renderDof(GLuint image);
    void drawFoliage(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st);
    void drawParticles(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st);
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
    std::unique_ptr<Shader> combine_, taa_, reduce_, adapt_, flare_, streak_, dof_, dofCombine_;
#ifdef TS_HAS_FOLIAGE
    FoliageField foliage_;
#endif
#ifdef TS_HAS_PARTICLES
    ParticleField particles_;
#endif
    bool mrt_ = false;                  // the main scene target (with the surface attachment) is bound
    bool historyValid_ = false, adaptValid_ = false;
    int historyIndex_ = 0, adaptIndex_ = 0;
    unsigned frameIndex_ = 0;
    Mat4 prevViewProj_;
    Vec3 prevCamPos_, prevCamFwd_;
    float prevTime_ = -1.0f;
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
