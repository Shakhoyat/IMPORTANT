#include "scene/KeyMap.h"

#include <cstdio>

namespace ts {

namespace {
// GLFW key codes (kept here so the table needs no GLFW header).
constexpr int kSpace = 32, kApostrophe = 39, kComma = 44, kMinus = 45, kPeriod = 46, kSlash = 47, kSemicolon = 59,
              kEqual = 61, kLeftBracket = 91, kBackslash = 92, kRightBracket = 93, kGrave = 96, kEsc = 256, kEnter = 257,
              kTab = 258, kBackspace = 259, kInsert = 260, kDelete = 261, kRight = 262, kLeft = 263, kUp = 265,
              kPageUp = 266, kHome = 268, kEnd = 269, kPause = 284, kF1 = 290, kLeftShift = 340,
              kLeftCtrl = 341, kLeftAlt = 342;
constexpr int F(int n) { return kF1 + n - 1; }
constexpr int K(char c) { return static_cast<int>(c); }   // letters (upper case) and digits are their ASCII codes
}  // namespace

const char* keyCtxName(KeyCtx c) {
    switch (c) {
        case KeyCtx::Any: return "ANY";
        case KeyCtx::Overview: return "OVERVIEW";
        case KeyCtx::Walk: return "WALK";
        case KeyCtx::Soldier: return "SOLDIER";
        case KeyCtx::Vehicle: return "VEHICLE";
        case KeyCtx::Ride: return "RIDE";
        case KeyCtx::Menu: return "MENU";
        case KeyCtx::Picker: return "PICKER";
    }
    return "?";
}

bool keyContextsOverlap(KeyCtx a, KeyCtx b) {
    if (a == b) return true;
    const auto modal = [](KeyCtx c) { return c == KeyCtx::Menu || c == KeyCtx::Picker; };
    if (modal(a) || modal(b)) return false;
    return a == KeyCtx::Any || b == KeyCtx::Any;
}

const std::vector<KeyBind>& keyMapV9() {
    using C = KeyCtx;
    static const std::vector<KeyBind> kMap = {
        // ---- every gameplay context
        {"E", K('E'), 0, C::Any, "interact: open / close a door, enter / exit the barracks, take control, ride along, release (prompt on screen)", "explode into primitives"},
        {"Tab", kTab, 0, C::Any, "take control of the next unit (soldiers, then UAVs, then vehicles)", "HUD on / off"},
        {"Shift+Tab", kTab, kModShift, C::Any, "take control of the previous unit", ""},
        {"Esc", kEsc, 0, C::Any, "release control / leave the ride -> clear the selection -> pause menu", "release -> clear selection -> quit"},
        {"P", K('P'), 0, C::Any, "pause / resume the simulation", "Phong shading on every part"},
        {"Pause", kPause, 0, C::Any, "pause / resume the simulation", ""},
        {"F1", F(1), 0, C::Any, "help overlay (every key)", ""},
        {"F2", F(2), 0, C::Any, "day / night cycle: automatic <-> manual clock", ""},
        {"Shift+F2", F(2), kModShift, C::Any, "day ops on / off: rover waves by day, the UAVs scramble (v9 M5)", ""},
        {"F3", F(3), 0, C::Any, "HUD: game -> game + debug blocks -> off", "possess the next soldier"},
        {"F4", F(4), 0, C::Any, "POV list: every moving object by distance", ""},
        {"F5", F(5), 0, C::Any, "quality preset LOW / HIGH / ULTRA / EPIC", ""},
        {"F6", F(6), 0, C::Any, "TAA on / off", ""},
        {"F7", F(7), 0, C::Any, "screen-space reflections on / off", ""},
        {"F8", F(8), 0, C::Any, "lens flare on / off", ""},
        {"F9", F(9), 0, C::Any, "session clip in / out (saved for --replay, v9 M9)", "auto-exposure on / off"},
        {"Shift+F9", F(9), kModShift, C::Any, "auto-exposure on / off", ""},
        {"Ctrl+P", 'P', kModCtrl, C::Any, "viewing projection: perspective -> orthographic top / front / side -> isometric (v10)", ""},
        {"Ctrl+L", 'L', kModCtrl, C::Any, "illumination term: all -> ambient -> diffuse -> specular (v10)", ""},
        {"F10", F(10), 0, C::Any, "depth of field on / off", ""},
        {"F11", F(11), 0, C::Any, "game mode (game HUD, compass, FOV kick, letterbox jumps)", ""},
        {"F12", F(12), 0, C::Any, "screenshot", ""},
        {"F", K('F'), 0, C::Any, "view: visible -> primitive diagnostic -> Z-buffer -> thermal", "flat shading on every part"},
        {"O", K('O'), 0, C::Any, "shading: per-object -> flat -> Gouraud -> Phong", "per-object shading"},
        {"G", K('G'), 0, C::Any, "explode composite objects into their primitives", "Gouraud shading on every part"},
        {"X", K('X'), 0, C::Any, "edges: solid, hidden-line wireframe, x-ray wireframe", ""},
        {"Z", K('Z'), 0, C::Any, "depth test on / off", ""},
        {"I", K('I'), 0, C::Any, "indexed / non-indexed drawing", ""},
        {"H", K('H'), 0, C::Any, "local axes of the hierarchy", ""},
        {"M", K('M'), 0, C::Any, "MicroMouse flood field and guide line", ""},
        {"B", K('B'), 0, C::Any, "PBR <-> classic Blinn-Phong", ""},
        {"N", K('N'), 0, C::Any, "v1 renderer (no post-processing)", ""},
        {"L", K('L'), 0, C::Any, "foliage cards on / off", ""},
        {"J", K('J'), 0, C::Any, "fireflies on / off", ""},
        {"R", K('R'), 0, C::Any, "rain on / off", ""},
        {"T", K('T'), 0, C::Any, "thermal palette", ""},
        {"Y", K('Y'), 0, C::Any, "thermal: first-person gimbal <-> external view", ""},
        {"Q", K('Q'), 0, C::Any, "thermal: force a NUC", ""},
        {"U", K('U'), 0, C::Any, "UAV feed on / off", ""},
        {"K", K('K'), 0, C::Any, "cinematic director (restarts the world) / stop it", ""},
        {"=", kEqual, 0, C::Any, "exposure up", ""},
        {"-", kMinus, 0, C::Any, "exposure down", ""},
        {",", kComma, 0, C::Any, "manual clock: scrub back (held)", ""},
        {".", kPeriod, 0, C::Any, "manual clock: scrub forward (held)", ""},
        {"/", kSlash, 0, C::Any, "jump to the next phase of the day", ""},
        {"1", K('1'), 0, C::Any, "moonlight on / off", ""},
        {"2", K('2'), 0, C::Any, "beacon on / off", ""},
        {"3", K('3'), 0, C::Any, "searchlight on / off", ""},
        {"4", K('4'), 0, C::Any, "practical lights on / off", ""},
        {"5", K('5'), 0, C::Any, "take control of the tank", ""},
        {"6", K('6'), 0, C::Any, "take control of the helicopter", ""},
        {"7", K('7'), 0, C::Any, "take control of the AA gun", ""},
        {"8", K('8'), 0, C::Any, "take control of the radar", ""},
        {"9", K('9'), 0, C::Any, "take control of the blast doors", ""},
        {"0", K('0'), 0, C::Any, "take control of UAV-1", ""},
        {"[", kLeftBracket, 0, C::Any, "previous controllable (every possessable object)", ""},
        {"]", kRightBracket, 0, C::Any, "next controllable (every possessable object)", ""},
        {"Backspace", kBackspace, 0, C::Any, "release control; driving a rover: give it back to its planner", ""},
        {"Home", kHome, 0, C::Any, "free fly camera from the current view", ""},
        {"End", kEnd, 0, C::Any, "back to the previous view", ""},
        {"Insert", kInsert, 0, C::Any, "next moving object's POV", ""},
        {"Delete", kDelete, 0, C::Any, "previous moving object's POV", ""},
        {";", kSemicolon, 0, C::Any, "render mode: raster -> hybrid RT -> Whitted -> path -> light tracing", ""},
        {"'", kApostrophe, 0, C::Any, "ray debugger (left click traces a pixel)", ""},
        {"`", kGrave, 0, C::Any, "Frame Explorer: freeze the frame, 16 stages", ""},
        {"MMB", kMouseMiddle, 0, C::Any, "jump into the object under the cursor", ""},
        {"Wheel", kMouseWheel, 0, C::Any, "zoom (thermal: 40 / 20 / 8 deg; free camera: fly speed)", ""},
        // ---- nothing under control
        {"C", K('C'), 0, C::Overview, "camera: orbit -> free -> chase -> UAV camera -> walk", "also soldier 1st / 3rd person, POV view, sight"},
        {"LMB click", kMouseLeftClick, 0, C::Overview, "select the object under the pointer", ""},
        {"LMB double-click", kMouseLeftDouble, 0, C::Overview, "take control of the object under the pointer", ""},
        {"LMB drag", kMouseLeftDrag, 0, C::Overview, "orbit / look around", ""},
        {"RMB drag", kMouseRightDrag, 0, C::Overview, "look around (free camera)", ""},
        {"Enter", kEnter, 0, C::Overview, "take control of the selection", ""},
        {"W A S D", K('W'), 0, C::Overview, "free camera: fly", ""},
        {"PgUp PgDn", kPageUp, 0, C::Overview, "free camera: up / down", ""},
        {"Shift", kLeftShift, 0, C::Overview, "free camera: x3 speed", ""},
        // ---- the v4 walker
        {"C", K('C'), 0, C::Walk, "camera: leave walk mode (orbit)", ""},
        {"W A S D", K('W'), 0, C::Walk, "walk", ""},
        {"Shift", kLeftShift, 0, C::Walk, "run", ""},
        {"Enter", kEnter, 0, C::Walk, "open / close the nearest door or barrier (E does the same)", ""},
        {"Left Ctrl", kLeftCtrl, 0, C::Walk, "open / close the nearest door or barrier", ""},
        {"RMB drag", kMouseRightDrag, 0, C::Walk, "look around", ""},
        // ---- a soldier under control
        {"W A S D", K('W'), 0, C::Soldier, "move (third person: relative to the camera)", ""},
        {"Shift", kLeftShift, 0, C::Soldier, "sprint", ""},
        {"Space", kSpace, 0, C::Soldier, "jump", "jump, else pause"},
        {"Left Alt", kLeftAlt, 0, C::Soldier, "crouch <-> stand", ""},
        {"V", K('V'), 0, C::Soldier, "first person <-> third person", "V or C"},
        {"Mouse", kMouseMove, 0, C::Soldier, "look (sensitivity / invert Y in Settings)", ""},
        {"LMB", kMouseLeftHold, 0, C::Soldier, "fire the rifle (hold = automatic)", ""},
        {"RMB", kMouseRightHold, 0, C::Soldier, "aim (iron sight / over the shoulder)", ""},
        {"Left Ctrl", kLeftCtrl, 0, C::Soldier, "fire the rifle", ""},
        {"Enter", kEnter, 0, C::Soldier, "fire the rifle", ""},
        {"Arrows", kLeft, 0, C::Soldier, "turn / aim", ""},
        {"\\", kBackslash, 0, C::Soldier, "stand / crouch / wave", ""},
        // ---- a vehicle, UAV or rover under control
        {"W A S D", K('W'), 0, C::Vehicle, "drive / throttle, steer / yaw", ""},
        {"PgUp PgDn", kPageUp, 0, C::Vehicle, "lift (helicopter, UAV)", ""},
        {"Arrows", kLeft, 0, C::Vehicle, "aim turret / gun / gimbal", ""},
        {"LMB drag", kMouseLeftDrag, 0, C::Vehicle, "aim turret / gun / gimbal", ""},
        {"Enter", kEnter, 0, C::Vehicle, "fire / launch (hold = automatic)", ""},
        {"Left Ctrl", kLeftCtrl, 0, C::Vehicle, "fire / launch", ""},
        {"\\", kBackslash, 0, C::Vehicle, "action: start / land, open / close, mode", ""},
        {"Shift", kLeftShift, 0, C::Vehicle, "boost", ""},
        {"V", K('V'), 0, C::Vehicle, "chase -> sight -> alt camera; UAV: chase -> FPV -> gimbal (v9 M5)", "C"},
        // ---- riding along
        {"W A S D", K('W'), 0, C::Ride, "take control of the object", ""},
        {"Arrows", kLeft, 0, C::Ride, "take control of the object", ""},
        {"PgUp PgDn", kPageUp, 0, C::Ride, "take control of the object", ""},
        {"V", K('V'), 0, C::Ride, "first person -> chase -> alt camera; UAV: chase -> FPV -> gimbal (v9 M5)", "C"},
        {"LMB drag", kMouseLeftDrag, 0, C::Ride, "free look", ""},
        // ---- pause menu (Esc with nothing under control)
        {"Esc", kEsc, 0, C::Menu, "resume", ""},
        {"Tab", kTab, 0, C::Menu, "next page (Travel, Time, View, Settings)", ""},
        {"Shift+Tab", kTab, kModShift, C::Menu, "previous page", ""},
        {"1-9", K('1'), 0, C::Menu, "Travel: UAV-1, UAV-2, soldier 1st, soldier 3rd, any POV, free camera, tank, helicopter, rover", ""},
        {"Q W E R", K('Q'), 0, C::Menu, "Time: dawn, noon, dusk, midnight", ""},
        {"T", K('T'), 0, C::Menu, "Time: automatic cycle on / off", ""},
        {"Y", K('Y'), 0, C::Menu, "Time: cycle speed x1 / x2 / x4 / x8 / x0.5", ""},
        {"O", K('O'), 0, C::Menu, "Time: day ops on / off (rover waves by day, the UAVs scramble; v9 M5)", ""},
        {"A S D F H U", K('A'), 0, C::Menu, "View: 1st / 3rd person, render mode, Frame Explorer, thermal, HUD, UAV feed", ""},
        {"G", K('G'), 0, C::Menu, "close the menu", ""},
        {"Left Right", kRight, 0, C::Menu, "Time: nudge the clock 15 min; Settings: change the value", ""},
        {"Up Down", kUp, 0, C::Menu, "Settings: select a row", ""},
        {"Enter", kEnter, 0, C::Menu, "Settings: toggle / next value; tabs: open", ""},
        {"LMB", kMouseLeftClick, 0, C::Menu, "press a tab or button, drag the time slider", ""},
        // ---- F4 POV list
        {"Up Down", kUp, 0, C::Picker, "select", ""},
        {"PgUp PgDn", kPageUp, 0, C::Picker, "select (8 rows)", ""},
        {"Enter", kEnter, 0, C::Picker, "jump to the selected POV", ""},
        {"Esc", kEsc, 0, C::Picker, "close the list", ""},
        {"F4", F(4), 0, C::Picker, "close the list", ""},
        {"LMB", kMouseLeftClick, 0, C::Picker, "jump to the clicked row", ""},
    };
    return kMap;
}

std::vector<std::string> keyMapConflicts(const std::vector<KeyBind>& map) {
    std::vector<std::string> out;
    for (size_t i = 0; i < map.size(); ++i)
        for (size_t j = i + 1; j < map.size(); ++j) {
            const KeyBind& a = map[i];
            const KeyBind& b = map[j];
            if (a.code != b.code || a.mods != b.mods || !keyContextsOverlap(a.ctx, b.ctx)) continue;
            char buf[512];
            std::snprintf(buf, sizeof(buf), "%s (%s / %s): %s <-> %s", a.key, keyCtxName(a.ctx), keyCtxName(b.ctx), a.action, b.action);
            out.push_back(buf);
        }
    return out;
}

std::vector<KeyBind> keyMapChanges(const std::vector<KeyBind>& map) {
    std::vector<KeyBind> out;
    for (const KeyBind& k : map)
        if (k.was && *k.was) out.push_back(k);
    return out;
}

namespace {
std::string esc(const char* s) {
    std::string o;
    for (const char* p = s; p && *p; ++p) {
        if (*p == '"' || *p == '\\') o += '\\';
        o += *p;
    }
    return o;
}
}  // namespace

std::string keyMapJson(const std::vector<KeyBind>& map) {
    std::string o = "{\n  \"source\": \"src/scene/KeyMap.cpp keyMapV9()\",\n  \"keys\": [\n";
    for (size_t i = 0; i < map.size(); ++i) {
        const KeyBind& k = map[i];
        char head[96];
        std::snprintf(head, sizeof(head), "\", \"code\": %d, \"mods\": %d, \"context\": \"", k.code, k.mods);
        o += "    {\"key\": \"" + esc(k.key) + head + keyCtxName(k.ctx) + "\", \"action\": \"" + esc(k.action) + "\", \"was\": \"" +
             esc(k.was) + "\"}" + (i + 1 < map.size() ? ",\n" : "\n");
    }
    o += "  ],\n  \"conflicts\": [";
    const std::vector<std::string> c = keyMapConflicts(map);
    for (size_t i = 0; i < c.size(); ++i) o += (i ? ", \"" : "\"") + esc(c[i].c_str()) + "\"";
    o += "]\n}\n";
    return o;
}

}  // namespace ts
