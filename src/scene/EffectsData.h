// Data the combat simulation (scene/Combat) hands to the effects renderer (rendering/Effects) and the HUD.
#pragma once

#include <vector>

#include "math/Math3D.h"

namespace ts {

enum class ProjectileKind { Missile = 0, TankShell = 1, Tracer = 2 };

struct Projectile {
    ProjectileKind kind = ProjectileKind::Tracer;
    Vec3 position, velocity;
    Vec3 previous;             // position one step ago (motion streak)
    float age = 0.0f;
    int owner = -1;            // who fired it (no self hits)
    int designator = -1;       // missiles: id of the laser spot they home on (-1 = unguided)
    bool alive = true;
};

// Instanced effect sprites; rendering/Effects.cpp decides the look of each type.
enum class EffectType {
    Fire = 0,       // fireball puff: emissive, additive, orange -> red -> dark
    Smoke = 1,      // lit, alpha blended, grows, dark grey (explosion) or light grey (exhaust trail)
    Spark = 2,      // tiny hot streak along velocity, additive
    Flash = 3,      // very short bright sphere (detonation, muzzle flash)
    Shockwave = 4,  // expanding ring on the ground, faint
    Trail = 5,      // missile / shell exhaust smoke puff (light grey, thin)
    Dust = 6,       // brown dust kicked up by blasts, vehicles, rotor wash (lit, alpha)
    Ember = 7,      // glowing debris speck that falls and cools
};

struct EffectParticle {
    EffectType type = EffectType::Smoke;
    Vec3 position, velocity;
    float size = 0.5f;         // radius (m) at age 0
    float growth = 0.0f;       // radius growth, m/s
    float age = 0.0f, life = 1.0f;
    float heat = 0.5f;         // apparent temperature for the thermal view at age 0 (0..1)
    float brightness = 1.0f;   // emissive scale for fire / flash / spark / ember
    float seed = 0.0f;         // 0..1, per-particle variation
};

struct DynamicLight {          // explosion flashes, burning wrecks, rocket motors, muzzle flashes
    Vec3 position;
    Vec3 color;                // linear HDR colour already multiplied by the intensity
    float range = 6.0f;        // metres (smooth cut-off)
    Vec3 direction{0.0f, 0.0f, 1.0f};  // spot axis (searchlights)
    float coneCos = -2.0f;     // cos(half angle); -2 = omni-directional
};

struct ScorchMark {            // blackened, hot ground after a blast; cools and fades
    Vec3 position;
    float radius = 1.0f;
    float age = 0.0f;
};

}  // namespace ts
