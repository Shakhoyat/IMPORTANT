#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rendering/Renderer.h"
#include "scene/Camera.h"
#include "scene/CameraRig.h"
#include "scene/Pov.h"
#include "vision/CvPipeline.h"
#include "audio/AudioEngine.h"
#include "scene/Control.h"
#include "scene/Director.h"
#include "scene/HudMath.h"
#include "scene/SceneApi.h"
#include "scene/Scene.h"
#include "vision/SensorModel.h"
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
    // v6 world options
    uint32_t seed = 0;       // run seed (main.cpp fills it: --seed or random_device ^ clock; 1 for offline modes)
    bool seedGiven = false;
    float hour = -1.0f;      // start hour (--time); < 0 = the mode's default (interactive 16:30, offline 23:00)
    bool autoCycle = true;   // --time cycle (interactive default) vs a fixed manual hour
    bool timeGiven = false;
    float cycleSeconds = 0.0f;  // --cycle-seconds: one full day + night in auto mode (0 = DayCycleConfig default)
    bool garrison = true;    // --no-garrison
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

    // --- v8 player control (AppPlay.cpp): selection, possession, first / third person. Every call blends the camera
    // from the view on screen (spring rig) and remembers it for End.
    std::vector<PovSource> povSources() const;          // every moving object, stable order (= Insert / Delete order)
    bool controlSource(int sourceIndex);                // take control (soldiers: first person) / ride along if autonomous
    bool possessRandomSoldier();                        // take control of a random soldier (outdoors first)
    void setViewThirdPerson(bool third);                // V: third person (spring arm / chase) or first person
    bool viewThirdPerson() const;                       // the current view is a third-person one
    bool controllingSoldier() const;                    // a possessed soldier (open-world movement, jump, aim)
    uint32_t selectedKey() const { return selKey_; }    // PovSource::key of the selection (0 = none)
    void selectKey(uint32_t key) { selKey_ = key; }     // select programmatically (captures, tests)

private:
    // Possession / control (Interactive.cpp, App.cpp).
    void possess(int index);                 // take control of controllables()[index] (-1 = release)
    void possessSlot(Slot slot);             // keys 5 6 7 8 9 0
    void cyclePossession(int dir);           // [ and ]
    void toggleSight();                      // C while possessed: chase <-> first-person sight
    void interact();                         // Enter / Left Ctrl in walk mode: toggle the nearest door / barrier
    void gatherControl(ControlInput& in, const ControlInput*& ip, int& index);  // who gets which input this step
    void syncWalkWorld();                    // WalkSpace + dynamic obstacles into the walk camera
    // Fresh scene with the same seed (cinematic K, recorder); nightStart: warm up at 23:00 (the director opens at night).
    void restartWorld(float warmUpSeconds, bool nightStart = false);
    // Scene(SceneOptions::v6(seed, hour, auto)) from the CLI options; nightStart forces 23:00, manual.
    std::unique_ptr<Scene> makeScene(bool nightStart = false) const;
    void possessNextSoldier();               // F3
    void updateCombatFeed();                 // HUD feed lines from Combat / Scene counters
    void drawPossessHud(const CameraView& cam, int w, int h);   // AppHud.cpp
    void drawCombatFeed(int w, int h);
    void drawClock(int w, int h, float x, float y, float s);    // v6: time of day, sun / moon glyph, seed, sensor line
    void drawPersonBoxes(int w, int h);                         // v6: PERS brackets on the person blobs (thermal FPV)
    // v5 game HUD (AppHud.cpp): enemy markers, UAV tags, top bar, banners, tactical mini-map. 'fpv' = thermal gimbal view
    // (no world markers, the bar sits under the compass); 'inset' = pixels reserved at the top / bottom (letterbox).
    void drawGameHud(const CameraView& cam, int w, int h, bool fpv, float inset = 0.0f);
    const char* possessedName() const;

    // --- v7 game camera (WP-G, AppGame.cpp) ---
    // Every view goes through targetPose() -> CameraRig blend -> FOV kick (composeView). Jumps (F4 picker, Insert /
    // Delete, middle click, Home / End, possession keys) start a blend from the pose on screen; possess() itself never
    // blends, so the scripted captures render exactly as in v6.
    struct ViewMemo {
        bool valid = false;
        Camera cam;                 // camera state (mode, free / walk / orbit parameters)
        int possessed = -1;         // controllable possessed then
        bool sight = false, alt = false;
        uint32_t povKey = 0;        // CameraMode::Pov source
        PovView povView = PovView::First;
        int rover = -1;             // rover the player drove
    };
    void updateGameCamera(float dt);            // after step(): POV follow, impact hold, blend clock, FOV kick, rifle
    RigPose targetPose(float aspect);           // the view the camera wants this frame (before blend and feel)
    CameraView composeView(float aspect);       // targetPose -> rig blend -> FOV kick (v6 views pass through unchanged)
    void beginJump(const RigPose& from);        // blend from 'from' to the current target
    ViewMemo memo() const;
    void restoreMemo(const ViewMemo& m);
    // Ride along with a source (view -1 = its default view). interactive: remember the current view for End and blend.
    bool jumpToPov(uint32_t key, int view = -1, bool interactive = true);
    void noteJump(const ViewMemo& before, const RigPose& from);   // after a possession key: End target + blend
    void cyclePovJump(int dir);                 // Insert / Delete
    void pickPovAt(double nx, double ny);       // middle mouse (cursor in 0..1 of the window, origin top-left)
    void enterFreeCam();                        // Home
    void returnToPrevious();                    // End
    void takeControl();                         // W A S D / arrows while riding along: possess / take the rover's wheel
    void releaseRover();
    void cycleView();                           // C in CameraMode::Pov
    bool currentSource(PovSource& out) const;   // the source the camera rides with / controls (pov or possessed)
    int possessedSoldierIndex() const;          // controllables() index of the possessed soldier, or -1
    bool firstPersonSoldier(int& index) const;  // a soldier's eyes are the camera (possessed sight or POV First)
    void togglePicker();
    void pickerMove(int delta);
    void pickerConfirm();
    bool pickerClick(double nx, double ny);     // cursor in 0..1 of the window
    void refreshPicker();
    void soldierWeapon(float dt);               // possessed soldier: rifle fire (tracers), recoil
    void applyViewmodel(int index, const RigPose& eye);   // first-person rifle + hands in front of the eyes
    void restoreViewmodel();
    void drawPicker(int w, int h);                              // AppHud.cpp
    void drawGameModeHud(const CameraView& cam, int w, int h);  // F11 game HUD
    void drawPovPanel(int w, int h, float y);                   // riding along / controlling label
    void drawAlertVignette(int w, int h, float strength);
    int captureGame();                          // --capture DIR --only game_*: v7 POV figures (AppGame.cpp)
    int capturePlay();                          // --capture DIR --only play_*: v8 select / first / third person (AppPlay.cpp)

    bool initWindow(bool visible);
    Quality autoQuality() const;
    void present();                                        // swap buffers (window) or flush (headless)
    bool closeRequested() const;
    void setSwapInterval(int interval);
    void step(float dt);                                   // simulation + UAV camera + tracker
    // v7 WP-Q: step() = stepSim() + stepVision(). stepSim renders the UAV cameras and starts their read-back;
    // stepVision collects the images (sensor model, CV, squad tracker). The interactive loop and the benchmark
    // submit the main frame in between, so the CPU vision work overlaps the GPU instead of waiting for it.
    void stepSim(float dt);
    void stepVision();
    struct PendingVision {
        bool active = false;
        bool rendered[2] = {false, false};   // per UAV: an image is on its way (not a dropped frame)
        Mat4 reported[2];                    // gimbal pose the measurement is geolocated with
    } pendingVision_;
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
    // v6 sensor: each UAV's microbolometer model (noise, fixed pattern, PSF, jitter, drops), seeded from the run seed.
    std::vector<SensorModel> sensors_;
    std::vector<Measurement2D> uavBlobs_[2];      // person blobs of the latest frame per UAV (PERS brackets in the FPV HUD)
    int framesTotal_[2] = {0, 0}, framesDropped_[2] = {0, 0};
    int soldierCursor_ = -1;                      // F3: last soldier possessed (0..7)
    bool planRoutesOnly_ = false;                 // M overlay without the rover flood field (soldier route figure)
    int axesControllable_ = -1;                   // >= 0: draw the local axes of every joint of this controllable (H)
    audio::AudioEngine audio_;                    // procedural game sound (live playback when a device exists)
    bool audioLive_ = false;
    double audioClock_ = -1.0;
    FrameStats stats_, cvStats_;
    HudEvents hudEvents_;                         // game events -> banners (consumed after every simulation step)

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
    DirectorCut scriptCut_ = DirectorCut::V6; // the captures replay the v5 script
    bool scriptFirePrev_ = false, scriptActionPrev_ = false;
    struct FeedLine { std::string text; float time; };
    std::vector<FeedLine> feed_;              // combat feed (newest last)
    int feedMissiles_ = 0, feedScorches_ = 0, feedKills_ = -1;
    float ammoMax_ = 0.0f;
    double lastX_ = 0, lastY_ = 0;
    double fps_ = 0.0;
    // v7 WP-Q frame pacing (interactive): frame-time history for the HUD graph, and a start-up governor for the
    // auto-detected preset: after a short warm-up it measures the frame rate and steps EPIC -> HIGH -> LOW until the
    // target holds (60 fps; 30 fps through WSLg, where the window copy caps the rate; env TS_TARGET_FPS). F5 = manual.
    static constexpr int kFrameHistory = 120;
    float frameMs_[kFrameHistory] = {};
    int frameHead_ = 0;
    bool qualityAuto_ = false;
    struct QualityGovernor {
        double start = -1.0, measureStart = -1.0, last = -1.0;
        int frames = 0;
        bool done = false;
        float targetFps = 60.0f;
    } governor_;
    void governQuality(double now);
    int screenshotCount_ = 0;

    // v7 game camera state (WP-G)
    CameraRig rig_;
    RigPose shownPose_;                       // last displayed pose (before shake): the start of the next jump
    bool shownValid_ = false;
    float lastAspect_ = 16.0f / 9.0f;
    int lastW_ = 1280, lastH_ = 720;          // framebuffer of the latest frame (picker layout)
    uint32_t povKey_ = 0;                     // CameraMode::Pov: the source ridden with
    PovView povView_ = PovView::First;
    PovSource povSrc_;                        // its latest state (also while holding after an impact)
    float povLookYaw_ = 0.0f, povLookPitch_ = 0.0f;   // free look while riding along (mouse drag)
    ProjectileFollow povRound_;
    float povHold_ = -1.0f;                   // >= 0: s since the followed round / rover ended (camera holds)
    RigPose povHoldPose_;
    bool possessAlt_ = false;                 // possessed tank / UAV on its alternative camera
    int possessedRover_ = -1;                 // intruder slot the player drives
    ViewMemo prevView_;                       // End: back to this view
    FlyCam fly_;
    bool flyCam_ = false;                     // free camera entered with Home (smoothed fly, wheel = speed)
    bool pickerOpen_ = false;
    int pickerSel_ = 0;
    uint32_t pickerKey_ = 0;
    std::vector<PovSource> pickerList_;
    PovSpeedMeter speedMeter_;
    bool gameMode_ = false;                   // F11
    float fovKick_ = 0.0f, letterbox_ = 0.0f, alert_ = 0.0f;
    float hitMarkerT_ = -10.0f, lastShotT_ = -10.0f, fireCooldown_ = 0.0f, recoil_ = 0.0f;
    int crouchWant_ = -1;                     // possessed soldier: stance the player asked for (Left Alt), -1 none
    bool cursorCaptured_ = false;
    // v7 WP-P: a borrowed leaf (hidden head / arm / spare-rifle part re-shaped into a viewmodel part for one frame)
    // also keeps its primitive, material and world matrix for restoreViewmodel.
    struct Hidden {
        SceneNode* node;
        bool visible;
        bool borrowed = false;
        PrimitiveType primitive = PrimitiveType::None;
        Material material{};
        Mat4 world{};
    };
    std::vector<Hidden> viewmodelHidden_;     // nodes whose visibility the first-person view changed this frame

    // --- v8 player control (AppPlay.cpp; builds on the v7.1 WP-T pieces ThirdPersonCam / stepFoot / sphereCast) ---
    // Selection: a left click (no drag) on any moving object selects it, a second click within 0.45 s or Enter takes
    // control; hovering shows its name. Controlling a soldier captures the mouse: first person = the soldier's eyes
    // (A / D strafe, mouse turns), third person (V) = the spring-arm camera (camera-relative W A S D).
    int pickSourceAt(double nx, double ny, std::vector<PovSource>& list) const;   // cursor in 0..1, origin top-left
    bool selectedSource(PovSource& out) const;
    void updateHover(double nx, double ny);
    void selectAt(double nx, double ny);
    void controlSelected();
    bool releaseControl();                      // Esc: leave control / ride along, else clear the selection
    RigPose thirdPersonTarget(float dt);        // advance the spring arm behind the possessed soldier
    void shapePlayerInput(ControlInput& in, float dt);   // possessed soldier: strafe / camera-relative move, jump
    void updateOpenWorld(float dt);             // landing shake, aim blend, third-person arm (once per frame)
    Vec3 aimPoint(const RigPose& eye, float maxDist) const;   // where the crosshair ray hits (solids, ground)
    void gatherNearSolids(const Vec3& centre, float radius);  // tpSolids_ for the arm probe
    float armCast(const Vec3& o, const Vec3& d, float len, float r) const;
    void drawPlayHud(const CameraView& cam, int w, int h);    // selection brackets, prompts, crosshair, key strip
    ThirdPersonCam tp_;
    uint32_t tpKey_ = 0;                        // subject the arm was set up for (re-centred on a change)
    RigPose tpPose_;
    bool tpPoseValid_ = false;
    struct NearBox {                            // oriented box (unit primitive under its world matrix)
        Vec3 c, u[3];
        float h[3] = {0.0f, 0.0f, 0.0f};
        float cast(const Vec3& o, const Vec3& d, float r) const;
    };
    std::vector<NearBox> tpBoxes_;              // near solids of this frame (arm probe)
    float tpFeetY_ = 0.0f;
    bool thirdPerson_ = false;                  // V: the soldier view preference
    bool aiming_ = false;                       // right mouse held (controlling a soldier)
    float aimBlend_ = 0.0f;                     // first-person iron-sight zoom 0..1
    bool jumpEdge_ = false;                     // Space went down (possessed soldier)
    float alignYaw_ = 0.0f;                     // third -> first person: body turn still to apply (degrees)
    float tpLookYaw_ = 0.0f, tpLookPitch_ = 0.0f;   // mouse look for the third-person arm since the last frame
    float feelShake_ = 0.0f;                    // own camera shake (landing)
    bool wasAirborne_ = false;
    uint32_t randSeed_ = 0x51ed2701u;           // possessRandomSoldier
    uint32_t selKey_ = 0, hoverKey_ = 0;        // selected / hovered PovSource::key (0 = none)
    float selClickT_ = -10.0f;                  // time of the last selecting click (double click)
    double pressX_ = 0.0, pressY_ = 0.0;        // left button press position (a click selects, a drag orbits)
    bool pressValid_ = false;
    bool playCapture_ = false;                  // a play_* capture drives the v8 player camera (captures only)
};

}  // namespace ts
