// Cinematic director: a ~120 s sequence of authored camera shots over the whole base (flyover, radar, helicopter
// take-off, tank, AA gun, the walk down the ramp into the bunker, the UAV missile strike and explosion, pull-back).
// Used by the 'K' camera and the video recorder. The director also scripts the controllables: for every moment it
// can return a synthetic ControlInput for one slot (directorScript), which the App feeds to Scene::update.
#pragma once

#include "scene/Control.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

namespace ts {

struct DirectorShot {
    const char* name = "";
    Vec3 eye, target;
    float fovDeg = 45.0f;
    bool thermal = false;      // show this shot through the thermal camera palette
    bool uavCamera = false;    // look through the UAV's own camera (eye/target unused)
    bool fpvHud = false;       // draw the thermal first-person (FLIR) HUD over this shot
    bool walk = false;         // first-person walk through the bunker (eye height 1.7 m, WalkSpace floors)
    float fade = 1.0f;         // 0 at a cut, 1 inside the shot (for a short dip to black between shots)
    int script = -1;           // index of the shot in the sequence (see directorShotCount / directorShotStart)
};

// What the director does to the world at a moment: possess one controllable with a synthetic input.
struct DirectorScript {
    int slot = -1;             // Slot (Tank ... Uav) to possess, -1 = nobody
    ControlInput in;           // fire / actionPressed are LEVELS here: the App turns rising edges into presses
    bool strike = false;       // level: UAV fires a missile at the primary rover on the rising edge
};

float directorLength();                                  // seconds before the sequence loops (120)
int directorShotCount();
float directorShotStart(int index);                      // seconds at which shot 'index' starts
const char* directorShotName(int index);
DirectorShot directorShot(const Scene& scene, float t);  // t = seconds since the director started
DirectorScript directorScript(float t);                  // scripted controls at time t

// Walk path of the bunker shot at shot-local time 'u' in 0..1 (eye position and look yaw / pitch, degrees).
// 'space' may be null (a ramp profile from the site plan is used).
struct WalkPose { Vec3 eye; float yawDeg, pitchDeg; };
WalkPose directorWalkPose(const WalkSpace* space, float u);

}  // namespace ts
