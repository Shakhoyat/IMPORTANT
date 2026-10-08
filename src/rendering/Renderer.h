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
#if __has_include("rendering/Effects.h")
#include "rendering/Effects.h"
#define TS_HAS_EFFECTS 1
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
#include "rendering/Instancing.h"
#include "rendering/Mesh.h"
#include "rendering/RenderTarget.h"
#include "rendering/Shader.h"
#include "scene/Camera.h"
#include "raytrace/RayTracer.h"   // v7 WP-H: RtMode, RayTree

namespace ts {

namespace fx { struct FrameSnapshot; }  // v7 WP-I (math/ExplorerMath.h)

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
    bool bumpOn = true;           // procedural surface relief (surfaceBump); false only for the lab ON/OFF figures
    // v7 WP-H ray tracing (';' cycles rtMode). Raster = the v6 renderer bit for bit; the other modes only apply to the
    // graded visible view (cinematic, solid edges).
    rt::RtMode rtMode = rt::RtMode::Raster;
    float rtScale = 0.5f;         // internal resolution of the path / light tracer (fraction of the frame)
    float rtWhittedScale = 0.75f; // internal resolution of the Whitted tracer (the report figures use 1)
    int rtBounces = 4;            // path / light tracing bounces
    bool rtDenoise = true;        // a-trous denoiser (path / light tracing)
    int rtLightPathsK = 256;      // light paths per frame, thousands (light tracing)
    bool rtOverlay = true;        // status line of the ray-traced modes + the ray debugger's tree
};

// Internal render targets that the lab figures read back (Renderer::labReadBuffer, after endFrame).
enum class LabBuffer {
    ShadowMoonNear, ShadowMoonWorld, Ssao, FogInscatter, FogTransmittance, HdrColour, HdrLogLuminance,
    Lit, Bloom, Reflectivity, SurfaceNormal, Depth
};
struct LabImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb;   // 8-bit RGB, rows top to bottom
    std::string note;           // value range / scaling used, for the caption
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

class RayTracerGL;

class Renderer {
public:
    Renderer();
    ~Renderer();
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
    // cameraOverride: render from this pose instead of the UAV's reported gimbal pose (pointing jitter).
    void captureUavCamera(const Scene& scene, std::vector<uint8_t>& heat, FrameStats& stats, int uav = 0,
                          const Mat4* cameraOverride = nullptr);
    // v7 WP-Q: the same in two halves: render + start the read-back into pixel buffer 'slot' (0..3) without waiting,
    // then collect it after other work (the main frame) has been submitted. Same image as captureUavCamera.
    void beginUavCamera(const Scene& scene, FrameStats& stats, int uav, const Mat4* cameraOverride, int slot);
    void finishUavCamera(int slot, std::vector<uint8_t>& heat) { cvTarget_.collect(slot, heat); }
    GLuint uavCameraTexture() const { return cvTarget_.texture(); }
    // Small rotating cube / sphere / cylinder / cone in corner viewports (diagnostic legend).
    static constexpr int kLegendCount = 4;
    void drawPrimitiveLegend(int x, int y, int size, float time, FrameStats& stats, bool wire = false);
    void drawLines(const std::vector<LineVertex>& verts, GLenum mode, const Mat4& viewProj, float alpha, bool depth);
    Hud& hud() { return hud_; }

    // RendererLab.cpp: helpers for the lab figures (docs/v5_notes/v5-f2.md).
    // Reads one internal buffer of the LAST frame (after endFrame) as a displayable 8-bit image. False if the
    // buffer does not exist at this quality (e.g. the fog or SSAO pass is off).
    bool labReadBuffer(LabBuffer which, LabImage& out);
    // Forget the cached static shadow map: needed whenever the scene's leaves are swapped for a lab stage.
    void labInvalidateCaches();
    // Replaces the unit sphere mesh by a coarse one (slices x stacks) for the Gouraud / flat close-up; 0 = restore.
    void labSphereResolution(int slices, int stacks);
    // v7 WP-I Frame Explorer (RendererLab.cpp): float copies of the LAST frame's internal buffers, matrices and
    // switches (math/ExplorerMath.h). Call right after endFrame.
    bool labSnapshot(fx::FrameSnapshot& out);

    // v7 WP-H ray tracing (RendererRT.cpp, RayTracerGL). The ray-traced modes are chosen by RenderSettings::rtMode.
    RayTracerGL* rayTracer() { return rt_.get(); }
    // Ray debugger (key '): while active, a click traces that pixel on the CPU and its ray tree is drawn in 3D with
    // labels on top of every frame. u, v in [0, 1] over the window, v = 0 at the top; uses the last frame's camera.
    void setRayDebug(bool on);
    bool rayDebug() const { return rayDebug_; }
    bool rayDebugPick(const Scene& scene, float u, float v);
    const rt::RayTree* rayTree() const { return rayTree_.nodes.empty() ? nullptr : &rayTree_; }
    // One line for the HUD: mode, resolution, samples, GPU time ("" in raster mode without the debugger).
    std::string rtStatusLine() const;

private:
    Shader& programFor(const SceneNode& n, const RenderSettings& s);
    void setFrameUniforms(Shader& sh, const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s);
    void setObjectUniforms(Shader& sh, const SceneNode& n, const Mat4& world, const RenderSettings& s);
    // v7 WP-Q: the per-part shader parameters (the same values for the uniform and the instanced path).
    struct ObjectParams {
        Mat4 model;
        Mat3 normal;
        Vec3 baseColor, centre;
        float heat = 0, emissive = 0, specular = 0, shininess = 0, roughness = 0, metalness = 0;
        float heatVariation = 0, fogged = 0, opacity = 1, radius = 0;
        int surface = 0;
    };
    ObjectParams objectParams(const SceneNode& n, const Mat4& world, const RenderSettings& s) const;
    // v7 WP-Q instancing (Instancing.h): one glDrawElementsInstanced per run of parts with the same mesh.
    // 'nodes' must be grouped by primitive; texelsPerPart 11 (lit), 7 (pre-pass) or 4 (depth only).
    bool useInstancing(const RenderSettings& s) const { return instancing_ && s.indexed && s.depthTest; }
    bool uploadInstances(const std::vector<const SceneNode*>& nodes, int texelsPerPart, const RenderSettings* s);
    int drawInstanced(Shader& sh, const std::vector<const SceneNode*>& nodes, FrameStats* st);
    Shader* instancedFor(const Shader* program) const;
    void drawSky(const CameraView& cam, int w, int h, const RenderSettings& s, float time);
    void drawSolids(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st);
    void drawTranslucent(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s,
                         FrameStats& st);
    void drawBeam(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st);
    void drawWire(const Scene& scene, const CameraView& cam, const RenderSettings& s, FrameStats& st);
    void surfaceWrites(bool on) const;  // enable / disable the surface render target (main scene only)
    void renderUavCamera(const Scene& scene, FrameStats& stats, int uav, const Mat4* cameraOverride);  // into cvTarget_

    static bool inFrustum(const SceneNode& n, const Frustum& f);
    // v8 specular LOD: curved parts (cylinders, spheres, cones) thinner than a few pixels get a wider GGX lobe, so the
    // sun's mirror highlight on wires, rails, poles and posts stays a soft sheen instead of a sparkling pixel line that
    // bloom and the lens flare blow up into white blobs. specLodPixel_ = metres per pixel at 1 m (0 = off).
    float specularLod(const SceneNode& n, const Mat4& world, float roughness) const;
    Vec3 specLodEye_;
    float specLodPixel_ = 0.0f;
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
        bool cameraIndoor = false;  // inside the bunker: no lens flare (screens / tubes would ghost everywhere)
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
    // Explosions, smoke, tracers, missiles, scorch marks from the combat simulation (rendering/Effects).
    void drawEffects(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s, FrameStats& st,
                     bool transparent);
    void bindLightingTextures();
    void setShadowUniforms(Shader& sh, const RenderSettings& s);
    void gatherPracticals(const Scene& scene, const CameraView& cam);
    void setPracticalUniforms(Shader& sh, bool enabled);
    static constexpr int kMaxPracticals = 16;
    int practicalCount_ = 0;
    float practicalPos_[4 * kMaxPracticals] = {}, practicalColor_[4 * kMaxPracticals] = {},
          practicalDir_[4 * kMaxPracticals] = {};
    int indoorCount_ = 0;
    float indoorMin_[3 * 8] = {}, indoorMax_[3 * 8] = {};
    float indoorLight_[2 * 8] = {};   // v8: sky fraction, key light pass per volume
    void setIndoorUniforms(Shader& sh) const;
    // v6 time of day: the scene's atmosphere of the current frame, and the factor of a practical light / lamp
    // (outdoor lamps fade out by day; fires, strobes, muzzle flashes and indoor lamps stay as they are).
    Atmosphere atmo_;
    const WalkSpace* walk_ = nullptr;
    float practicalFactor(const SceneNode& n) const;

    std::unique_ptr<Shader> flat_, gouraud_, phong_, beam_, line_, sky_;
    std::unique_ptr<Shader> depth_, prepass_, ssao_, blur_, fog_, bloomDown_, bloomUp_, composite_;
    std::unique_ptr<Shader> combine_, taa_, reduce_, adapt_, flare_, streak_, dof_, dofCombine_;
    std::unique_ptr<Shader> flatI_, gouraudI_, phongI_, depthI_, prepassI_;   // v7 WP-Q instanced variants
    InstanceStream instances_;
    std::vector<float> instanceScratch_;
    bool instancing_ = false;                 // off: TS_NO_INSTANCING=1, or no buffer textures
#ifdef TS_HAS_FOLIAGE
    FoliageField foliage_;
#endif
#ifdef TS_HAS_PARTICLES
    ParticleField particles_;
#endif
#ifdef TS_HAS_EFFECTS
    EffectsRenderer effects_;
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
    static constexpr int kUnitInstances = 11;   // v7 WP-Q (unit 10 = soft-particle depth of the effects)
    GLuint trailTex_ = 0;
    uint32_t trailVersion_ = 0xFFFFFFFFu;
    void uploadHeatTrail(const Scene& scene);
    int maxSamples_ = 4;
    std::map<PrimitiveType, std::unique_ptr<Mesh>> meshes_;
    GLuint emptyVao_ = 0, lineVao_ = 0, lineVbo_ = 0;
    RenderTarget cvTarget_;
    Hud hud_;
    // v7 WP-H (RendererRT.cpp)
    std::unique_ptr<RayTracerGL> rt_;
    rt::RtMode rtActive_ = rt::RtMode::Raster;    // mode of the current frame (Raster outside the graded view)
    bool skyOnly_ = false;                        // full ray-traced frame: the raster pass draws only the sky
    bool rayDebug_ = false;
    rt::RayTree rayTree_;
    CameraView rtCam_;                            // unjittered camera of the last frame
    int rtW_ = 0, rtH_ = 0;
    void rtBegin(const Scene& scene, const CameraView& cam, int w, int h, const RenderSettings& s);
    GLuint rtEnd();                               // hybrid / full RT image replacing the combine pass
    void drawRayOverlay(GLuint targetFbo, int w, int h);
};

}  // namespace ts
