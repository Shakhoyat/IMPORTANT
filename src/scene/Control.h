// Possession table: which key takes which controllable, and helpers to find controllables by name.
// Pure logic (no GL, no GLFW) so it is unit-tested. Owner: D.
#pragma once

#include <vector>

#include "scene/Controllable.h"

namespace ts {

enum class Slot { Tank = 0, Helicopter, AaGun, Radar, Doors, Uav, Count };
constexpr int kSlotCount = static_cast<int>(Slot::Count);

struct SlotInfo {
    Slot slot;
    char key;                 // digit key that possesses it: 5 6 7 8 9 0
    const char* label;        // HUD / help text
    const char* keywords[3];  // upper-case substrings of Controllable::name() (nullptr terminated)
};

const SlotInfo& slotInfo(Slot s);
// '5'..'9', '0' -> slot; returns false for any other key.
bool slotForKey(char key, Slot& out);

// Case-insensitive substring test.
bool nameHas(const char* name, const char* needle);
// Index of the controllable that fills 'slot' (matched by name; falls back to the documented order
// Tank, Helicopter, AA gun, Radar, Blast doors, UAV when no name matches any slot), or -1.
int resolveSlot(const std::vector<Controllable*>& c, Slot slot);
// Slot a controllable fills (by name), or -1.
int slotOfName(const char* name);
// Next possessable controllable after 'current' (dir = +1 / -1); -1 if none. current = -1 starts the cycle.
int cyclePossessable(const std::vector<Controllable*>& c, int current, int dir);
// Door / barrier controllable nearest to 'p' within maxDist (default 3.5 m), or -1. Walk-mode interaction
// toggles it by giving it one frame of possession with actionPressed set.
int nearestToggle(const std::vector<Controllable*>& c, const Vec3& p, float maxDist = 3.5f);

}  // namespace ts
