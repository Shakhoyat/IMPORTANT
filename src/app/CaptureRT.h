// v7 WP-H report figures for ray tracing (raytrace_*), rendered by the real Renderer on the GPU:
//   raytrace_modes      the same view in the five modes (raster, hybrid, Whitted, path traced, light traced) as a
//                       labelled montage + one full-size image per mode (raytrace_mode_<name>.png)
//   raytrace_tree       ray debugger: the Whitted ray tree of one pixel on the glass ball, seen from the side
//   raytrace_ssr        SSR (raster) vs ray-traced reflections (hybrid) on wet ground, side by side + crops
//   raytrace_photons    photon map (CPU, from the sun / moon / searchlight / lamps): direct, indirect and caustic
//                       photons drawn over the raster image
//   raytrace_perf       ms per frame of every mode at the capture size (raytrace_perf.json + the log)
// A few showcase props (glass ball, mirror ball, glass block, chrome post) are added to the scene's leaves while the
// figures render (both raster and ray tracer see them), then removed. Driven by Capture.cpp (agent CAP) through
// rtcap::figure(ctx, name); every shot name above is accepted.
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "rendering/Renderer.h"
#include "scene/Scene.h"

namespace ts::rtcap {

struct Ctx {
    Scene* scene = nullptr;
    Renderer* renderer = nullptr;
    std::string dir;             // output directory
    int width = 1280, height = 720;
    Quality quality = Quality::Epic;
    float hour = -1.0f;          // >= 0: set the clock first (manual), e.g. 16.5 for long sun shadows
    std::ostream* log = nullptr;
};

// The shot names this module renders ("raytrace_modes", ...).
const std::vector<std::string>& shotNames();
// Renders one figure; false if the name is unknown or the GPU path failed.
bool figure(Ctx& c, const std::string& shot);

}  // namespace ts::rtcap
