// Headless OpenGL 3.3 core context through EGL (Linux): renders on a GPU without a display or X server,
// e.g. an RTX card on a remote machine reached over SSH. Used by --capture / --record / --benchmark /
// --trace when --headless is given or no display is available. All drawing then goes to off-screen
// framebuffers (there is no default framebuffer). libEGL is loaded at run time, so no EGL headers or
// import libraries are needed to build.
#pragma once

#include <string>

namespace ts {

// Seconds from a monotonic clock (replaces glfwGetTime so headless builds need no GLFW).
double nowSeconds();

// True if a headless EGL context can exist on this platform (Linux).
bool headlessSupported();
// Creates the context, makes it current and loads the GL functions. 'device' = EGL device index
// (-1 = first device that works; environment variable TS_EGL_DEVICE overrides). Fills 'description'.
bool createHeadlessContext(int device, std::string& description);
void destroyHeadlessContext();

// True when the process has no display to open a window on (Linux without DISPLAY / WAYLAND_DISPLAY).
bool noDisplayAvailable();

}  // namespace ts
