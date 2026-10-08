#include "scene/Control.h"

#include <cmath>

#include <cctype>
#include <cstring>

namespace ts {

namespace {
const SlotInfo kSlots[kSlotCount] = {
    {Slot::Tank, '5', "TANK", {"TANK", "MBT", nullptr}},
    {Slot::Helicopter, '6', "HELICOPTER", {"HELI", nullptr, nullptr}},
    {Slot::AaGun, '7', "AA GUN", {"AA GUN", "GATLING", "AA-1"}},
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


float angleDiffDeg(float a, float b) {
    float d = std::fmod(a - b, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d <= -180.0f) d += 360.0f;
    return d;
}

ControlInput thirdPersonSoldierInput(const ControlInput& raw, float moveFwd, float moveRight, float camYawDeg,
                                     float bodyYawDeg, bool aiming, float dt) {
    ControlInput in = raw;
    in.turn = in.aimYaw = 0.0f;
    in.strafe = 0.0f;
    in.forward = 0.0f;
    in.aimYawDelta = 0.0f;
    const float cy = radians(camYawDeg);
    const Vec3 f{std::sin(cy), 0.0f, std::cos(cy)}, r{-std::cos(cy), 0.0f, std::sin(cy)};
    float desired = bodyYawDeg, tau = 0.09f, mag = 0.0f;
    if (aiming) {
        desired = camYawDeg;
        tau = 0.035f;
        in.forward = clampf(moveFwd, -1.0f, 1.0f);
        in.strafe = clampf(moveRight, -1.0f, 1.0f);
    } else {
        const Vec3 wish = f * moveFwd + r * moveRight;
        mag = std::sqrt(wish.x * wish.x + wish.z * wish.z);
        if (mag > 0.05f) {
            desired = degrees(std::atan2(wish.x, wish.z));
            in.forward = std::min(mag, 1.0f);
        }
    }
    const float delta = angleDiffDeg(desired, bodyYawDeg);
    const float k = dt > 0.0f ? 1.0f - std::exp(-dt / tau) : 0.0f;
    const float maxStep = 720.0f * std::max(dt, 0.0f);
    const float step = clampf(delta * k, -maxStep, maxStep);
    in.aimYawDelta = -step;   // Soldier: yaw -= aimYawDelta (positive = turn right)
    // Turning on the spot more than ~100 degrees: hold the walk until the body faces the way (no moonwalk arcs).
    if (!aiming && mag > 0.05f && std::fabs(delta) > 100.0f) in.forward *= 0.35f;
    return in;
}

}  // namespace ts
