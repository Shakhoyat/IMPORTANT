#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rendering/Renderer.h"
#include "scene/Camera.h"
#include "vision/CvPipeline.h"
#include "audio/AudioEngine.h"
#include "scene/Control.h"
#include "scene/SceneApi.h"
#include "scene/Scene.h"
#include "vision/ThermalTracker.h"

struct GLFWwindow;

namespace ts {

struct AppOptions {
    enum class Mode { Interactive, Capture, Benchmark, Trace, Record } mode = Mode::Interactive;
    std::string outPath;  // capture directory or benchmark JSON
    int width = 1600, height = 900;
    bool sizeGiven = false;  // --size WxH overrides the per-mode / per-shot resolution
    float seconds = 180.0f;  // trace length
    std::string only;        // capture: comma-separated shot names (empty = all)
    std::string ffmpeg = "ffmpeg";  // record: encoder executable
    std::string encoder = "libx264";  // record: ffmpeg video codec (h264_nvenc on an NVIDIA GPU)
    int fps = 30;                     // record: video frame rate (30 or 60)
    int quality = -1;        // --quality low|high|ultra|epic (-1 = auto-detect from the GPU)
    bool headless = false;   // --headless: EGL context without a window (Linux, remote GPU over SSH)
    int device = -1;         // --gpu N: EGL device index for headless rendering
    bool fullscreen = false; // --fullscreen (interactive)
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
    // Possession / control (Interactive.cpp, App.cpp).
    void possess(int index);                 // take control of controllables()[index] (-1 = release)
    void possessSlot(Slot slot);             // keys 5 6 7 8 9 0
    void cyclePossession(int dir);           // [ and ]
    void toggleSight();                      // C while possessed: chase <-> first-person sight
    void interact();                         // Enter / Left Ctrl in walk mode: toggle the nearest door / barrier
    void gatherControl(ControlInput& in, const ControlInput*& ip, int& index);  // who gets which input this step
    void syncWalkWorld();                    // WalkSpace + dynamic obstacles into the walk camera
    void forceStrike();                      // UAV fires a missile at the primary rover (director / capture)
    void restartWorld(float warmUpSeconds);  // fresh deterministic scene (cinematic K, recorder)
    void updateCombatFeed();                 // HUD feed lines from Combat / Scene counters
    void drawPossessHud(const CameraView& cam, int w, int h);   // AppHud.cpp
    void drawCombatFeed(int w, int h);
    const char* possessedName() const;

    bool initWindow(bool visible);
    Quality autoQuality() const;
    void present();                                        // swap buffers (window) or flush (headless)
    bool closeRequested() const;
    void setSwapInterval(int interval);
    void step(float dt);                                   // simulation + UAV camera + tracker
    void renderFrame(int w, int h, bool withHud);          // main view + overlays + HUD
    void drawOverlays(const CameraView& cam);              // hierarchy axes, MicroMouse plan
    void drawHud(const CameraView& cam, int w, int h);     // AppHud.cpp
    void drawTargetMarker(const CameraView& cam, int w, int h);
    void drawUavFeed(int w, int h);
    void drawHelp(int w, int h);
    // First-person thermal (FLIR) view: military-style HUD over the full-screen gimbal camera (AppHud.cpp).
    void drawThermalFpvHud(const CameraView& cam, int w, int h);
    bool thermalFirstPerson() const;  // thermal view through the UAV camera (not the cinematic director)
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
    bool headless_ = false;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Renderer> renderer_;
    Camera camera_;
    RenderSettings settings_;
    ThermalTracker tracker_;
    Detection detection_;
    std::vector<uint8_t> uavImage_, uavImage2_;   // thermal images of UAV 0 / UAV 1 (320 x 240)
    CvFrameStats cvFrame_[2];                     // CV statistics of the latest frame per UAV
    audio::AudioEngine audio_;                    // procedural game sound (live playback when a device exists)
    bool audioLive_ = false;
    double audioClock_ = -1.0;
    FrameStats stats_, cvStats_;

    bool thermalFpv_ = true;         // thermal view = first-person gimbal camera, full screen (Y toggles)
    float thermalFovDeg_ = 40.0f;    // display zoom (mouse wheel in thermal FPV): 40 WFOV, 20 MFOV, 8 NFOV
    float nucTimer_ = 30.0f;         // seconds until the next sensor NUC (shutter closes for ~0.4 s)
    float rainTarget_ = 0.0f;  // R toggles; rain intensity eases towards it, wetness follows slowly
    bool paused_ = false, showHud_ = true, showFeed_ = true, showHelp_ = false, exploded_ = false;
    bool dragging_ = false;
    // Possession state. possessed_ indexes sa::controllables(*scene_); possessedSlot_ is its Slot (or -1).
    int possessed_ = -1, possessedSlot_ = -1;
    CameraMode modeBeforePossess_ = CameraMode::Orbit;
    ControlInput held_;                       // axes / held buttons sampled by the loop each frame
    bool firePressedEdge_ = false, actionEdge_ = false;
    float aimYawAcc_ = 0.0f, aimPitchAcc_ = 0.0f;   // mouse look since the last step, degrees
    int interactIdx_ = -1;                    // door / barrier that gets one frame of actionPressed
    float scriptClock_ = -1.0f;               // >= 0: seconds on the director's script clock (video, cinematic K, capture)
    bool scriptFirePrev_ = false, scriptActionPrev_ = false, scriptStrikePrev_ = false;
    float strikeTimer_ = -1.0f;               // > 0: keep the laser designator on the rover for the missile
    struct FeedLine { std::string text; float time; };
    std::vector<FeedLine> feed_;              // combat feed (newest last)
    int feedMissiles_ = 0, feedScorches_ = 0, feedKills_ = -1;
    float ammoMax_ = 0.0f;
    double lastX_ = 0, lastY_ = 0;
    double fps_ = 0.0;
    int screenshotCount_ = 0;
};

}  // namespace ts
