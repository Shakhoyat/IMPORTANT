// v9 M2: context-sensitive interaction (the "use" key E), as in most action games. Every frame the App lists what can
// be used from where the player is (doors, the barrier, the barracks hall entrance, other soldiers, vehicles, UAVs,
// "release" while controlling something) and this module picks ONE of them: the nearest that is within its reach,
// inside the view cone and not hidden behind a wall. Its prompt ("[E] Open blast doors", "[E] Take control of UAV-1")
// is drawn on screen and E performs it. Also the Tab order of controllable units.
//
// Pure CPU (no GL, no GLFW): the pick, the line-of-sight test and the cycle order are unit-tested (tests/test_v9_m2.cpp).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ai/Occlusion.h"
#include "math/Math3D.h"
#include "scene/Pov.h"

namespace ts {

class WalkSpace;

enum class InteractAction { None, OpenDoor, CloseDoor, EnterBuilding, ExitBuilding, TakeControl, RideAlong, Release };
const char* interactActionName(InteractAction a);   // "OPEN_DOOR", ... (logs, tests)

struct Interactable {
    InteractAction action = InteractAction::None;
    std::string name;        // shown in the prompt: "blast doors", "Barracks", "UAV-1 DRONE"
    Vec3 point;              // where it is: reach, view cone and line of sight are measured to this point
    float reach = 2.5f;      // metres, horizontal distance from the player
    bool always = false;     // no reach / cone / sight test (the current selection, "release" while controlling)
    int ref = -1;            // caller's handle (controllable index, PovSource index, ...)
    uint32_t key = 0;        // PovSource key of units (0 otherwise)
};

// Where the player is and looks. On foot the reach is measured from the body and the cone is horizontal (looking down
// at a door handle still counts); from a free / orbit camera the cone is a narrow 3D cone around the view ray.
struct InteractView {
    Vec3 player;               // body position (reach origin; the cone is measured from here as well)
    Vec3 eye;                  // line-of-sight origin (the soldier's eyes / the camera)
    Vec3 forward{0, 0, 1};     // view direction (unit; the camera's, also in third person)
    float coneHalfDeg = 50.0f;
    bool flatCone = true;      // compare headings only (on foot)
    float maxRise = 2.6f;      // largest height difference player -> point (a door one floor up is out of reach)
};

// true = nothing solid between 'from' and 'to'. An empty function means "always clear".
using LineOfSight = std::function<bool(const Vec3& from, const Vec3& to)>;

struct InteractPick {
    int index = -1;            // into the list, -1 = nothing to use
    float distance = 0.0f;     // horizontal (flat cone) or 3D distance
    float angleDeg = 0.0f;     // from the view direction
};

// Rules: items with 'always' win (first one listed); otherwise the nearest item within reach and |dy| <= maxRise,
// inside the cone, with a clear line of sight from the eye. Ties (within 5 cm) go to the smaller view angle.
InteractPick pickInteractable(const std::vector<Interactable>& items, const InteractView& view, const LineOfSight& sight);

// "[E] Open blast doors", "[E] Enter Barracks", "[E] Take control of UAV-1 DRONE", "[E] Ride UAV-2", "[E] Release".
std::string interactPrompt(const Interactable& it, const char* key = "E");

// Segment a -> b against WalkSpace walls (may be null) and solid boxes: false when it passes through one. Boxes that
// contain 'a' (the player's own cell / vehicle) or 'b' (the target itself), and hits within 'slack' metres of b, are
// ignored, so a door frame or the vehicle's own hull never hides it.
bool segmentClear(const WalkSpace* ws, const std::vector<Box>& solids, const Vec3& a, const Vec3& b, float slack = 0.3f);
// Entry distance of the segment a -> b (length len, unit dir d) into box bx, or -1 (no hit within the segment).
float segmentBoxEntry(const Vec3& a, const Vec3& d, float len, const Box& bx);

// --- Tab: cycle the controllable units ---------------------------------------------------------------------------------
// Units are the friendly sources a player can be: soldiers first, then the UAVs, then vehicles and installations
// (tank, helicopter, AA gun, launchers, radar), each group in listPovSources order. Rovers and rounds are left out.
bool isUnit(const PovSource& s);
int unitGroup(const PovSource& s);   // 0 soldier, 1 UAV, 2 vehicle / installation, -1 not a unit
std::vector<int> unitOrder(const std::vector<PovSource>& list);   // indices into list, in Tab order
// Next (dir +1) / previous (-1) unit after the one with 'currentKey' (absent: the first / the last). -1 when none.
int cycleUnit(const std::vector<PovSource>& list, uint32_t currentKey, int dir);

}  // namespace ts
