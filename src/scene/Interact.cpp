#include "scene/Interact.h"

#include <algorithm>
#include <cmath>

#include "scene/WalkSpace.h"

namespace ts {

const char* interactActionName(InteractAction a) {
    switch (a) {
        case InteractAction::None: return "NONE";
        case InteractAction::OpenDoor: return "OPEN_DOOR";
        case InteractAction::CloseDoor: return "CLOSE_DOOR";
        case InteractAction::EnterBuilding: return "ENTER_BUILDING";
        case InteractAction::ExitBuilding: return "EXIT_BUILDING";
        case InteractAction::TakeControl: return "TAKE_CONTROL";
        case InteractAction::RideAlong: return "RIDE_ALONG";
        case InteractAction::Release: return "RELEASE";
    }
    return "?";
}

InteractPick pickInteractable(const std::vector<Interactable>& items, const InteractView& view, const LineOfSight& sight) {
    InteractPick best;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].always && items[i].action != InteractAction::None) {
            best.index = static_cast<int>(i);
            return best;
        }
    const float coneCos = std::cos(radians(clampf(view.coneHalfDeg, 0.0f, 179.0f)));
    Vec3 f = view.forward;
    if (view.flatCone) f.y = 0.0f;
    const float fl = length(f);
    const bool hasDir = fl > 1e-5f;
    if (hasDir) f = f * (1.0f / fl);
    for (size_t i = 0; i < items.size(); ++i) {
        const Interactable& it = items[i];
        if (it.action == InteractAction::None) continue;
        Vec3 d = it.point - view.player;
        if (std::fabs(d.y) > view.maxRise && view.flatCone) continue;
        if (view.flatCone) d.y = 0.0f;
        const float dist = length(d);
        if (dist > it.reach) continue;
        // Cone: a point practically under the player passes (standing on / next to it).
        float cosA = 1.0f;
        if (hasDir && dist > 0.15f) cosA = dot(d * (1.0f / dist), f);
        if (cosA < coneCos) continue;
        if (sight && !sight(view.eye, it.point)) continue;
        const float ang = degrees(std::acos(clampf(cosA, -1.0f, 1.0f)));
        if (best.index < 0 || dist < best.distance - 0.05f || (std::fabs(dist - best.distance) <= 0.05f && ang < best.angleDeg)) {
            best.index = static_cast<int>(i);
            best.distance = dist;
            best.angleDeg = ang;
        }
    }
    return best;
}

std::string interactPrompt(const Interactable& it, const char* key) {
    std::string verb;
    switch (it.action) {
        case InteractAction::None: return "";
        case InteractAction::OpenDoor: verb = "Open "; break;
        case InteractAction::CloseDoor: verb = "Close "; break;
        case InteractAction::EnterBuilding: verb = "Enter "; break;
        case InteractAction::ExitBuilding: verb = "Exit "; break;
        case InteractAction::TakeControl: verb = "Take control of "; break;
        case InteractAction::RideAlong: verb = "Ride "; break;
        case InteractAction::Release: verb = "Release"; break;
    }
    std::string out = "[";
    out += key;
    out += "] ";
    out += verb;
    if (it.action != InteractAction::Release) out += it.name;
    return out;
}

float segmentBoxEntry(const Vec3& a, const Vec3& d, float len, const Box& b) {
    float t0 = 0.0f, t1 = len;
    const float oo[3] = {a.x, a.y, a.z}, dd[3] = {d.x, d.y, d.z}, mn[3] = {b.min.x, b.min.y, b.min.z},
                mx[3] = {b.max.x, b.max.y, b.max.z};
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(dd[k]) < 1e-7f) {
            if (oo[k] < mn[k] || oo[k] > mx[k]) return -1.0f;
            continue;
        }
        float u = (mn[k] - oo[k]) / dd[k], v = (mx[k] - oo[k]) / dd[k];
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u);
        t1 = std::min(t1, v);
        if (t0 > t1) return -1.0f;
    }
    return t0;
}

namespace {
bool contains(const Box& b, const Vec3& p, float pad = 0.0f) {
    return p.x >= b.min.x - pad && p.x <= b.max.x + pad && p.y >= b.min.y - pad && p.y <= b.max.y + pad &&
           p.z >= b.min.z - pad && p.z <= b.max.z + pad;
}
}  // namespace

bool segmentClear(const WalkSpace* ws, const std::vector<Box>& solids, const Vec3& a, const Vec3& b, float slack) {
    const Vec3 ab = b - a;
    const float len = length(ab);
    if (len < 1e-4f) return true;
    const Vec3 d = ab * (1.0f / len);
    const float limit = len - std::max(slack, 0.0f);
    auto blocks = [&](const Box& bx) {
        if (contains(bx, a) || contains(bx, b, 0.05f)) return false;
        const float t = segmentBoxEntry(a, d, len, bx);
        return t >= 0.0f && t < limit;
    };
    if (ws)
        for (const Box& bx : ws->walls())
            if (blocks(bx)) return false;
    for (const Box& bx : solids)
        if (blocks(bx)) return false;
    return true;
}

// ---- Tab order ------------------------------------------------------------------------------------------------------------

int unitGroup(const PovSource& s) {
    switch (s.kind) {
        case PovKind::Soldier: return 0;
        case PovKind::Uav: return 1;
        case PovKind::Helicopter:
        case PovKind::Tank:
        case PovKind::AaGun:
        case PovKind::Launcher:
        case PovKind::Radar: return 2;
        default: return -1;   // intruder rovers (hostile), rounds in flight
    }
}

bool isUnit(const PovSource& s) { return unitGroup(s) >= 0; }

std::vector<int> unitOrder(const std::vector<PovSource>& list) {
    std::vector<int> out;
    for (int g = 0; g < 3; ++g)
        for (size_t i = 0; i < list.size(); ++i)
            if (unitGroup(list[i]) == g) out.push_back(static_cast<int>(i));
    return out;
}

int cycleUnit(const std::vector<PovSource>& list, uint32_t currentKey, int dir) {
    const std::vector<int> order = unitOrder(list);
    if (order.empty()) return -1;
    const int n = static_cast<int>(order.size());
    int at = -1;
    for (int k = 0; k < n; ++k)
        if (currentKey != 0 && list[static_cast<size_t>(order[k])].key == currentKey) at = k;
    if (at < 0) return dir < 0 ? order[n - 1] : order[0];
    return order[((at + (dir < 0 ? -1 : 1)) % n + n) % n];
}

}  // namespace ts
