#include "rendering/RayTracerGL.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "objects/BaseLayout.h"
#include "scene/Scene.h"

namespace ts {

namespace {

constexpr int kGroup = 8;   // compute work group side (8 x 8)

GLuint makeTexture(int w, int h, GLenum internal, GLenum format, GLenum type, GLenum filter, const void* data = nullptr) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal), w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void deleteTexture(GLuint& t) {
    if (t) glDeleteTextures(1, &t);
    t = 0;
}

void bindTex(GLuint prog, const char* name, int unit, GLuint tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(prog, name), unit);
}

int groups(int n) { return (n + kGroup - 1) / kGroup; }

bool sameMat(const Mat4& a, const Mat4& b) { return std::memcmp(a.m, b.m, sizeof(a.m)) == 0; }

}  // namespace

RayTracerGL::Program::~Program() {
    if (id) glDeleteProgram(id);
}

bool RayTracerGL::Program::build(const char* name, const std::string& src) {
    GLuint s = glCreateShader(GL_COMPUTE_SHADER);
    const char* text = src.c_str();
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(len > 1 ? len : 1);
        glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "[shader %s] compile error:\n%s\n", name, log.data());
        glDeleteShader(s);
        return false;
    }
    id = glCreateProgram();
    glAttachShader(id, s);
    glLinkProgram(id);
    glDeleteShader(s);
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(id, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[shader %s] link error:\n%s\n", name, log);
        return false;
    }
    return true;
}

RayTracerGL::RayTracerGL() = default;

RayTracerGL::~RayTracerGL() {
    if (!inited_) return;
    if (gpu_) glDeleteBuffers(5, ssbo_);
    for (GLuint* t : {&hfTex_, &hfBlockTex_, &accum_, &albedo_, &normalDepth_, &tmp_[0], &tmp_[1], &out_, &cpuTex_}) deleteTexture(*t);
}

bool RayTracerGL::init() {
    inited_ = true;
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    // TS_RT_CPU=1 forces the CPU fallback (to test it on a GPU that has compute shaders).
    const bool compute = (major > 4 || (major == 4 && minor >= 3)) && glDispatchCompute != nullptr && !std::getenv("TS_RT_CPU");
    if (!compute) {
        std::printf("Ray tracing: no compute shaders (OpenGL %d.%d%s): CPU fallback for the ray-traced modes\n", major, minor,
                    std::getenv("TS_RT_CPU") ? ", TS_RT_CPU set" : "");
        gpu_ = false;
        return false;
    }
    const std::string common = glslrt::kCommon;
    gpu_ = trace_.build("rt-trace", common + glslrt::kTrace) && hybrid_.build("rt-hybrid", common + glslrt::kHybrid) &&
           light_.build("rt-light", common + glslrt::kLight) && lightResolve_.build("rt-light-resolve", common + glslrt::kLightResolve) &&
           denoise_.build("rt-denoise", glslrt::kDenoise) && output_.build("rt-output", glslrt::kOutput);
    if (!gpu_) {
        std::printf("Ray tracing: compute shaders failed to build: CPU fallback for the ray-traced modes\n");
        return false;
    }
    glGenBuffers(5, ssbo_);
    return true;
}

void RayTracerGL::sync(const Scene& scene, const rt::LightOptions& lo) {
    rebuilt_ = bridge_.sync(scene, lo, scene_) || rebuilt_;
    if (rebuilt_) samples_ = 0;
    if (gpu_) uploadScene();
    else rebuilt_ = false;
}

void RayTracerGL::uploadScene() {
    rt::packScene(scene_, packed_);
    auto upload = [](GLuint buf, const void* data, size_t bytes) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
        const size_t n = std::max<size_t>(bytes, 64);   // never a zero-sized store
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(n), nullptr, GL_DYNAMIC_DRAW);
        if (bytes) glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
    };
    upload(ssbo_[0], packed_.prims.data(), packed_.prims.size() * sizeof(rt::GpuPrim));
    upload(ssbo_[1], packed_.materials.data(), packed_.materials.size() * sizeof(rt::GpuMaterial));
    upload(ssbo_[2], packed_.nodes.data(), packed_.nodes.size() * sizeof(rt::GpuNode));
    upload(ssbo_[3], packed_.lights.data(), packed_.lights.size() * sizeof(rt::GpuLight));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    rebuilt_ = false;
    if (!hfUploaded_ && scene_.terrain()) {
        const rt::Heightfield& hf = *scene_.terrain();
        hfTex_ = makeTexture(hf.samples(), hf.samples(), GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_NEAREST, hf.texels().data());
        hfBlockTex_ = makeTexture(hf.blocksPerSide(), hf.blocksPerSide(), GL_RG32F, GL_RG, GL_FLOAT, GL_NEAREST, hf.blockTexels().data());
        hfUploaded_ = true;
    }
}

void RayTracerGL::setCommon(GLuint prog, const CameraView& cam) {
    glUseProgram(prog);
    for (int i = 0; i < 4; ++i) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(i), ssbo_[i]);
    glUniform1i(glGetUniformLocation(prog, "uNodeCount"), static_cast<int>(packed_.nodes.size()));
    glUniform1i(glGetUniformLocation(prog, "uLightCount"), static_cast<int>(packed_.lights.size()));
    const rt::Heightfield* hf = scene_.terrain();
    glUniform1i(glGetUniformLocation(prog, "uHasTerrain"), hf && hfUploaded_ ? 1 : 0);
    bindTex(prog, "uHf", 6, hfTex_);
    bindTex(prog, "uHfBlocks", 7, hfBlockTex_);
    bindTex(prog, "uGround", 8, groundTex_);
    glUniform1i(glGetUniformLocation(prog, "uHasGround"), groundTex_ ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "uGroundExtent"), groundExtent_);
    if (hf) {
        glUniform1i(glGetUniformLocation(prog, "uHfN"), hf->samples());
        glUniform1i(glGetUniformLocation(prog, "uHfBlocksN"), hf->blocksPerSide());
        glUniform1f(glGetUniformLocation(prog, "uHfRadius"), hf->radius());
        glUniform1f(glGetUniformLocation(prog, "uHfCell"), hf->cellSize());
        glUniform2f(glGetUniformLocation(prog, "uHfY"), hf->bounds().lo.y, hf->bounds().hi.y);
    }
    const rt::GpuMaterial& t = packed_.terrain;
    glUniform4f(glGetUniformLocation(prog, "uTerrA"), t.albedo[0], t.albedo[1], t.albedo[2], t.roughness);
    glUniform4f(glGetUniformLocation(prog, "uTerrE"), t.emission[0], t.emission[1], t.emission[2], t.metalness);
    glUniform4f(glGetUniformLocation(prog, "uTerrT"), t.transmit[0], t.transmit[1], t.transmit[2], t.ior);
    const rt::RtEnv& e = scene_.env;
    glUniform3f(glGetUniformLocation(prog, "uZenith"), e.zenith.x, e.zenith.y, e.zenith.z);
    glUniform3f(glGetUniformLocation(prog, "uBounce"), e.bounce.x, e.bounce.y, e.bounce.z);
    glUniform3f(glGetUniformLocation(prog, "uFogColor"), e.fogColor.x, e.fogColor.y, e.fogColor.z);
    glUniform1f(glGetUniformLocation(prog, "uFogDensity"), e.fogDensity);
    const rt::RtCamera c = rt::RtCamera::fromView(cam.view, cam.proj);
    glUniform3f(glGetUniformLocation(prog, "uCamPos"), c.position.x, c.position.y, c.position.z);
    glUniform3f(glGetUniformLocation(prog, "uCamFwd"), c.forward.x, c.forward.y, c.forward.z);
    glUniform3f(glGetUniformLocation(prog, "uCamRight"), c.right.x, c.right.y, c.right.z);
    glUniform3f(glGetUniformLocation(prog, "uCamUp"), c.up.x, c.up.y, c.up.z);
    glUniform1f(glGetUniformLocation(prog, "uTanHalfY"), c.tanHalfY);
    glUniform1f(glGetUniformLocation(prog, "uAspect"), c.aspect);
    glUniform1ui(glGetUniformLocation(prog, "uFrame"), frame_);
}

void RayTracerGL::ensureTargets(int iw, int ih, int w, int h) {
    if (iw != iw_ || ih != ih_) {
        for (GLuint* t : {&accum_, &albedo_, &normalDepth_, &tmp_[0], &tmp_[1], &cpuTex_}) deleteTexture(*t);
        iw_ = iw;
        ih_ = ih;
        if (gpu_) {
            accum_ = makeTexture(iw, ih, GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_LINEAR);
            albedo_ = makeTexture(iw, ih, GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_LINEAR);
            normalDepth_ = makeTexture(iw, ih, GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_NEAREST);
            tmp_[0] = makeTexture(iw, ih, GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_LINEAR);
            tmp_[1] = makeTexture(iw, ih, GL_RGBA32F, GL_RGBA, GL_FLOAT, GL_LINEAR);
            splatSize_ = static_cast<size_t>(iw) * ih * 3 * sizeof(GLuint);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_[4]);
            std::vector<GLuint> zero(static_cast<size_t>(iw) * ih * 3, 0u);
            glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(splatSize_), zero.data(), GL_DYNAMIC_COPY);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        } else {
            cpuTex_ = makeTexture(iw, ih, GL_RGBA16F, GL_RGB, GL_FLOAT, GL_LINEAR);
        }
        samples_ = 0;
    }
    if (gpu_ && (w != ow_ || h != oh_)) {
        deleteTexture(out_);
        out_ = makeTexture(w, h, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
        ow_ = w;
        oh_ = h;
    }
}

bool RayTracerGL::accumulationValid(const RtSettings& s, const CameraView& cam, int iw, int ih, float sceneTime) {
    const bool same = s.mode == lastMode_ && sameMat(cam.view, lastView_) && sameMat(cam.proj, lastProj_) && iw == iw_ && ih == ih_;
    lastMode_ = s.mode;
    lastView_ = cam.view;
    lastProj_ = cam.proj;
    const bool animating = sceneTime != lastTime_;
    lastTime_ = sceneTime;
    if (!same) samples_ = 0;
    // A moving scene keeps a short running average (moving objects leave only a brief trail).
    if (animating && samples_ > s.temporalCap) samples_ = s.temporalCap;
    return same;
}

std::string RayTracerGL::status(rt::RtMode mode) const {
    char buf[200];
    if (mode == rt::RtMode::Raster) return "RASTER";
    if (mode == rt::RtMode::Hybrid)
        std::snprintf(buf, sizeof(buf), "%s  %s  |  %zu prims, BVH %zu nodes", rt::rtModeName(mode),
                      gpu_ ? "RT shadows + reflections" : "(needs GL 4.3 compute: raster shown)", scene_.prims.size(),
                      scene_.bvh().nodes().size());
    else
        std::snprintf(buf, sizeof(buf), "%s  %dx%d  %d spp%s%s  |  %zu prims, BVH %zu nodes", rt::rtModeName(mode), iw_, ih_, samples_,
                      denoisedLast_ ? "  denoised" : "", gpu_ ? "" : "  CPU fallback", scene_.prims.size(), scene_.bvh().nodes().size());
    return buf;
}

rt::RayTree RayTracerGL::pick(const CameraView& cam, float u, float v) const {
    return rt::debugTrace(scene_, rt::RtCamera::fromView(cam.view, cam.proj), u, v);
}

GLuint RayTracerGL::cpuFallback(const RtSettings& s, const CameraView& cam, int w, int h) {
    // Tiny CPU image (the same tracer as the tests); accumulated like the GPU path while the view is still.
    const int iw = std::max(32, w / 8), ih = std::max(18, h / 8);
    const bool valid = accumulationValid(s, cam, iw, ih, lastTime_);
    ensureTargets(iw, ih, w, h);
    if (!valid || cpuAccum_.size() != static_cast<size_t>(iw) * ih) {
        cpuAccum_.assign(static_cast<size_t>(iw) * ih, Vec3{});
        samples_ = 0;
    }
    rt::Image img(iw, ih);
    const rt::RtCamera c = rt::RtCamera::fromView(cam.view, cam.proj);
    rt::TraceSettings ts;
    ts.maxBounces = s.maxBounces;
    if (s.mode == rt::RtMode::Whitted) rt::renderWhitted(scene_, c, img, 1, frame_ + 1, ts);
    else if (s.mode == rt::RtMode::PathTrace) rt::renderPath(scene_, c, img, 1, frame_ + 1, ts);
    else rt::renderLight(scene_, c, img, static_cast<long long>(iw) * ih * 16, frame_ + 1, ts);
    ++samples_;
    const float k = 1.0f / static_cast<float>(samples_);
    for (size_t i = 0; i < cpuAccum_.size(); ++i) cpuAccum_[i] = cpuAccum_[i] + (img.px[i] - cpuAccum_[i]) * k;
    // rows: the CPU image has v = 0 at the top, GL textures start at the bottom
    std::vector<Vec3> flipped(cpuAccum_.size());
    for (int y = 0; y < ih; ++y)
        std::memcpy(&flipped[static_cast<size_t>(ih - 1 - y) * iw], &cpuAccum_[static_cast<size_t>(y) * iw], iw * sizeof(Vec3));
    glBindTexture(GL_TEXTURE_2D, cpuTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, iw, ih, GL_RGB, GL_FLOAT, flipped.data());
    ++frame_;
    denoisedLast_ = false;
    return cpuTex_;
}

GLuint RayTracerGL::render(const RtSettings& s, const CameraView& cam, int w, int h, GLuint sky, float sceneTime) {
    if (!gpu_) {
        lastTime_ = sceneTime;
        return cpuFallback(s, cam, w, h);
    }
    const bool whitted = s.mode == rt::RtMode::Whitted;
    const float scale = clampf(whitted ? s.whittedScale : s.scale, 0.125f, 1.0f);
    const int iw = std::max(16, static_cast<int>(w * scale)), ih = std::max(16, static_cast<int>(h * scale));
    accumulationValid(s, cam, iw, ih, sceneTime);
    ensureTargets(iw, ih, w, h);
    ++samples_;
    const float blend = 1.0f / static_cast<float>(samples_);

    glBindImageTexture(0, accum_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(1, albedo_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    glBindImageTexture(2, normalDepth_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
    if (s.mode == rt::RtMode::LightTrace) {
        // Light paths -> splat buffer
        setCommon(light_.id, cam);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo_[4]);
        rt::TraceSettings ts;
        ts.emitRadius = layout::kBaseRadius * 1.6f;
        const rt::Tracer tr(scene_, ts);
        std::vector<float> power = tr.emitterPower();
        if (power.size() > 24) power.resize(24);
        float total = 0.0f;
        for (float p : power) total += p;
        float cdf[24] = {}, pdf[24] = {};
        float acc = 0.0f;
        for (size_t i = 0; i < power.size(); ++i) {
            pdf[i] = total > 0.0f ? power[i] / total : 0.0f;
            acc += pdf[i];
            cdf[i] = acc;
        }
        if (!power.empty()) cdf[power.size() - 1] = 1.0f;
        const GLuint paths = static_cast<GLuint>(std::max(1, s.lightPathsK) * 1024);
        const float splatScale = 4.0e9f / (static_cast<float>(paths) * std::exp2(s.exposureKeyLog2) * 256.0f);
        glUniform1i(light_.loc("uEmitters"), static_cast<int>(power.size()));
        glUniform1fv(light_.loc("uEmitCdf"), 24, cdf);
        glUniform1fv(light_.loc("uEmitPdf"), 24, pdf);
        glUniform1f(light_.loc("uEmitRadius"), ts.emitRadius);
        glUniform3f(light_.loc("uEmitCentre"), 0.0f, 0.0f, 0.0f);
        const rt::RtCamera rc = rt::RtCamera::fromView(cam.view, cam.proj);
        const Vec3 focus = rc.position + rc.forward * 9.0f;
        glUniform3f(light_.loc("uFocus"), focus.x, focus.y, focus.z);
        glUniform1f(light_.loc("uFocusRadius"), 14.0f);
        glUniform2i(light_.loc("uRes"), iw, ih);
        glUniform1f(light_.loc("uSplatScale"), splatScale);
        glUniform1i(light_.loc("uMaxBounces"), s.maxBounces);
        glUniform1i(light_.loc("uRrStart"), 2);
        glUniform1ui(light_.loc("uPathsPerFrame"), paths);
        glDispatchCompute((paths + 63) / 64, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        lastPaths_ = paths;
        // Resolve: splats / paths + directly seen emitters and sky, into the running average
        setCommon(lightResolve_.id, cam);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ssbo_[4]);
        bindTex(lightResolve_.id, "uSky", 0, sky);
        glUniform2i(lightResolve_.loc("uRes"), iw, ih);
        glUniform1f(lightResolve_.loc("uInvScalePaths"), 1.0f / (splatScale * static_cast<float>(paths)));
        glUniform1f(lightResolve_.loc("uBlend"), blend);
        glDispatchCompute(groups(iw), groups(ih), 1);
    } else {
        setCommon(trace_.id, cam);
        bindTex(trace_.id, "uSky", 0, sky);
        glUniform1i(trace_.loc("uMode"), static_cast<int>(s.mode));
        glUniform1i(trace_.loc("uMaxDepth"), s.whittedDepth);
        glUniform1i(trace_.loc("uMaxBounces"), s.maxBounces);
        glUniform1i(trace_.loc("uRrStart"), 2);
        glUniform1f(trace_.loc("uBlend"), blend);
        // Sub-pixel jitter: Halton (2, 3) -> progressive anti-aliasing while the view is still
        auto halton = [](unsigned i, unsigned b) {
            float f = 1.0f, r = 0.0f;
            while (i > 0) {
                f /= static_cast<float>(b);
                r += f * static_cast<float>(i % b);
                i /= b;
            }
            return r;
        };
        const unsigned k = static_cast<unsigned>(samples_) % 64u + 1u;
        glUniform2f(trace_.loc("uJitter"), samples_ <= 1 ? 0.5f : halton(k, 2), samples_ <= 1 ? 0.5f : halton(k, 3));
        glUniform2i(trace_.loc("uRes"), iw, ih);
        glDispatchCompute(groups(iw), groups(ih), 1);
    }
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);

    // Denoise (path / light tracing): 5 a-trous passes on the demodulated image
    GLuint src = accum_;
    bool remod = false;
    denoisedLast_ = false;
    if (s.denoise && !whitted) {
        glUseProgram(denoise_.id);
        glUniform2i(denoise_.loc("uRes"), iw, ih);
        // Luminance edge-stopping relaxes with the sample count's noise (1 / sqrt(n)).
        const float noise = s.mode == rt::RtMode::LightTrace ? 8.0f : 2.5f;   // light tracing is the noisier estimator
        glUniform1f(denoise_.loc("uSigma"), std::max(0.08f, noise / std::sqrt(static_cast<float>(samples_))));
        glBindImageTexture(1, albedo_, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(2, normalDepth_, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        const int passes = samples_ < 64 ? 5 : (samples_ < 512 ? 3 : 1);
        for (int i = 0; i < passes; ++i) {
            const GLuint in = i == 0 ? accum_ : tmp_[(i + 1) & 1];
            const GLuint dst = tmp_[i & 1];
            glBindImageTexture(0, in, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
            glBindImageTexture(5, dst, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glUniform1i(denoise_.loc("uStep"), 1 << i);
            glUniform1i(denoise_.loc("uFirst"), i == 0 ? 1 : 0);
            glDispatchCompute(groups(iw), groups(ih), 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            src = dst;
        }
        remod = true;
        denoisedLast_ = true;
    }
    // Output: upscale to w x h (+ remodulation)
    glUseProgram(output_.id);
    bindTex(output_.id, "uSrc", 0, src);
    bindTex(output_.id, "uAlbedoTex", 1, albedo_);
    glUniform1i(output_.loc("uRemodulate"), remod ? 1 : 0);
    glUniform2i(output_.loc("uRes"), w, h);
    glBindImageTexture(3, out_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(groups(w), groups(h), 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    ++frame_;
    return out_;
}

GLuint RayTracerGL::hybrid(const RtSettings& s, const CameraView& cam, int w, int h, const RtRasterInputs& in, float sceneTime) {
    (void)s;
    (void)sceneTime;
    if (!gpu_) return 0;
    ensureTargets(iw_ > 0 ? iw_ : 16, ih_ > 0 ? ih_ : 16, w, h);
    setCommon(hybrid_.id, cam);
    bindTex(hybrid_.id, "uScene", 0, in.scene);
    bindTex(hybrid_.id, "uDepth", 1, in.depth);
    bindTex(hybrid_.id, "uSurface", 2, in.surface);
    bindTex(hybrid_.id, "uFog", 3, in.fog);
    glUniform1i(hybrid_.loc("uUseFog"), in.useFog ? 1 : 0);
    glUniform1i(hybrid_.loc("uShadows"), 1);
    glUniform1i(hybrid_.loc("uReflections"), 1);
    // light 0 is the key light when it shines; the searchlight is the Spot entry
    int key = -1, spot = -1;
    for (size_t i = 0; i < scene_.lights.size(); ++i) {
        if (scene_.lights[i].type == rt::LightType::Directional && key < 0) key = static_cast<int>(i);
        if (scene_.lights[i].type == rt::LightType::Spot && spot < 0) spot = static_cast<int>(i);
    }
    glUniform1i(hybrid_.loc("uKeyLight"), key);
    glUniform1i(hybrid_.loc("uSpotLight"), spot);
    glUniformMatrix4fv(hybrid_.loc("uViewProj"), 1, GL_FALSE, in.viewProjJ.m);
    const Mat4 inv = inverse(in.viewProjJ);
    glUniformMatrix4fv(hybrid_.loc("uInvViewProj"), 1, GL_FALSE, inv.m);
    glUniform2i(hybrid_.loc("uRes"), w, h);
    glBindImageTexture(3, out_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(groups(w), groups(h), 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    ++frame_;
    return out_;
}

}  // namespace ts
