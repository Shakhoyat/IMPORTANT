#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rendering/Renderer.h"
#include "scene/Camera.h"
#include "scene/Scene.h"
#include "vision/ThermalTracker.h"

struct GLFWwindow;

namespace ts {

struct AppOptions {
    enum class Mode { Interactive, Capture, Benchmark, Trace, Record } mode = Mode::Interactive;
    std::string outPath;  // capture directory or benchmark JSON
    int width = 1600, height = 900;
    float seconds = 180.0f;  // trace length
    std::string only;        // capture: comma-separated shot names (empty = all)
    std::string ffmpeg = "ffmpeg";  // record: encoder executable
};

class App {
public:
    explicit App(AppOptions options) : opt_(std::move(options)) {}
    ~App();
    int run();

    // GLFW callbacks forward here.
    void onKey(int key, int action, int mods);
    void onMouseButton(int button, int action);
    void onCursor(double x, double y);
    void onScroll(double dy);

private:
    bool initWindow(bool visible);
    void step(float dt);                                   // simulation + UAV camera + tracker
    void renderFrame(int w, int h, bool withHud);          // main view + overlays + HUD
    void drawOverlays(const CameraView& cam);              // hierarchy axes, MicroMouse plan
    void drawHud(const CameraView& cam, int w, int h);     // AppHud.cpp
    void drawTargetMarker(const CameraView& cam, int w, int h);
    void drawUavFeed(int w, int h);
    void drawHelp(int w, int h);
    void interactiveLoop();
    void saveScreenshot(int w, int h);
    int captureMode();                                     // Capture.cpp
    int benchmarkMode();
    int traceMode();
    int recordMode();                                      // Record.cpp
    void applyCinematic();                                 // view mode / fade / letterbox for the director
    void drawCinematicCaption(int w, int h);
    void updateTitle(double fps);

    AppOptions opt_;
    GLFWwindow* window_ = nullptr;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Renderer> renderer_;
    Camera camera_;
    RenderSettings settings_;
    ThermalTracker tracker_;
    Detection detection_;
    std::vector<uint8_t> uavImage_;
    FrameStats stats_, cvStats_;

    float rainTarget_ = 0.0f;  // R toggles; rain intensity eases towards it, wetness follows slowly
    bool paused_ = false, showHud_ = true, showFeed_ = true, showHelp_ = false, exploded_ = false;
    bool dragging_ = false;
    double lastX_ = 0, lastY_ = 0;
    double fps_ = 0.0;
    int screenshotCount_ = 0;
};

}  // namespace ts
