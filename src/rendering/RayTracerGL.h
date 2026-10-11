// v7 WP-H: GPU ray tracer. The scene (analytic primitives + SAH BVH + terrain height field + lights, see
// src/raytrace) is mirrored into shader storage buffers every frame (BVH refit on the CPU, ~1 ms) and traced by
// GLSL 4.30 compute shaders (ShadersRT.cpp):
//   hybrid      raster G-buffer + ray-traced soft shadows (key light, searchlight) and mirror reflections (vs SSR)
//   Whitted     recursive ray tracing with an explicit ray stack: reflection, refraction, Fresnel, shadow rays
//   path        progressive path tracing (NEE, cosine / GGX importance sampling, Russian roulette), running
//               average reset on camera motion (capped while the scene animates), a-trous denoiser, upscale
//   light       reverse ray tracing: light paths from the sun / moon / searchlight / lamps / sky splatted to the
//               camera with atomics (caustics through glass and mirrors), same accumulation + denoiser
// Without GL 4.3 compute the full ray-traced modes fall back to the CPU tracer at a low resolution (hybrid then
// shows the raster image). Public use beyond the Renderer: the Frame Explorer may call reflectionImage-like
// helpers through Renderer::rayTracer() (see docs/v7_notes/wp-h.md).
#pragma once

#include <glad/gl.h>

#include <memory>
#include <string>
#include <vector>

#include "raytrace/GpuLayout.h"
#include "raytrace/RayTracer.h"
#include "raytrace/SceneBridge.h"
#include "scene/Camera.h"

namespace ts {

class Scene;

namespace glslrt {
extern const char* kCommon;
extern const char* kTrace;
extern const char* kHybrid;
extern const char* kLight;
extern const char* kLightResolve;
extern const char* kDenoise;
extern const char* kOutput;
}  // namespace glslrt

// What the tracer needs from RenderSettings (the Renderer fills it).
struct RtSettings {
    rt::RtMode mode = rt::RtMode::Raster;
    float scale = 0.5f;          // internal resolution of the path / light tracer (fraction of the output)
    float whittedScale = 1.0f;   // internal resolution of the Whitted tracer
    int maxBounces = 4;          // path / light tracing
    int whittedDepth = 5;
    bool denoise = true;
    int lightPathsK = 256;       // light paths per frame (thousands)
    int temporalCap = 12;        // running-average length while the scene animates (static scene: unlimited)
    float exposureKeyLog2 = -5.2f;
};

// Raster buffers of the current frame for the hybrid mode.
struct RtRasterInputs {
    GLuint scene = 0, depth = 0, surface = 0, fog = 0;
    bool useFog = false;
    Mat4 viewProjJ;              // jittered view-projection the depth buffer was rendered with
};

class RayTracerGL {
public:
    RayTracerGL();
    ~RayTracerGL();
    // Compiles the compute programs. False (and a log line) when compute shaders are unavailable: CPU fallback.
    bool init();
    bool gpuAvailable() const { return gpu_; }

    // Scene -> RtScene (bridge: rebuild or refit) -> GPU buffers. Call once per ray-traced frame.
    void sync(const Scene& scene, const rt::LightOptions& lo);
    // Full ray-traced modes (Whitted, path, light): HDR image of w x h. sky = raster sky texture (w x h).
    GLuint render(const RtSettings& s, const CameraView& cam, int w, int h, GLuint sky, float sceneTime);
    // Hybrid mode: replaces the combine pass (fog + SSR) of the raster frame.
    GLuint hybrid(const RtSettings& s, const CameraView& cam, int w, int h, const RtRasterInputs& in, float sceneTime);

    // The raster renderer's ground map (terrain materials) for the terrain albedo; 0 = plain terrain colour.
    void setGround(GLuint tex, float extent) {
        groundTex_ = tex;
        groundExtent_ = extent;
    }
    // CPU ray tree of the pixel (u, v in [0, 1], v = 0 at the top) seen by 'cam' in the synced scene.
    rt::RayTree pick(const CameraView& cam, float u, float v) const;

    const rt::RtScene& scene() const { return scene_; }
    const rt::SceneBridge& bridge() const { return bridge_; }
    int samples() const { return samples_; }        // samples in the running average (full RT modes)
    int internalWidth() const { return iw_; }
    int internalHeight() const { return ih_; }
    bool denoised() const { return denoisedLast_; }
    void resetAccumulation() { samples_ = 0; }
    // One status line: mode, resolution, samples, rays / paths per frame, fallback note.
    std::string status(rt::RtMode mode) const;

private:
    struct Program {
        GLuint id = 0;
        bool build(const char* name, const std::string& src);
        GLint loc(const char* name) const { return glGetUniformLocation(id, name); }
        ~Program();
    };
    void ensureTargets(int iw, int ih, int w, int h);
    void setCommon(GLuint prog, const CameraView& cam);
    void uploadScene();
    bool accumulationValid(const RtSettings& s, const CameraView& cam, int iw, int ih, float sceneTime);
    GLuint cpuFallback(const RtSettings& s, const CameraView& cam, int w, int h);

    bool gpu_ = false, inited_ = false;
    Program trace_, hybrid_, light_, lightResolve_, denoise_, output_;
    GLuint ssbo_[5] = {0, 0, 0, 0, 0};   // prims, materials, nodes, lights, splat
    GLuint hfTex_ = 0, hfBlockTex_ = 0, groundTex_ = 0;
    float groundExtent_ = 1.0f;
    GLuint accum_ = 0, albedo_ = 0, normalDepth_ = 0, tmp_[2] = {0, 0}, out_ = 0, cpuTex_ = 0;
    int iw_ = 0, ih_ = 0, ow_ = 0, oh_ = 0;
    size_t splatSize_ = 0;

    rt::SceneBridge bridge_;
    rt::RtScene scene_;
    rt::GpuScene packed_;
    bool rebuilt_ = true;
    bool hfUploaded_ = false;

    // accumulation state
    int samples_ = 0;
    unsigned frame_ = 0;
    bool denoisedLast_ = false;
    rt::RtMode lastMode_ = rt::RtMode::Raster;
    Mat4 lastView_, lastProj_;
    float lastTime_ = -1.0f;
    long long lastPaths_ = 0;
    std::vector<Vec3> cpuAccum_;
};

}  // namespace ts
