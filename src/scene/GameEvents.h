// Discrete game events produced by the simulation each frame (waves, sightings, launches, blasts, kills,
// breaches...). The HUD turns them into banners / markers, the audio engine into sounds. Scene::events() holds
// the events of the last update() only; consumers must read them every frame.
#pragma once

#include <vector>

#include "math/Math3D.h"

namespace ts {

enum class GameEventType {
    WaveStart,        // value = wave number
    EnemySpotted,     // a new track became confirmed (track = id)
    LockOn,           // uav locked track (uav, track)
    RoundLaunched,    // uav released a round (uav, track); position = launch point
    CombinedStrike,   // both UAVs committed to one target (track); value = planned arrival spread in ms
    Explosion,        // any blast (position, value = power x 100)
    EnemyHit,         // a round damaged a rover without killing it (value = health left in %)
    EnemyDestroyed,   // track / rover destroyed (value = score awarded, combined = true if both rounds hit)
    Breach,           // a rover reached its objective (position, value = integrity lost in %)
    BaseLost,         // integrity reached 0, mission restarts
    CannonFire,       // tank gun (position)
    GatlingBurst,     // AA gun started firing (position)
    DoorMoving,       // blast doors / barrier started moving (position)
    // v6 day/night cycle (value = hour x 100)
    Dusk,             // sun sets below +4 deg: shift change starts
    NightFalls,       // sun below -6 deg: rover waves allowed
    Dawn,             // sun rises above -6 deg: surviving rovers withdraw
    DayBreak,         // sun above +4 deg: day shift starts
    ShiftChange,      // soldiers move to their next duty post (value = soldiers moving)
    Withdraw,         // a rover left the field at dawn (position)
    RifleFire,        // a soldier on duty fired at an intruder (position = muzzle)
};

struct GameEvent {
    GameEventType type = GameEventType::Explosion;
    Vec3 position;
    int uav = -1;        // 0 / 1 for UAV events
    int track = -1;      // track id for tracking / strike events
    int value = 0;
    bool combined = false;
    float time = 0.0f;   // scene time of the event
};

}  // namespace ts
