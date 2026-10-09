// CPU-only helpers of the game HUD (no OpenGL): marker projection, screen-edge arrows, banner timing and the
// event -> banner queue, tactical mini-map mapping. Kept out of src/app so tests/test_app.cpp can check them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/Math3D.h"
#include "scene/GameEvents.h"

namespace ts {

// --- marker projection -------------------------------------------------------------------------------------------------
struct MarkerProjection {
    bool front = false;     // in front of the camera (clip w > 0)
    bool onScreen = false;  // front and inside the screen rectangle
    float x = 0, y = 0;     // pixels, origin top-left (for points behind the camera: the mirrored position)
    float dist = 0;         // metres from the camera
};
MarkerProjection projectMarker(const Mat4& viewProj, const Vec3& camPos, const Vec3& p, int w, int h);

// Half-size in pixels of a bracket box that frames an object 'worldSize' metres across at 'dist' metres; scales
// with the screen height / 900 and is clamped to [10, 120] * h / 900.
float markerHalfSize(float dist, int h, float worldSize = 0.55f, float fovYDeg = 50.0f);

// --- off-screen arrows ----------------------------------------------------------------------------------------------------
struct EdgeArrow {
    float x = 0, y = 0;        // arrow tip on the screen border (inset by margin)
    float dirX = 1, dirY = 0;  // unit direction the arrow points to (from the centre outwards)
};
// 'sx, sy' come from projectMarker; a point behind the camera points the opposite way (the projection is mirrored).
EdgeArrow edgeArrow(float sx, float sy, bool front, int w, int h, float margin);

// --- banners / event queue ------------------------------------------------------------------------------------------------
enum class BannerKind { Wave, Spotted, Kill, Hit, Breach, BaseLost, Strike,
                        // v6 day / night phases and the dawn withdrawal
                        Dusk, NightFalls, Dawn, DayBreak, Shift, Withdraw };
// Headline of a v6 phase banner ("DUSK", "NIGHT FALLS", ...), nullptr for the v5 kinds.
const char* phaseBannerText(BannerKind k);
// Second line of a v6 phase banner (what changes in the camp), nullptr for the v5 kinds.
const char* phaseBannerDetail(BannerKind k);

struct Banner {
    BannerKind kind = BannerKind::Wave;
    float start = 0.0f;       // scene time
    float duration = 2.0f;    // s
    int value = 0;            // wave number / score / integrity lost / health left
    bool combined = false;
    int track = -1;
    Vec3 position;            // world anchor (Kill, Hit, Breach)
    bool hasPosition = false;
};

float bannerDuration(BannerKind k);
// Opacity of a banner of age 'age': linear fade-in over fadeIn, hold, linear fade-out over the last fadeOut seconds.
float bannerAlpha(float age, float duration, float fadeIn = 0.15f, float fadeOut = 0.5f);
// Upward drift in 0..1 over the banner life (score pop-ups rise).
float bannerRise(float age, float duration);

class HudEvents {
public:
    // Turns the events of one simulation step into banners. Call after every Scene::update ('time' = scene time).
    void consume(const std::vector<GameEvent>& events, float time);
    // Drops expired banners; a clock that jumped back (new scene) clears everything.
    void prune(float time);
    void reset() { banners_.clear(); lastTime_ = -1.0f; edgeFlashStart_ = baseLostStart_ = lastSpotted_ = -1e9f; }
    const std::vector<Banner>& banners() const { return banners_; }
    // Red screen-edge flash of a breach: 1 -> 0 over 0.9 s.
    float breachFlash(float time) const;
    // Full-screen dim of BASE LOST: ramps in, holds, fades out (0..0.65).
    float baseLostDim(float time) const;
    // Text of the BASE LOST banner at 'time' ("BASE LOST", then "RESTARTING"), nullptr when none is active.
    const char* baseLostText(float time) const;
    // Newest banner of a kind, or nullptr.
    const Banner* latest(BannerKind k) const;

private:
    std::vector<Banner> banners_;
    float lastTime_ = -1.0f;
    float edgeFlashStart_ = -1e9f;
    float baseLostStart_ = -1e9f;
    float lastSpotted_ = -1e9f;  // ENEMY SPOTTED is shown at most every 6 s
};

// --- v6 clock / time of day ---------------------------------------------------------------------------------------------
enum class SkyGlyph { Sun, LowSun, Moon };
// Glyph of the HUD clock: sun above +6 deg, low (orange, half) sun within +-6 deg of the horizon, moon below -6 deg.
SkyGlyph skyGlyph(float sunElevationDeg);
// "HH:MM" of a clock hour (wraps into 0..24, minutes rounded down).
std::string formatClock(float hour);
// --time value: day (12:00), night (23:00), dusk (17:45), dawn (06:00) and HH:MM set a fixed manual hour; cycle
// runs the automatic day/night cycle from the mode's default start (hour = -1). False for anything else.
bool parseTimeOfDay(const std::string& value, float& hour, bool& autoCycle);
// Default run seed from a random_device value and a clock reading (never 0: seed 0 means the v5 legacy run).
uint32_t mixRunSeed(uint32_t randomDevice, uint64_t clockTicks);

// --- v6 thermal FPV overlays ----------------------------------------------------------------------------------------------
// Pixel box of a blob in the 320 x 240 CV image -> screen rectangle of the full-screen first-person thermal view, which
// shows the same gimbal camera with a vertical field of view 'viewFovYDeg' (the CV image has cvFovYDeg, aspect w/h).
struct ScreenBox { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
ScreenBox cvBoxToScreen(int minX, int minY, int maxX, int maxY, int imgW, int imgH, float cvFovYDeg, float viewFovYDeg,
                        int w, int h);

// --- controls (F1 help overlay, report appendix B, workbook) -------------------------------------------------------------
struct HelpLine {
    int column;        // 0 = view & camera, 1 = control
    const char* text;  // first line of each column is its heading
};
const std::vector<HelpLine>& helpLines();
struct KeyBinding {
    const char* keys;
    const char* action;
    const char* version;  // "v6" for the day/night and soldier keys
};
const std::vector<KeyBinding>& keyBindingsV6();
const std::vector<KeyBinding>& keyBindingsV7();   // v7 game camera keys (WP-G)

// --- v7.1 WP-U: field guide (G), both UAV feeds, context prompts ----------------------------------------------------------
// All layouts are in pixels (origin top-left) and scale with the screen: s = min(w / 1600, h / 900) for the guide (a
// design canvas of 1600 x 900), h / 900 for the HUD (like every other HUD element).
constexpr float kHudGlyphAspect = 31.0f / 59.0f;   // advance / pixel height of the baked HUD font (DejaVu Sans Mono Bold)
inline float hudTextWidth(size_t chars, float px) { return static_cast<float>(chars) * px * kHudGlyphAspect; }

struct UiRect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    bool overlaps(const UiRect& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
    bool within(const UiRect& o) const { return x >= o.x - 1e-3f && y >= o.y - 1e-3f && right() <= o.right() + 1e-3f && bottom() <= o.bottom() + 1e-3f; }
};

enum class GuideSection { Travel, Time, View };
enum class GuideAction {
    None = 0,
    // TRAVEL (keys 1-9)
    RideUav1, RideUav2, SoldierFirst, SoldierThird, PickPov, FreeCam, Tank, Helicopter, Rover,
    // TIME (Q W E R presets, the slider, T auto / manual, Y cycle speed, O day ops (v9 M5); Left / Right nudge the slider)
    Dawn, Noon, Dusk, Midnight, TimeSlider, CycleAuto, CycleSpeed, DayOps,
    // VIEW (A S D F H U)
    PersonToggle, RenderMode, FrameExplorer, Thermal, HudToggle, Feeds,
    Close,
};
const char* guideActionName(GuideAction a);   // "RIDE_UAV1", ... (logs, tests)
const char* guideActionKey(GuideAction a);    // shortcut shown on the button's key cap ("1", "Q", "A", ...), "" for none
// Shortcut while the guide is open: 'key' = the GLFW key code, which equals the ASCII code for digits and capital letters.
GuideAction guideKeyAction(int key);
// Preset hour of a TIME button (dawn 06:00, noon 12:00, dusk 17:45, midnight 00:00), -1 for the other actions.
float guidePresetHour(GuideAction a);

struct GuideButton {
    GuideAction action = GuideAction::None;
    GuideSection section = GuideSection::Travel;
    UiRect r;
    const char* label = "";
    const char* sub = "";   // second line (travel tiles)
};
struct GuideLayout {
    float s = 1.0f;                // pixels per design unit
    UiRect panel, header, close;
    UiRect travelBox, timeBox, viewBox, helpBox;   // section frames (titles sit inside, at the top)
    UiRect clock;                  // big clock read-out of the TIME section
    UiRect slider;                 // time-of-day track: hour 0 at the left end, 24 at the right
    UiRect sliderHit;              // taller click / drag area around the track
    std::vector<GuideButton> buttons;
};
GuideLayout guideLayout(int w, int h);
int guideButtonAt(const GuideLayout& l, float x, float y);              // index in buttons, -1
GuideAction guideActionAt(const GuideLayout& l, float x, float y);      // button, TimeSlider on the track, Close, None
float guideSliderHour(const GuideLayout& l, float x);                   // [0, 24): the left end is 00:00
float guideSliderX(const GuideLayout& l, float hour);
// Clock speed presets of the auto cycle (Y cycles them): x0.5, x1, x2, x4, x8. Index wraps.
constexpr int kGuideSpeedCount = 5;
float guideSpeedFactor(int index);
// Opacity of the first-run hint "PRESS G FOR THE FIELD GUIDE": fades in after 1.5 s, holds, fades out at 14 s (or
// 0.4 s after the guide was first opened, openedAt >= 0).
float guideHintAlpha(float t, float openedAt = -1.0f);

// HUD regions of the default / game-mode HUD (AppHud draws them here; the tests check they never overlap).
struct HudLayout {
    float s = 1.0f;                 // h / 900
    UiRect topBar;                  // wave / score / integrity panel (centred, 700 s wide)
    UiRect compass;                 // game mode compass strip under the top bar (empty otherwise)
    UiRect cornerTop;               // title + settings rows (top left)
    UiRect cornerBottom;            // UAV / rover / frame-rate rows (bottom left)
    UiRect clock, miniMap;          // bottom right
    UiRect feed[2], feedImage[2];   // both UAV feeds (cards; the 4:3 image inside); empty when the feeds are off
    UiRect paletteBar;              // thermal palette scale (external thermal view)
    UiRect combatFeed;              // area of the combat feed lines (right-aligned)
    UiRect prompts;                 // area for the context prompt chips (bottom centre)
};
HudLayout hudLayout(int w, int h, bool feeds, bool gameMode);
// Title line of a UAV feed card: "UAV-1  PATROL" / "UAV-2  DOCKED  //  DAY SHIFT".
std::string uavFeedTitle(int uav, const char* dutyState, bool airborne);

// Context prompts ("[G] GUIDE  [F4] ANY POV ..."): chips for the current mode, most important first.
enum class PromptMode { Overview, Walk, FreeCam, RideSoldier, RideVehicle, RideUav, RideRound, Soldier, Vehicle, Rover, ThermalFpv };
struct PromptChip {
    const char* key;
    const char* label;
};
std::vector<PromptChip> contextPrompts(PromptMode m, bool thirdPerson);
struct PromptLayout {
    UiRect bar;                 // backing strip around the shown chips
    std::vector<UiRect> chips;  // one per shown chip (the first chips.size() prompts)
    std::vector<float> keyW;    // key-cap width per chip
    float px = 12.0f;           // text size
};
// Lays the chips out in one row inside 'area', centred on centreX when it fits (< 0: the area's centre), and drops
// the last ones that do not fit.
PromptLayout layoutPrompts(const std::vector<PromptChip>& chips, const UiRect& area, float s, float centreX = -1.0f);

// --- tactical mini-map ----------------------------------------------------------------------------------------------------
// North-up like the plan_topdown capture: world +x to the right, world +z down the screen.
struct MiniMapGeom {
    float cx = 0, cy = 0;       // centre in pixels
    float radius = 90.0f;       // pixels of the map circle
    float worldRadius = 40.0f;  // metres shown at the rim
};
MiniMapGeom miniMapGeometry(int w, int h);  // bottom-right, 180 px across at h = 900
// World (x, z) -> pixels. Points beyond the rim are pulled onto it when 'clampToRim'; returns true if inside.
bool miniMapToScreen(const MiniMapGeom& g, float wx, float wz, float& sx, float& sy, bool clampToRim = true);
// Screen direction (unit) of a heading yawRad where yaw 0 = +z (the sim's convention: forward = (sin yaw, cos yaw)).
void miniMapHeading(float yawRad, float& dx, float& dy);

// Colour ramp of the base integrity bar: green (100 %) -> amber (50 %) -> red (<= 25 %), r g b in 0..1.
void integrityColor(float percent, float& r, float& g, float& b);

}  // namespace ts
