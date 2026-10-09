// v9 M2: the input map as data. Every key / mouse binding of the interactive program with the context it works in, so
// the scheme can be checked for conflicts (one key, two actions in a context that can be active at the same time),
// exported for the workbook (--export-keys FILE) and compared with what each key did before v9 ('was').
//
// Contexts: Any = every gameplay context; Overview = nothing under control (orbit / free / chase / UAV camera); Walk =
// the v4 first-person walker; Soldier / Vehicle = a soldier / a vehicle, UAV or rover under control; Ride = riding
// along; Menu and Picker are modal: while open they take their keys before anything else, so they overlap nothing.
// Codes are GLFW key codes; mouse gestures use kMouse* pseudo codes. Pure data + CPU code (tests/test_v9_m2.cpp).
#pragma once

#include <string>
#include <vector>

namespace ts {

enum class KeyCtx { Any, Overview, Walk, Soldier, Vehicle, Ride, Menu, Picker };
const char* keyCtxName(KeyCtx c);   // "ANY", "OVERVIEW", ...
// Can both contexts be active at the same time (Any overlaps every gameplay context; Menu / Picker are modal)?
bool keyContextsOverlap(KeyCtx a, KeyCtx b);

constexpr int kModShift = 1;   // GLFW_MOD_SHIFT
// Mouse gestures (not GLFW codes; distinct gestures of one button are distinct bindings).
constexpr int kMouseLeftClick = 1000, kMouseLeftDouble = 1001, kMouseLeftDrag = 1002, kMouseLeftHold = 1003,
              kMouseRightHold = 1004, kMouseRightDrag = 1005, kMouseMiddle = 1006, kMouseWheel = 1007, kMouseMove = 1008;

struct KeyBind {
    const char* key;      // as shown to the player: "E", "Shift+Tab", "LMB double-click"
    int code;             // GLFW key code or a kMouse* gesture
    int mods;             // 0 or kModShift
    KeyCtx ctx;
    const char* action;
    const char* was;      // before v9 M2 ("" = unchanged / new key)
};

const std::vector<KeyBind>& keyMapV9();
// "KEY (CTX): action A <-> action B" for every pair that shares code + mods in overlapping contexts; empty = none.
std::vector<std::string> keyMapConflicts(const std::vector<KeyBind>& map);
// Bindings that changed in M2 (non-empty 'was'), in table order.
std::vector<KeyBind> keyMapChanges(const std::vector<KeyBind>& map);
// JSON for the workbook / report: {"keys": [{"key", "code", "mods", "context", "action", "was"}...], "conflicts": [...]}.
std::string keyMapJson(const std::vector<KeyBind>& map);

}  // namespace ts
