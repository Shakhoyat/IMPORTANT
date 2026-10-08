#include "app/HeadlessGL.h"

#include <glad/gl.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace ts {

double nowSeconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<double>(Clock::now() - start).count();
}

bool noDisplayAvailable() {
#if defined(__linux__)
    const char* x = std::getenv("DISPLAY");
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    return (!x || !*x) && (!wl || !*wl);
#else
    return false;
#endif
}

#if defined(__linux__)

namespace {

// The few EGL 1.5 types, enums and entry points used here (values from the Khronos registry).
using EGLDisplay = void*;
using EGLConfig = void*;
using EGLContext = void*;
using EGLSurface = void*;
using EGLDeviceEXT = void*;
using EGLint = int32_t;
using EGLBoolean = unsigned int;
using EGLenum = unsigned int;

constexpr EGLint EGL_NONE = 0x3038, EGL_SURFACE_TYPE = 0x3033, EGL_PBUFFER_BIT = 0x0001;
constexpr EGLint EGL_RENDERABLE_TYPE = 0x3040, EGL_OPENGL_BIT = 0x0008;
constexpr EGLint EGL_RED_SIZE = 0x3024, EGL_GREEN_SIZE = 0x3023, EGL_BLUE_SIZE = 0x3022, EGL_DEPTH_SIZE = 0x3025;
constexpr EGLenum EGL_OPENGL_API = 0x30A2;
constexpr EGLint EGL_CONTEXT_MAJOR_VERSION = 0x3098, EGL_CONTEXT_MINOR_VERSION = 0x30FB;
constexpr EGLint EGL_CONTEXT_OPENGL_PROFILE_MASK = 0x30FD, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT = 0x1;
constexpr EGLint EGL_WIDTH = 0x3057, EGL_HEIGHT = 0x3056, EGL_VENDOR = 0x3053, EGL_VERSION = 0x3054;
constexpr EGLenum EGL_PLATFORM_DEVICE_EXT = 0x313F, EGL_PLATFORM_SURFACELESS_MESA = 0x31DD;

struct Egl {
    void* lib = nullptr;
    void* (*getProcAddress)(const char*) = nullptr;
    EGLDisplay (*getDisplay)(void*) = nullptr;
    EGLBoolean (*initialize)(EGLDisplay, EGLint*, EGLint*) = nullptr;
    EGLBoolean (*terminate)(EGLDisplay) = nullptr;
    EGLBoolean (*chooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*) = nullptr;
    EGLBoolean (*bindApi)(EGLenum) = nullptr;
    EGLContext (*createContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*) = nullptr;
    EGLBoolean (*destroyContext)(EGLDisplay, EGLContext) = nullptr;
    EGLSurface (*createPbuffer)(EGLDisplay, EGLConfig, const EGLint*) = nullptr;
    EGLBoolean (*destroySurface)(EGLDisplay, EGLSurface) = nullptr;
    EGLBoolean (*makeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
    const char* (*queryString)(EGLDisplay, EGLint) = nullptr;
    EGLint (*getError)() = nullptr;
    // Extensions
    EGLBoolean (*queryDevices)(EGLint, EGLDeviceEXT*, EGLint*) = nullptr;
    EGLDisplay (*getPlatformDisplay)(EGLenum, void*, const EGLint*) = nullptr;

    EGLDisplay display = nullptr;
    EGLContext context = nullptr;
    EGLSurface surface = nullptr;
};

Egl g;

template <typename F>
bool load(F& fn, const char* name) {
    fn = reinterpret_cast<F>(dlsym(g.lib, name));
    return fn != nullptr;
}

bool loadEgl() {
    if (g.lib) return true;
    g.lib = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!g.lib) g.lib = dlopen("libEGL.so", RTLD_NOW | RTLD_LOCAL);
    if (!g.lib) {
        std::fprintf(stderr, "headless: libEGL.so.1 not found (install the GPU driver's EGL library)\n");
        return false;
    }
    bool ok = load(g.getProcAddress, "eglGetProcAddress") && load(g.getDisplay, "eglGetDisplay") &&
              load(g.initialize, "eglInitialize") && load(g.terminate, "eglTerminate") &&
              load(g.chooseConfig, "eglChooseConfig") && load(g.bindApi, "eglBindAPI") &&
              load(g.createContext, "eglCreateContext") && load(g.destroyContext, "eglDestroyContext") &&
              load(g.createPbuffer, "eglCreatePbufferSurface") && load(g.destroySurface, "eglDestroySurface") &&
              load(g.makeCurrent, "eglMakeCurrent") && load(g.queryString, "eglQueryString") &&
              load(g.getError, "eglGetError");
    if (!ok) {
        std::fprintf(stderr, "headless: libEGL is missing EGL 1.4 entry points\n");
        return false;
    }
    g.queryDevices = reinterpret_cast<decltype(g.queryDevices)>(g.getProcAddress("eglQueryDevicesEXT"));
    g.getPlatformDisplay = reinterpret_cast<decltype(g.getPlatformDisplay)>(g.getProcAddress("eglGetPlatformDisplayEXT"));
    return true;
}

// Tries to create a GL 3.3 core context on 'display'. On success the context is current.
bool tryDisplay(EGLDisplay display) {
    if (!display) return false;
    EGLint major = 0, minor = 0;
    if (!g.initialize(display, &major, &minor)) return false;
    if (!g.bindApi(EGL_OPENGL_API)) {
        g.terminate(display);
        return false;
    }
    const EGLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24,
                                    EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!g.chooseConfig(display, configAttribs, &config, 1, &count) || count < 1) {
        g.terminate(display);
        return false;
    }
    const EGLint ctxAttribs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = g.createContext(display, config, nullptr, ctxAttribs);
    if (!ctx) {
        g.terminate(display);
        return false;
    }
    // A tiny pbuffer keeps drivers happy that do not support surfaceless contexts; all real
    // rendering goes to framebuffer objects.
    const EGLint pbAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface surface = g.createPbuffer(display, config, pbAttribs);
    if (!g.makeCurrent(display, surface, surface, ctx)) {
        if (surface) g.destroySurface(display, surface);
        g.destroyContext(display, ctx);
        g.terminate(display);
        return false;
    }
    g.display = display;
    g.context = ctx;
    g.surface = surface;
    return true;
}

GLADapiproc eglProc(const char* name) { return reinterpret_cast<GLADapiproc>(g.getProcAddress(name)); }

}  // namespace

bool headlessSupported() { return true; }

// WSL2 exposes the Windows GPUs only through /dev/dxg (no /dev/dri): Mesa's EGL device list then holds just
// llvmpipe, while the surfaceless platform with the d3d12 Gallium driver reaches the real GPU. Prefer the NVIDIA
// adapter (the RTX 4070) over an integrated one; TS_GPU_ADAPTER or the Mesa variables themselves override.
bool preferWslGpu() {
#if defined(__linux__)
    if (!std::getenv("TS_NO_WSL_GPU") && access("/dev/dxg", F_OK) == 0 && access("/dev/dri", F_OK) != 0) {
        const char* adapter = std::getenv("TS_GPU_ADAPTER");
        setenv("GALLIUM_DRIVER", "d3d12", 0);
        setenv("MESA_D3D12_DEFAULT_ADAPTER_NAME", adapter ? adapter : "NVIDIA", 0);
        return true;
    }
#endif
    return false;
}

bool createHeadlessContext(int device, std::string& description) {
    const bool wsl = preferWslGpu();
    if (!loadEgl()) return false;
    if (const char* env = std::getenv("TS_EGL_DEVICE")) device = std::atoi(env);
    bool ok = false;
    if (wsl && g.getPlatformDisplay) {
        ok = tryDisplay(g.getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr));
        if (ok) description = "EGL surfaceless (WSL d3d12)";
    }
    // 1. EGL devices (NVIDIA / Mesa): render on a GPU with no display attached.
    if (!ok && g.queryDevices && g.getPlatformDisplay) {
        EGLDeviceEXT devices[16];
        EGLint n = 0;
        if (g.queryDevices(16, devices, &n)) {
            for (EGLint i = 0; i < n && !ok; ++i) {
                if (device >= 0 && i != device) continue;
                ok = tryDisplay(g.getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr));
                if (ok) description = "EGL device " + std::to_string(i) + " of " + std::to_string(n);
            }
        }
    }
    // 2. Mesa's surfaceless platform, 3. the default display.
    if (!ok && g.getPlatformDisplay) {
        ok = tryDisplay(g.getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr));
        if (ok) description = "EGL surfaceless";
    }
    if (!ok) {
        ok = tryDisplay(g.getDisplay(nullptr));
        if (ok) description = "EGL default display";
    }
    if (!ok) {
        std::fprintf(stderr, "headless: no EGL display could create an OpenGL 3.3 core context (error 0x%x)\n",
                     g.getError ? g.getError() : 0);
        return false;
    }
    if (!gladLoadGL(eglProc)) {
        std::fprintf(stderr, "headless: failed to load OpenGL functions\n");
        return false;
    }
    const char* glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (glRenderer && std::strstr(glRenderer, "llvmpipe"))
        std::fprintf(stderr, "headless: WARNING rendering on the CPU (llvmpipe), not on a GPU: very slow\n");
    const char* vendor = g.queryString(g.display, EGL_VENDOR);
    const char* version = g.queryString(g.display, EGL_VERSION);
    description += std::string(" (") + (vendor ? vendor : "?") + ", EGL " + (version ? version : "?") + ")";
    return true;
}

void destroyHeadlessContext() {
    if (!g.display) return;
    g.makeCurrent(g.display, nullptr, nullptr, nullptr);
    if (g.surface) g.destroySurface(g.display, g.surface);
    if (g.context) g.destroyContext(g.display, g.context);
    g.terminate(g.display);
    g.display = g.context = g.surface = nullptr;
}

#else  // Windows / macOS: a hidden GLFW window is used instead

bool headlessSupported() { return false; }
bool preferWslGpu() { return false; }
bool createHeadlessContext(int, std::string& description) {
    description = "headless EGL is only available on Linux";
    return false;
}
void destroyHeadlessContext() {}

#endif

}  // namespace ts
