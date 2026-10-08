// v6 cut (default, the video and the 'K' camera): 120 s that open on the night battle (wave banner, both UAVs on patrol,
// combined strike, thermal FPV, the kill), play a dawn time-lapse (the surviving rovers withdraw), the day camp (flyover,
// soldiers on patrol, the mechanic at the tank, the logistics crate run, the helicopter take-off, the day thermal view
// with the soldiers boxed as PERS), the dusk shift change, a soldier walking into the bunker and the night searchlights.
// The script forces the clock (DirectorScript::hour, manual time) so every run shows the same times of day.
// v5 cut (kept for the report captures that were authored against it): a 120 s sequence of authored camera shots over the whole base (flyover with the wave banner, radar,
// helicopter take-off, tank, AA gun, the walk down the ramp into the bunker, then the v5 game: both UAVs on patrol, a wide
// coordinated strike, thermal FPV with all track boxes, the multi-hit kill, pull-back). The game shots follow the live
// action (squad target / UAV positions / most damaged rover), the strikes themselves are played by the squad AI.
// Used by the 'K' camera and the video recorder. The director also scripts the controllables: for every moment it
// can return a synthetic ControlInput for one slot (directorScript), which the App feeds to Scene::update.
// v7 cut (~150 s, the v7 video): night strike action -> POV picker and jumps (UAV gimbal, a round ridden to its impact,
// an intruder rover being designated) -> a soldier's eyes, firing -> dawn time-lapse -> day hero orbits of the tank,
// helicopter and AA gun -> ray-tracing showcase (the five render modes on the prop set, then the ray debugger's tree)
// -> Frame Explorer walkthrough (raster scan, colour interpolation, reflection, final) -> dusk -> night searchlights.
// Segments that are not plain camera shots carry an action (V7Act) that the recorder performs with the game's own
// systems (POV jumps, possession, render modes, the Frame Explorer); their eye / target are the fallback camera.
#pragma once

#include "scene/Control.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

namespace ts {

enum class DirectorCut { V5, V6, V7 };

// v7 cut: what the recorder does in a segment besides pointing the camera.
enum class V7Act { Camera, Picker, PovUav, PovMissile, PovRover, SoldierFire, RtMode, RtTree, Explorer };

struct DirectorShot {
    const char* name = "";
    Vec3 eye, target;
    float fovDeg = 45.0f;
    bool thermal = false;      // show this shot through the thermal camera palette
    bool uavCamera = false;    // look through the UAV's own camera (eye/target unused)
    bool fpvHud = false;       // draw the thermal first-person (FLIR) HUD over this shot
    bool gameHud = false;      // draw the v5 game HUD (top bar, mini-map, markers, banners) over this shot
    bool walk = false;         // first-person walk through the bunker (eye height 1.7 m, WalkSpace floors)
    float fade = 1.0f;         // 0 at a cut, 1 inside the shot (for a short dip to black between shots)
    int script = -1;           // index of the shot in the sequence (see directorShotCount / directorShotStart)
    // v7 cut
    V7Act act = V7Act::Camera;
    int param = 0;             // RtMode: rt::RtMode index; Explorer: Frame Explorer stage (0-based)
    float local = 0.0f;        // seconds since the segment began
    float length = 0.0f;       // segment length (s)
    const char* subtitle = ""; // second title line (what the segment shows)
};

// What the director does to the world at a moment: possess one controllable with a synthetic input.
// (v6: no scripted strikes - the squad engages only what its cameras detect.)
struct DirectorScript {
    int slot = -1;             // Slot (Tank ... Uav) to possess, -1 = nobody
    ControlInput in;           // fire / actionPressed are LEVELS here: the App turns rising edges into presses
    float hour = -1.0f;        // >= 0: force the clock to this hour (manual time); -1 = leave the day cycle alone
};

// v6 shot indices (DirectorCut::V6).
enum DirectorShotV6 { kV6NightWave, kV6Patrol, kV6Strike, kV6FpvTracks, kV6Kill, kV6Dawn, kV6DayFlyover, kV6SoldierPatrol,
                      kV6Mechanic, kV6Logistics, kV6Heli, kV6DayThermal, kV6Dusk, kV6Bunker, kV6NightLights, kV6ShotCount };
// v7 shot indices (DirectorCut::V7).
enum DirectorShotV7 { kV7NightWave, kV7Strike, kV7Picker, kV7PovUav, kV7PovMissile, kV7PovRover, kV7SoldierFire, kV7Dawn,
                      kV7HeroTank, kV7HeroHeli, kV7HeroAa, kV7RtRaster, kV7RtHybrid, kV7RtWhitted, kV7RtPath, kV7RtLight,
                      kV7RtTree, kV7ExRaster, kV7ExInterp, kV7ExReflect, kV7ExFinal, kV7Dusk, kV7Finale, kV7ShotCount };
// The ray-tracing showcase props (glass ball, mirror ball, glass block, chrome post, red block) stand here, east of the
// bunker entrance (same place as the raytrace_* report figures, app/CaptureRT.cpp).
constexpr float kV7PropsX = 5.2f, kV7PropsZ = 8.6f;
// Camera of the ray-tracing segments (eye, target, vertical fov) and the side view of the ray-debugger segment.
void directorRtCamera(Vec3& eye, Vec3& target, float& fovDeg, bool side = false);

// Roster order of the garrison (Garrison.h): the shots follow these soldiers.
enum DirectorSoldier { kSoldierSentry, kSoldierWatch, kSoldierGunner, kSoldierMechanic, kSoldierLogistics, kSoldierTechnician,
                       kSoldierPatrol, kSoldierRunner };

float directorLength(DirectorCut cut = DirectorCut::V6);  // seconds before the sequence loops (120)
int directorShotCount(DirectorCut cut = DirectorCut::V6);
float directorShotStart(int index, DirectorCut cut = DirectorCut::V6);   // seconds at which shot 'index' starts
const char* directorShotName(int index, DirectorCut cut = DirectorCut::V6);
// t = seconds since the director started
DirectorShot directorShot(const Scene& scene, float t, DirectorCut cut = DirectorCut::V6);
DirectorScript directorScript(float t, DirectorCut cut = DirectorCut::V6);  // scripted controls (and clock) at time t

// Walk path of the bunker shot at shot-local time 'u' in 0..1 (eye position and look yaw / pitch, degrees).
// 'space' may be null (a ramp profile from the site plan is used).
struct WalkPose { Vec3 eye; float yawDeg, pitchDeg; };
WalkPose directorWalkPose(const WalkSpace* space, float u);

}  // namespace ts
