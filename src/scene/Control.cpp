#include "scene/Control.h"

#include <cctype>
#include <cstring>

namespace ts {

namespace {
const SlotInfo kSlots[kSlotCount] = {
    {Slot::Tank, '5', "TANK", {"TANK", "MBT", nullptr}},
    {Slot::Helicopter, '6', "HELICOPTER", {"HELI", nullptr, nullptr}},
    {Slot::AaGun, '7', "AA GUN", {"AA GUN", "AA-GUN", "ANTI-AIR"}},
    {Slot::Radar, '8', "RADAR", {"RADAR", nullptr, nullptr}},
    {Slot::Doors, '9', "BLAST DOORS", {"DOOR", nullptr, nullptr}},
    {Slot::Uav, '0', "UAV", {"UAV", "DRONE", nullptr}},
};
}  // namespace

const SlotInfo& slotInfo(Slot s) { return kSlots[static_cast<int>(s)]; }

bool slotForKey(char key, Slot& out) {
    for (const SlotInfo& s : kSlots)
        if (s.key == key) {
            out = s.slot;
            return true;
        }
    return false;
}

bool nameHas(const char* name, const char* needle) {
    if (!name || !needle) return false;
    const size_t n = std::strlen(needle);
    for (const char* p = name; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && std::toupper(static_cast<unsigned char>(p[i])) == std::toupper(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

int slotOfName(const char* name) {
    for (const SlotInfo& s : kSlots)
        for (const char* k : s.keywords)
            if (k && nameHas(name, k)) return static_cast<int>(s.slot);
    return -1;
}

int resolveSlot(const std::vector<Controllable*>& c, Slot slot) {
    bool anyNamed = false;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!c[i]) continue;
        const int s = slotOfName(c[i]->name());
        if (s >= 0) anyNamed = true;
        if (s == static_cast<int>(slot)) return static_cast<int>(i);
    }
    const int idx = static_cast<int>(slot);
    if (!anyNamed && idx < static_cast<int>(c.size()) && c[idx]) return idx;
    return -1;
}

int cyclePossessable(const std::vector<Controllable*>& c, int current, int dir) {
    const int n = static_cast<int>(c.size());
    if (n == 0) return -1;
    dir = dir < 0 ? -1 : 1;
    int i = current;
    for (int k = 0; k < n; ++k) {
        i = current < 0 && k == 0 ? (dir > 0 ? 0 : n - 1) : ((i + dir) % n + n) % n;
        if (c[i] && c[i]->possessable()) return i;
    }
    return -1;
}

int nearestToggle(const std::vector<Controllable*>& c, const Vec3& p, float maxDist) {
    int best = -1;
    float bestD = maxDist;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!c[i]) continue;
        const char* n = c[i]->name();
        if (!nameHas(n, "DOOR") && !nameHas(n, "BARRIER") && !nameHas(n, "GATE")) continue;
        const Vec3 q = c[i]->position();
        const float d = length(Vec3{q.x - p.x, 0.0f, q.z - p.z});
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

}  // namespace ts
