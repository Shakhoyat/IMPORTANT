// Weapons and explosions: projectiles (UAV missiles, tank shells, AA tracers), blasts with damage,
// debris pieces, and the visual effect particles / lights / scorch marks they produce.
// CPU simulation only (unit-testable); rendering/Effects draws effects().
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ai/Occlusion.h"
#include "scene/EffectsData.h"

namespace ts {

struct Target {            // something that can be hit (rovers; the scene refreshes the list every frame)
    int id = -1;
    Vec3 centre;
    float radius = 0.4f;
    bool alive = true;
};

struct BlastEvent {        // an explosion that happened since the last takeBlasts()
    Vec3 position;
    float power = 1.0f;    // 1 = UAV missile, ~1.6 tank shell, 0.15 tracer impact
    int owner = -1;
    int directHit = -1;    // target id hit by the projectile itself, -1 if none
};

class Combat {
public:
    // --- weapons (owner ids: 0 = UAV, 1.. = controllables in scene order) ---
    void fireMissile(const Vec3& pos, const Vec3& dir, int designator, int owner);  // homes on the designator
    void fireShell(const Vec3& pos, const Vec3& dir, float speed, int owner);       // ballistic, tank gun
    void fireTracer(const Vec3& pos, const Vec3& dir, float speed, int owner);      // AA gun round
    void muzzleFlash(const Vec3& pos, const Vec3& dir, float size);
    void emitDust(const Vec3& pos, float radius, int count);   // vehicle tracks, rotor wash, impacts
    // Laser spot that guided missiles home on (the UAV keeps it on the target with its gimbal camera).
    void setDesignator(int id, const Vec3& point, bool valid);
    // Fireball, smoke column, sparks, embers, shock wave, light flash, scorch mark; damage within ~1.5*power m.
    void explode(const Vec3& pos, float power, int owner);

    // --- additions (C1) ---
    void emit(const EffectParticle& p);                                   // raw particle (wrecks, test hooks)
    void emitBurn(const Vec3& pos, float intensity, float dt);            // burning wreck: fire + smoke + embers (rate-based)
    void addScorch(const Vec3& pos, float radius);
    void setExtraLights(const std::vector<DynamicLight>& lights) { extraLights_ = lights; }  // appended to lights()
    float time() const { return time_; }
    // v6: seed of the effects' random stream (0 = the fixed legacy seed).
    void setSeed(uint32_t seed) { rng_ = seed ? seed : 0x2468ace1u; }
    int projectileCount() const { return static_cast<int>(projectiles_.size()); }
    static constexpr size_t kParticleCap = 4000;
    static constexpr float kMissileMaxSpeed = 60.0f, kMissileRampTime = 0.4f, kMissileLife = 6.0f;
    static constexpr float kMissileTurnRate = 6.0f;      // rad/s, limited manoeuvrability
    static constexpr float kMissileFuse = 0.5f;          // proximity fuse radius, m
    static constexpr float kShellSpeed = 120.0f, kTracerSpeed = 1000.0f, kTracerLife = 2.5f;
    static constexpr float kDamageRadiusPerPower = 1.6f; // blast damage radius = 1.6 * power (m)
    static constexpr float kShakeRange = 25.0f, kShakeDecay = 0.8f;

    // --- world ---
    void setObstacles(const std::vector<Box>& boxes);   // static solids: projectiles detonate on them
    void setTargets(const std::vector<Target>& targets);
    void update(float dt, float time);                  // moves everything, detects impacts
    std::vector<BlastEvent> takeBlasts();

    // --- read-only state (renderer, HUD, tests) ---
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<EffectParticle>& particles() const { return particles_; }
    const std::vector<DynamicLight>& lights() const { return lights_; }
    const std::vector<ScorchMark>& scorches() const { return scorches_; }
    float shake(const Vec3& cameraPos) const;           // camera shake 0..1 from recent nearby blasts

private:
    std::vector<Projectile> projectiles_;
    std::vector<EffectParticle> particles_;
    std::vector<DynamicLight> lights_;
    std::vector<ScorchMark> scorches_;
    struct Designator { int id; Vec3 point; bool valid; };
    struct FlashLight { Vec3 position; float power; float age; float life; };
    struct Shake { Vec3 position; float power; float age; };
    void explodeImpl(const Vec3& pos, float power, int owner, int directHit);
    // returns true when the projectile has ended (detonated or burnt out); 'at' / 'power' / 'target' describe the blast
    bool stepProjectile(Projectile& p, float& trail, float dt, Vec3& at, float& power, int& target, bool& blast);
    void spawn(EffectType type, const Vec3& pos, const Vec3& vel, float size, float growth, float life, float heat,
               float brightness);
    void updateParticles(float dt);
    void enforceCap();
    float rnd() { rng_ = rng_ * 1664525u + 1013904223u; return static_cast<float>(rng_ >> 8) / 16777216.0f; }
    float rr(float lo, float hi) { return lo + (hi - lo) * rnd(); }
    Vec3 randomDir() {
        const float z = rr(-1.0f, 1.0f), a = rr(0.0f, 6.2831853f), s = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return {s * std::cos(a), z, s * std::sin(a)};
    }

    std::vector<float> trailAcc_;   // per projectile: time since the last exhaust puff (parallel to projectiles_)
    std::vector<Designator> designators_;
    std::vector<Box> obstacles_;
    std::vector<Target> targets_;
    std::vector<BlastEvent> blasts_;
    std::vector<FlashLight> flashes_;
    std::vector<Shake> shakes_;
    std::vector<DynamicLight> extraLights_;
    uint32_t rng_ = 0x2468ace1u;
    float time_ = 0.0f;
    float burnAcc_ = 0.0f;
};

}  // namespace ts
