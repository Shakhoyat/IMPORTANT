// Cinematic director: a looping sequence of authored camera shots (crane, dolly, chase, ground level,
// UAV thermal feed, watchtower point of view, pull-back). Used by the 'K' camera and the video recorder.
#pragma once

#include "scene/Scene.h"

namespace ts {

struct DirectorShot {
    const char* name = "";
    Vec3 eye, target;
    float fovDeg = 45.0f;
    bool thermal = false;      // show this shot through the thermal camera palette
    bool uavCamera = false;    // look through the UAV's own camera (eye/target unused)
    float fade = 1.0f;         // 0 at a cut, 1 inside the shot (for a short dip to black between shots)
};

float directorLength();                              // seconds before the sequence loops
DirectorShot directorShot(const Scene& scene, float t);  // t = seconds since the director started

}  // namespace ts
