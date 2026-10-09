// v9 M2: player settings (mouse sensitivity, invert Y, field of view, vsync, frame limit, head bob, quality), their
// ThermalSense.ini file (plain key = value lines next to the exe) and the pause menu layout: a tab bar (Resume / Travel
// / Time / View / Settings / Quit) over the v7.1 field guide panel (HudMath guideLayout: Travel, Time and View pages)
// or the settings rows. Pure CPU (no GL, no GLFW): parse / serialize, value steps and hit tests are unit-tested.
#pragma once

#include <string>
#include <vector>

#include "scene/HudMath.h"

namespace ts {

struct GameSettings {
    float mouseSensitivity = 1.0f;   // multiplier on every mouse-look path (0.1 .. 3.0)
    bool invertY = false;            // mouse up looks down
    float fovDeg = 65.0f;            // on-foot vertical field of view (first person; third person = fov - 3), 50 .. 95
    bool vsync = false;              // swap interval 1 (the frame limiter is then off)
    float fpsLimit = -1.0f;          // frame limiter: -1 = display refresh, 0 = uncapped, else frames per second
    bool headBob = true;             // camera bob while walking (read by the soldier camera)
    int quality = -1;                // -1 = auto-detect, 0 LOW, 1 HIGH, 2 ULTRA, 3 EPIC
    bool operator==(const GameSettings& o) const;
    bool operator!=(const GameSettings& o) const { return !(*this == o); }
};

// Limits and steps (shared by the menu and the parser's clamping).
constexpr float kSensMin = 0.1f, kSensMax = 3.0f, kSensStep = 0.1f;
constexpr float kFovMin = 50.0f, kFovMax = 95.0f, kFovStep = 5.0f;
// Frame-limit choices of the menu (fps; -1 = display, 0 = uncapped).
const std::vector<float>& frameLimitChoices();

// key = value lines, '#' comments, a header line. Floats with 2 decimals; booleans 0 / 1; fps_limit and quality as words
// ("auto", "uncapped", "low", ...) or numbers.
std::string serializeSettings(const GameSettings& s);
// Unknown keys and bad values are skipped (each adds a warning naming the line); values are clamped to their limits.
GameSettings parseSettings(const std::string& text, std::vector<std::string>* warnings = nullptr);

// Mouse look delta after the player's settings (sensitivity on both axes, invert Y flips the vertical one).
void applyLookSettings(const GameSettings& s, float& dx, float& dy);

// --- settings rows -------------------------------------------------------------------------------------------------------
enum class SettingId { Sensitivity, InvertY, Fov, Vsync, FrameLimit, HeadBob, Quality, Count };
constexpr int kSettingCount = static_cast<int>(SettingId::Count);
const char* settingLabel(SettingId id);    // "MOUSE SENSITIVITY", ...
const char* settingHint(SettingId id);     // one line under the label
std::string settingValueText(const GameSettings& s, SettingId id);   // "1.00x", "ON", "65 DEG", "AUTO (DISPLAY)", ...
// Left (dir -1) / right (+1): steps clamp at the ends, on / off rows toggle, lists step through their choices.
void adjustSetting(GameSettings& s, SettingId id, int dir);

// --- pause menu ------------------------------------------------------------------------------------------------------------
enum class MenuTab { Resume = 0, Travel, Time, View, Settings, Quit, Count };
constexpr int kMenuTabCount = static_cast<int>(MenuTab::Count);
const char* menuTabLabel(MenuTab t);
bool menuTabIsPage(MenuTab t);             // Travel / Time / View / Settings (Resume and Quit are actions)
MenuTab nextMenuPage(MenuTab t, int dir);  // Tab / Shift+Tab: Travel -> Time -> View -> Settings -> Travel
GuideSection menuTabSection(MenuTab t);    // Travel / Time / View pages highlight that guide section

struct SettingRowRect {
    SettingId id = SettingId::Sensitivity;
    UiRect row, minus, value, plus;
};
struct PauseMenuLayout {
    float s = 1.0f;
    GuideLayout guide;                 // the field guide panel (Travel / Time / View pages)
    UiRect tabs[kMenuTabCount];        // tab bar above the panel
    UiRect page;                       // = guide.panel (the settings page uses the same frame)
    UiRect settingsTitle, settingsFooter;
    std::vector<SettingRowRect> rows;  // one per SettingId, in order
};
PauseMenuLayout pauseMenuLayout(int w, int h);
int menuTabAt(const PauseMenuLayout& l, float x, float y);   // MenuTab index or -1
struct SettingHit {
    int row = -1;   // SettingId index, -1 = none
    int dir = 0;    // -1 minus button, +1 plus button (or the value box: next value), 0 the row label
};
SettingHit settingAt(const PauseMenuLayout& l, float x, float y);

}  // namespace ts
