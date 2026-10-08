// v7 points of view (WP-G): every moving object of the world as a camera source. Soldiers (eyes in the head node,
// head bob from the gait), intruder rovers (bumper / mast camera), both UAVs (gimbal, belly), the helicopter cockpit,
// the tank (gunner sight, commander hatch), AA gun, SAM / MLRS launchers, the radar, and rounds in flight (UAV
// missiles, tank shells: follow camera until impact). Doors, the gate barrier, flag, windsock and the sky clock are
// not moving objects and are left out.
//
// Pure CPU code over the Scene (no GL): the list, its orders (picker = by distance, Insert / Delete = stable),
// the pose of every view, the middle-click ray pick and the projectile follow logic are unit-tested.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "scene/CameraRig.h"
#include "scene/Controllable.h"

namespace ts {

class Scene;
struct Projectile;

enum class PovKind { Soldier, Rover, Uav, Helicopter, Tank, AaGun, Launcher, Radar, Missile, Shell };
const char* povKindName(PovKind k);   // "SOLDIER", "ROVER", ...
const char* povKindTag(PovKind k);    // three-letter picker icon tag: SLD RVR UAV HEL TNK AAG LCH RDR MSL SHL
bool povHostile(PovKind k);           // intruder rovers
bool povProjectile(PovKind k);

// Views of a source. First = eyes / sight / cockpit, Chase = third person, Alt = second first-person camera
// (tank commander hatch, UAV belly camera, rover mast camera); sources without one fall back to First.
enum class PovView { First = 0, Chase = 1, Alt = 2 };
const char* povViewName(PovView v);

struct PovSource {
    PovKind kind = PovKind::Soldier;
    uint32_t key = 0;         // stable identity across frames (category << 24 | index, rounds: owner / launch time)
    int controllable = -1;    // index in sa::controllables(scene), or -1
    int intruder = -1;        // intruder slot, or -1
    int uav = -1;             // UAV index (0, 1) for UAVs, or -1
    int projectile = -1;      // index in Combat::projectiles() of this frame, or -1
    std::string name;
    Vec3 position, velocity;  // velocity: rounds exactly, others from PovSpeedMeter (else 0)
    float speed = 0.0f;       // m/s
    float distance = 0.0f;    // from the viewer given to listPovSources
    Vec3 centre;              // pick sphere centre (body centre, above the ground point)
    float radius = 0.5f;      // pick sphere (m)
    bool canControl = false;  // possession (controllables) or the player taking the wheel (rovers)
    bool hasAlt = false;
};

// Every moving object, in the stable order used by Insert / Delete: vehicles and installations (scene order),
// soldiers, UAV-2, intruder rovers (alive), rounds in flight. 'viewer' fills distance.
std::vector<PovSource> listPovSources(const Scene& scene, const Vec3& viewer);
// Picker order: nearest first (ties: stable order).
void sortByDistance(std::vector<PovSource>& list);
int findPov(const std::vector<PovSource>& list, uint32_t key);
// Next (dir +1) / previous (-1) source after 'key' in the list order (wraps; key absent: first / last). -1 if empty.
int cyclePov(const std::vector<PovSource>& list, uint32_t key, int dir);
// Middle-click: the source whose pick sphere the ray hits first; if none, the one closest to the ray within
// 'maxAngleDeg' (small or far objects stay pickable). -1 if none.
int pickPov(const std::vector<PovSource>& list, const Vec3& origin, const Vec3& dir, float maxAngleDeg = 4.0f);
// Ray through pixel (px, py) (origin top-left) of a w x h view with inverse(proj * view) = invViewProj.
void pixelRay(const Mat4& invViewProj, float px, float py, int w, int h, Vec3& origin, Vec3& dir);

// Camera pose of a view of a source (valid world pose, finite, unit orientation). False if the source vanished.
// lookYaw / lookPitch (degrees): free look added by the mouse while riding along.
bool povPose(const Scene& scene, const PovSource& src, PovView view, RigPose& out, float lookYawDeg = 0.0f,
             float lookPitchDeg = 0.0f);
// Soldier eyes: the head node's world position (bobs with the gait), looking where the soldier looks.
RigPose soldierEyePose(const Scene& scene, int controllable, float lookYawDeg = 0.0f, float lookPitchDeg = 0.0f);

// Speed of every source from its positions over time (rounds keep their exact velocity).
class PovSpeedMeter {
public:
    void update(std::vector<PovSource>& list, float time);
    void clear() { last_.clear(); }

private:
    struct Entry { Vec3 position; float time = 0.0f; float speed = 0.0f; Vec3 velocity; };
    std::unordered_map<uint32_t, Entry> last_;
};

// Follows one round in flight from frame to frame: by key, else the alive round of the same kind and owner
// closest to where it should be now. Once it is gone (impact) 'impact' holds its last position.
struct ProjectileFollow {
    uint32_t key = 0;
    int kind = -1, owner = -1;
    Vec3 position, velocity;
    bool lost = false;
    Vec3 impact;
    // Returns the index of the round in 'rounds' (or -1 once it is gone). dt = time since the previous call.
    int track(const std::vector<Projectile>& rounds, float combatTime, float dt);
};
uint32_t projectileKey(const Projectile& p, float combatTime);

// Strength 0..1 of the "you are being designated" alert for a rover at 'p': a UAV's laser spot on it (1 at the rover,
// 0 beyond 3 m) or a confirmed squad track within 2 m (0.6).
float designationAlert(const Scene& scene, const Vec3& p);

// --- picker overlay layout (pixels, origin top-left) ----------------------------------------------------------------------
struct PickerLayout {
    float x = 0, y = 0, w = 0;   // panel
    float rowH = 0, headerH = 0;
    int firstRow = 0, rows = 0;  // visible window of the list
};
PickerLayout pickerLayout(int screenW, int screenH, int count, int selected);
// List index under the cursor, or -1.
int pickerRowAt(const PickerLayout& l, int count, float x, float y);

}  // namespace ts
