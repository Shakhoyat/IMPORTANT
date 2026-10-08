// v7 WP-I Frame Explorer: a modal view (key `) that freezes the current frame and replays how it is made, stage by
// stage (math/ExplorerMath.h lists the 16 stages). On open() the frame is rendered once more by the real Renderer
// (plus a few switch variants: SSR / SSAO / fog / TAA off, Blinn-Phong, thermal), its internal buffers are copied
// (Renderer::labSnapshot) and the explorer draws its own didactic passes over the same scene: G-buffer, triangle IDs,
// barycentrics, affine vs perspective-correct checker, Gouraud vs Phong, overdraw, back faces, a bird's-eye view with
// the view frustum. Every stage then has a few image layers plus a drawn overlay (caption, key formula, diagrams).
//
// Inside the explorer:  Left / Right stage, Up / Down layer, 1-9 / 0 jump to stage 1-10, Home / End first / last,
//   W wipe to the previous stage (drag with the left mouse button), M magnifier (wheel = zoom), I pixel inspector,
//   P image-processing lab (K kernel, [ ] gamma, S sampling factor), Space pause / play the animations, R restart them,
//   left click = probe this pixel (raster / vertex / reflection stages follow it), Tab hide the text, ` or Esc close.
#pragma once

#include <glad/gl.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "math/ExplorerMath.h"
#include "rendering/Renderer.h"
#include "scene/Scene.h"

namespace ts {

class FrameExplorer {
public:
    // Key codes = GLFW's, so the window layer forwards them unchanged.
    enum Key {
        KeySpace = 32, KeyZero = 48, KeyNine = 57, KeyA = 65, KeyB = 66, KeyD = 68, KeyG = 71, KeyH = 72, KeyI = 73,
        KeyK = 75, KeyM = 77, KeyP = 80, KeyR = 82, KeyS = 83, KeyW = 87, KeyLeftBracket = 91, KeyRightBracket = 93,
        KeyGrave = 96, KeyEscape = 256, KeyEnter = 257, KeyTab = 258, KeyRight = 262, KeyLeft = 263, KeyDown = 264,
        KeyUp = 265, KeyPageUp = 266, KeyPageDown = 267, KeyHome = 268, KeyEnd = 269
    };

    // What the overlay shows (scripted by the captures; toggled by the keys in the interactive mode).
    struct ViewState {
        int stage = 0, layer = 0;
        bool captions = true;
        bool magnifier = false;
        float zoom = 8.0f;           // magnifier: screen pixels per image pixel
        bool inspector = false;
        bool wipe = false;           // split-screen with the previous stage
        float wipeX = 0.5f;          // split position (0..1 of the width)
        float split = -1.0f;         // a | b layers: split position (< 0 = the layer's default)
        bool lab = false;            // image-processing panel
        int kernel = 0;              // fx::KernelType
        float gamma = 1.0f;
        int sampleFactor = 6;        // sampling demo: minification factor
        bool playing = true;
        bool tag = true;             // clean view (captions off): the small stage tag top-left
        float cursorX = -1.0f, cursorY = -1.0f;   // framebuffer pixels (< 0 = none)
    };

    FrameExplorer();
    ~FrameExplorer();
    FrameExplorer(const FrameExplorer&) = delete;
    FrameExplorer& operator=(const FrameExplorer&) = delete;

    // Freezes the frame: renders it (and its variants) through 'r' at w x h and builds every stage image.
    // 'settings' = the live settings (forced to the visible HDR pipeline). Returns false if the GPU work failed.
    bool open(Renderer& r, const Scene& scene, const CameraView& cam, const RenderSettings& settings, int w, int h);
    void close();
    bool isOpen() const { return open_; }
    // open() takes seconds: it reports its steps (fraction 0..1, what it is doing) to this callback, which can draw
    // drawProgress() into the window and present it. Not called when empty (captures).
    using Progress = std::function<void(float fraction, const std::string& label)>;
    void setProgress(Progress p) { progress_ = std::move(p); }
    void drawProgress(Renderer& r, GLuint targetFbo, int w, int h, float fraction, const std::string& label);
    // TAA convergence of the frozen frame: 'frames' renders; keepHistory = continue the live view's TAA history
    // (interactive, visible view, same camera: 3 frames give the converged image) instead of starting from scratch (8).
    void setConvergence(int frames, bool keepHistory) {
        convergeFrames_ = frames;
        keepHistory_ = keepHistory;
    }

    // Advances the animations and draws the explorer into 'targetFbo' (w x h; the window may have been resized).
    void frame(Renderer& r, GLuint targetFbo, int w, int h, float dt);
    void update(float dt);
    void draw(Renderer& r, GLuint targetFbo, int w, int h);

    // Input (framebuffer pixel coordinates, origin top-left). onKey returns false for keys it does not use.
    bool onKey(int key);
    void onCursor(float x, float y);
    void onMouseButton(int button, bool down);
    void onScroll(float dy);

    // Scripted control (captures, tests).
    ViewState& view() { return view_; }
    void setStage(int s);
    int layerCount(int stage) const;
    const char* layerName(int stage, int layer) const;
    void setProbe(int px, int py);          // snapshot pixel the raster / vertex / reflection stages explain
    void setAnimation(float seconds);       // animation clock (scanline reveal, SSR march), paused
    void setTransition(float t01) { trans_ = t01; }
    float rasterProbeTime() const;          // animation time at which the raster scan reaches the probe pixel's cell
    const fx::FrameSnapshot& snapshot() const { return snap_; }
    double openSeconds() const { return openSeconds_; }

    struct Impl;  // GPU state + stage images (FrameExplorer.cpp)

private:
    std::unique_ptr<Impl> impl_;
    fx::FrameSnapshot snap_;
    ViewState view_;
    bool open_ = false;
    bool dragging_ = false;
    float anim_ = 0.0f;       // seconds since the stage was entered (animations)
    float trans_ = 1.0f;      // stage transition 0..1
    int prevStage_ = 0;
    double openSeconds_ = 0.0;
    float lastW_ = 0.0f, lastH_ = 0.0f;  // size of the last draw (mouse mapping)
    Progress progress_;
    int convergeFrames_ = 8;
    bool keepHistory_ = false;
};

}  // namespace ts
