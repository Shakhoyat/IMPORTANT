// Weapons, blasts and the visual effect particles they leave behind. CPU only, deterministic (LCG).
#include "scene/Combat.h"

#include "objects/BaseLayout.h"
#include "objects/Terrain.h"

namespace ts {

namespace {

constexpr float kMaxSubstep = 1.0f / 120.0f;
constexpr float kGravity = 9.81f;
constexpr float kScorchLife = 120.0f;
constexpr size_t kScorchCap = 96;
const Vec3 kWind{0.8f * 0.6f, 0.0f, 0.45f * 0.6f};

float groundAt(float x, float z) {
    return (x * x + z * z < layout::kBaseRadius * layout::kBaseRadius) ? terrainHeight(x, z) : layout::kApronLevel;
}

// First parameter t in [0,1] where segment a->b enters the sphere (centre c, radius r); false if it misses.
bool segmentSphere(const Vec3& a, const Vec3& b, const Vec3& c, float r, float& tHit) {
    const Vec3 d = b - a, m = a - c;
    const float A = dot(d, d), B = dot(m, d), C = dot(m, m) - r * r;
    if (C <= 0.0f) { tHit = 0.0f; return true; }        // started inside
    if (A < 1e-12f) return false;
    const float disc = B * B - A * C;
    if (disc < 0.0f) return false;
    const float t = (-B - std::sqrt(disc)) / A;
    if (t < 0.0f || t > 1.0f) return false;
    tHit = t;
    return true;
}

// Entry parameter of a segment into a box; false if no hit.
bool segmentBoxT(const Vec3& a, const Vec3& b, const Box& box, float& tHit) {
    const float s[3] = {a.x, a.y, a.z}, d[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    const float lo[3] = {box.min.x, box.min.y, box.min.z}, hi[3] = {box.max.x, box.max.y, box.max.z};
    float t0 = 0.0f, t1 = 1.0f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-9f) {
            if (s[i] < lo[i] || s[i] > hi[i]) return false;
            continue;
        }
        float u = (lo[i] - s[i]) / d[i], v = (hi[i] - s[i]) / d[i];
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u);
        t1 = std::min(t1, v);
        if (t0 > t1) return false;
    }
    tHit = t0;
    return true;
}

bool lowPriority(EffectType t) { return t == EffectType::Smoke || t == EffectType::Trail || t == EffectType::Dust; }

}  // namespace

// ---------------------------------------------------------------------------------------------- spawning

void Combat::spawn(EffectType type, const Vec3& pos, const Vec3& vel, float size, float growth, float life, float heat,
                   float brightness) {
    EffectParticle p;
    p.type = type;
    p.position = pos;
    p.velocity = vel;
    p.size = size;
    p.growth = growth;
    p.life = life;
    p.heat = heat;
    // Emissive calibration for the night exposure (key ~ log2 L = -5.2): the effects shader reaches ~18x
    // 'brightness' in the hot core, so the authored values are scaled to land fire at a few HDR units.
    float k = 1.0f;
    switch (type) {
        case EffectType::Flash: k = 0.09f; break;
        case EffectType::Fire: k = 0.085f; break;
        case EffectType::Spark: k = 0.3f; break;
        case EffectType::Ember: k = 0.45f; break;
        default: break;
    }
    p.brightness = brightness * k;
    p.seed = rnd();
    particles_.push_back(p);
}

void Combat::emit(const EffectParticle& p) {
    particles_.push_back(p);
    if (particles_.size() > kParticleCap) enforceCap();
}

void Combat::enforceCap() {
    if (particles_.size() <= kParticleCap) return;
    size_t excess = particles_.size() - kParticleCap;
    std::vector<char> drop(particles_.size(), 0);
    for (size_t i = 0; i < particles_.size() && excess > 0; ++i) {   // oldest smoke / trail / dust first
        if (lowPriority(particles_[i].type)) { drop[i] = 1; --excess; }
    }
    for (size_t i = 0; i < particles_.size() && excess > 0; ++i) {   // then the oldest of anything
        if (!drop[i]) { drop[i] = 1; --excess; }
    }
    size_t w = 0;
    for (size_t i = 0; i < particles_.size(); ++i) {
        if (!drop[i]) particles_[w++] = particles_[i];
    }
    particles_.resize(w);
}

void Combat::addScorch(const Vec3& pos, float radius) {
    ScorchMark s;
    s.position = pos;
    s.radius = radius;
    s.age = 0.0f;
    scorches_.push_back(s);
    if (scorches_.size() > kScorchCap) scorches_.erase(scorches_.begin());
}

void Combat::muzzleFlash(const Vec3& pos, const Vec3& dir, float size) {
    const Vec3 d = normalize(dir);
    spawn(EffectType::Flash, pos + d * size * 0.3f, d * 2.0f, size * 0.6f, size * 4.0f, 0.06f, 1.0f, 6.0f);
    for (int i = 0; i < 2; ++i)
        spawn(EffectType::Fire, pos + d * size * 0.4f, d * rr(3.0f, 8.0f) + randomDir() * 0.6f, size * rr(0.25f, 0.4f),
              size * 1.5f, rr(0.08f, 0.16f), 1.0f, 3.0f);
    for (int i = 0; i < 3; ++i)
        spawn(EffectType::Smoke, pos + d * size * 0.5f, d * rr(1.0f, 3.0f) + randomDir() * 0.4f, size * 0.3f, size * 0.8f,
              rr(1.2f, 2.2f), 0.4f, 1.0f);
    for (int i = 0; i < 4; ++i)
        spawn(EffectType::Spark, pos + d * size * 0.3f, d * rr(8.0f, 20.0f) + randomDir() * 3.0f, 0.02f, 0.0f,
              rr(0.2f, 0.5f), 1.0f, 3.0f);
    flashes_.push_back({pos + d * size * 0.5f, 0.3f * std::min(size, 2.0f), 0.0f, 0.07f});
    if (particles_.size() > kParticleCap) enforceCap();
}

void Combat::emitDust(const Vec3& pos, float radius, int count) {
    for (int i = 0; i < count; ++i) {
        const float a = rr(0.0f, 6.2831853f), r = radius * std::sqrt(rnd());
        const Vec3 p = pos + Vec3{r * std::cos(a), 0.05f, r * std::sin(a)};
        const Vec3 v{std::cos(a) * rr(0.3f, 1.4f), rr(0.2f, 0.8f), std::sin(a) * rr(0.3f, 1.4f)};
        spawn(EffectType::Dust, p, v, rr(0.15f, 0.3f) * (0.5f + radius * 0.3f), rr(0.2f, 0.5f), rr(1.0f, 2.2f), 0.2f, 1.0f);
    }
    if (particles_.size() > kParticleCap) enforceCap();
}

void Combat::emitBurn(const Vec3& pos, float intensity, float dt) {
    const float k = std::max(0.0f, intensity);
    auto chance = [&](float rate) { return rnd() < rate * k * dt; };
    for (int i = 0; i < 2; ++i) {
        if (chance(14.0f))
            spawn(EffectType::Fire, pos + Vec3{rr(-0.3f, 0.3f), rr(0.0f, 0.2f), rr(-0.3f, 0.3f)},
                  Vec3{rr(-0.2f, 0.2f), rr(0.8f, 1.8f), rr(-0.2f, 0.2f)}, rr(0.18f, 0.35f), 0.35f, rr(0.5f, 1.0f), 1.0f, 2.2f);
        if (chance(10.0f))
            spawn(EffectType::Smoke, pos + Vec3{rr(-0.3f, 0.3f), rr(0.2f, 0.5f), rr(-0.3f, 0.3f)},
                  Vec3{rr(-0.2f, 0.2f), rr(1.0f, 1.8f), rr(-0.2f, 0.2f)}, rr(0.3f, 0.5f), rr(0.25f, 0.45f), rr(5.0f, 9.0f), 0.45f, 1.0f);
    }
    if (chance(3.0f))
        spawn(EffectType::Ember, pos + Vec3{0.0f, 0.2f, 0.0f}, Vec3{rr(-0.8f, 0.8f), rr(1.5f, 3.5f), rr(-0.8f, 0.8f)}, 0.03f, 0.0f,
              rr(1.5f, 3.0f), 1.0f, 2.5f);
    if (particles_.size() > kParticleCap) enforceCap();
}

// -------------------------------------------------------------------------------------------- explosions

void Combat::explode(const Vec3& pos, float power, int owner) { explodeImpl(pos, power, owner, -1); }

void Combat::explodeImpl(const Vec3& pos, float power, int owner, int directHit) {
    BlastEvent ev;
    ev.position = pos;
    ev.power = power;
    ev.owner = owner;
    ev.directHit = directHit;
    blasts_.push_back(ev);

    const float gy = groundAt(pos.x, pos.z);
    if (power < 0.3f) {  // tracer impact: a small puff of sparks and smoke
        spawn(EffectType::Flash, pos, Vec3{}, 0.15f, 2.0f, 0.06f, 1.0f, 4.0f);
        for (int i = 0; i < 2; ++i)
            spawn(EffectType::Fire, pos, randomDir() * 1.0f + Vec3{0, 0.8f, 0}, 0.1f, 0.5f, rr(0.15f, 0.3f), 1.0f, 2.0f);
        spawn(EffectType::Smoke, pos, Vec3{0, 0.8f, 0}, 0.12f, 0.2f, rr(1.0f, 2.0f), 0.4f, 1.0f);
        for (int i = 0; i < 5; ++i)
            spawn(EffectType::Spark, pos, randomDir() * rr(3.0f, 9.0f) + Vec3{0, 2.0f, 0}, 0.02f, 0.0f, rr(0.3f, 0.8f), 1.0f, 3.0f);
        spawn(EffectType::Dust, Vec3{pos.x, std::max(gy, pos.y), pos.z}, Vec3{0, 0.5f, 0}, 0.1f, 0.3f, 1.0f, 0.2f, 1.0f);
        flashes_.push_back({pos, power * 2.0f, 0.0f, 0.15f});
        if (particles_.size() > kParticleCap) enforceCap();
        return;
    }

    const float sc = clampf(power, 0.3f, 2.0f);
    const float sq = std::sqrt(sc);
    // detonation flash
    spawn(EffectType::Flash, pos, Vec3{}, 1.4f * sq, 5.0f * sq, 0.12f, 1.0f, 10.0f);
    // fireball
    const int nFire = static_cast<int>((10 + 15 * rnd()) * std::min(sc, 1.5f));
    for (int i = 0; i < nFire; ++i) {
        const Vec3 d = randomDir();
        spawn(EffectType::Fire, pos + d * 0.3f * sq, d * rr(1.5f, 5.0f) * sq + Vec3{0, rr(1.0f, 3.0f), 0}, rr(0.5f, 1.0f) * sq,
              rr(0.8f, 1.8f) * sq, rr(0.5f, 1.3f), 1.0f, rr(2.0f, 3.5f));
    }
    // smoke column
    const int nSmoke = static_cast<int>((15 + 25 * rnd()) * std::min(sc, 1.5f));
    for (int i = 0; i < nSmoke; ++i) {
        const Vec3 d = randomDir();
        spawn(EffectType::Smoke, pos + Vec3{d.x, std::fabs(d.y), d.z} * 0.5f * sq,
              Vec3{d.x * 1.0f, rr(1.2f, 3.5f), d.z * 1.0f}, rr(0.5f, 1.0f) * sq, rr(0.25f, 0.55f) * sq, rr(6.0f, 12.0f), 0.55f, 1.0f);
    }
    // sparks and embers
    const int nSpark = static_cast<int>((30 + 30 * rnd()) * std::min(sc, 1.5f));
    for (int i = 0; i < nSpark; ++i) {
        const Vec3 d = randomDir();
        spawn(EffectType::Spark, pos, Vec3{d.x, std::fabs(d.y) * 0.8f + 0.2f, d.z} * rr(6.0f, 20.0f) * sq, 0.025f, 0.0f,
              rr(0.5f, 1.6f), 1.0f, 3.0f);
    }
    const int nEmber = static_cast<int>((8 + 7 * rnd()) * std::min(sc, 1.5f));
    for (int i = 0; i < nEmber; ++i) {
        const Vec3 d = randomDir();
        spawn(EffectType::Ember, pos, Vec3{d.x * 3.0f, rr(3.0f, 9.0f), d.z * 3.0f} * sq, 0.04f, 0.0f, rr(2.0f, 4.5f), 1.0f, 2.5f);
    }
    // shock wave ring on the ground and kicked-up dust
    spawn(EffectType::Shockwave, Vec3{pos.x, std::max(gy, std::min(pos.y, gy + 0.3f)) + 0.05f, pos.z}, Vec3{}, 0.5f, 14.0f * sq, 0.7f,
          0.8f, 1.0f);
    const int nDust = static_cast<int>((10 + 10 * rnd()) * std::min(sc, 1.5f));
    for (int i = 0; i < nDust; ++i) {
        const float a = rr(0.0f, 6.2831853f);
        const float sp = rr(2.0f, 6.0f) * sq;
        spawn(EffectType::Dust, Vec3{pos.x, gy + 0.1f, pos.z}, Vec3{std::cos(a) * sp, rr(0.3f, 1.2f), std::sin(a) * sp},
              rr(0.3f, 0.6f) * sq, rr(0.6f, 1.2f), rr(2.0f, 4.0f), 0.3f, 1.0f);
    }
    flashes_.push_back({pos + Vec3{0, 0.3f, 0}, power, 0.0f, 0.6f});
    shakes_.push_back({pos, power, 0.0f});
    addScorch(Vec3{pos.x, gy, pos.z}, 0.9f * power);
    if (particles_.size() > kParticleCap) enforceCap();
}

float Combat::shake(const Vec3& cameraPos) const {
    float s = 0.0f;
    for (const Shake& k : shakes_) {
        const float d = length(cameraPos - k.position);
        if (d >= kShakeRange || k.age >= kShakeDecay) continue;
        const float f = 1.0f - d / kShakeRange;
        const float t = 1.0f - k.age / kShakeDecay;
        s += std::min(1.0f, k.power) * f * f * t;
    }
    return clampf(s, 0.0f, 1.0f);
}

std::vector<BlastEvent> Combat::takeBlasts() {
    std::vector<BlastEvent> out;
    out.swap(blasts_);
    return out;
}

// ----------------------------------------------------------------------------------------------- weapons

void Combat::setObstacles(const std::vector<Box>& boxes) { obstacles_ = boxes; }
void Combat::setTargets(const std::vector<Target>& targets) { targets_ = targets; }

void Combat::setDesignator(int id, const Vec3& point, bool valid) {
    for (Designator& d : designators_) {
        if (d.id == id) { d.point = point; d.valid = valid; return; }
    }
    designators_.push_back({id, point, valid});
}

void Combat::fireMissile(const Vec3& pos, const Vec3& dir, int designator, int owner) {
    Projectile p;
    p.kind = ProjectileKind::Missile;
    p.position = p.previous = pos;
    p.velocity = normalize(dir) * 8.0f;
    p.owner = owner;
    p.designator = designator;
    projectiles_.push_back(p);
    trailAcc_.push_back(0.0f);
    for (int i = 0; i < 4; ++i)  // ignition puff
        spawn(EffectType::Smoke, pos, normalize(dir) * rr(-1.5f, 0.0f) + randomDir() * 0.5f, 0.15f, 0.6f, rr(1.0f, 1.8f), 0.5f, 1.0f);
    flashes_.push_back({pos, 0.15f, 0.0f, 0.12f});
}

void Combat::fireShell(const Vec3& pos, const Vec3& dir, float speed, int owner) {
    Projectile p;
    p.kind = ProjectileKind::TankShell;
    p.position = p.previous = pos;
    p.velocity = normalize(dir) * (speed > 0.0f ? speed : kShellSpeed);
    p.owner = owner;
    projectiles_.push_back(p);
    trailAcc_.push_back(0.0f);
}

void Combat::fireTracer(const Vec3& pos, const Vec3& dir, float speed, int owner) {
    Projectile p;
    p.kind = ProjectileKind::Tracer;
    p.position = p.previous = pos;
    p.velocity = normalize(dir) * (speed > 0.0f ? speed : kTracerSpeed);
    p.owner = owner;
    projectiles_.push_back(p);
    trailAcc_.push_back(0.0f);
}

bool Combat::stepProjectile(Projectile& p, float& trail, float dt, Vec3& at, float& power, int& target, bool& blast) {
    blast = false;
    target = -1;
    p.previous = p.position;
    p.age += dt;
    const float fuse = (p.kind == ProjectileKind::Missile) ? kMissileFuse : 0.0f;

    if (p.kind == ProjectileKind::Missile) {
        // speed: 8 m/s -> 60 m/s linearly in 0.4 s
        const float speed = lerpf(8.0f, kMissileMaxSpeed, clampf(p.age / kMissileRampTime, 0.0f, 1.0f));
        Vec3 v = normalize(p.velocity);
        const Designator* des = nullptr;
        for (const Designator& d : designators_) {
            if (d.id == p.designator && d.valid) { des = &d; break; }
        }
        if (des) {
            const Vec3 r = des->point - p.position;
            const float dist = std::max(0.05f, length(r));
            const Vec3 los = r * (1.0f / dist);
            // proportional navigation: omega = N * (v x r) / |r|^2 * |v| / |v| (static target) + heading term
            Vec3 omega = cross(v, r) * (4.0f * speed / (dist * dist)) + cross(v, los) * 2.0f;
            const float w = length(omega);
            if (w > kMissileTurnRate) omega = omega * (kMissileTurnRate / w);
            const float ang = length(omega) * dt;
            if (ang > 1e-7f) {
                const Vec3 k = normalize(omega);   // Rodrigues rotation of v about k by ang
                v = v * std::cos(ang) + cross(k, v) * std::sin(ang) + k * (dot(k, v) * (1.0f - std::cos(ang)));
                v = normalize(v);
            }
        }
        p.velocity = v * speed;
    } else if (p.kind == ProjectileKind::TankShell) {
        p.velocity.y -= kGravity * dt;
    }
    p.position = p.position + p.velocity * dt;

    // exhaust puffs and smoke every 0.03 s (shells every 0.06 s)
    if (p.kind != ProjectileKind::Tracer) {
        trail += dt;
        const float every = p.kind == ProjectileKind::Missile ? 0.03f : 0.06f;
        while (trail >= every) {
            trail -= every;
            const Vec3 back = normalize(p.velocity) * -0.3f;
            spawn(EffectType::Trail, p.position + back, normalize(p.velocity) * -1.0f + randomDir() * 0.15f,
                  p.kind == ProjectileKind::Missile ? 0.1f : 0.07f, 0.35f, rr(1.0f, 2.0f), 0.35f, 1.0f);
        }
    }

    // ---- impacts (segment tests: no tunnelling) ----
    float bestT = 2.0f;
    int hitTarget = -1;
    bool hitSomething = false;
    for (const Target& t : targets_) {
        if (!t.alive) continue;
        float th;
        if (segmentSphere(p.previous, p.position, t.centre, t.radius + fuse, th) && th < bestT) {
            bestT = th; hitTarget = t.id; hitSomething = true;
        }
    }
    for (const Box& b : obstacles_) {
        float th;
        if (segmentBoxT(p.previous, p.position, b, th) && th < bestT) {
            bestT = th; hitTarget = -1; hitSomething = true;
        }
    }
    if (p.kind == ProjectileKind::Missile) {
        for (const Designator& d : designators_) {
            if (d.id != p.designator || !d.valid) continue;
            float th;
            if (segmentSphere(p.previous, p.position, d.point, fuse, th) && th < bestT) { bestT = th; hitTarget = -1; hitSomething = true; }
            // closest-approach fuse: flying past the spot within a few metres
            const Vec3 r = d.point - p.position;
            if (!hitSomething && dot(r, p.velocity) < 0.0f && length(r) < 3.0f) { bestT = 1.0f; hitSomething = true; }
        }
    }
    // ground (terrain for r < 36)
    {
        const float g0 = groundAt(p.previous.x, p.previous.z), g1 = groundAt(p.position.x, p.position.z);
        const float h0 = p.previous.y - g0, h1 = p.position.y - g1;
        if (h1 <= fuse * 0.0f && h0 > 0.0f) {
            const float th = h0 / std::max(1e-6f, h0 - h1);
            if (th < bestT) { bestT = th; hitTarget = -1; hitSomething = true; }
        } else if (h1 <= 0.0f && h0 <= 0.0f && bestT > 1.0f) {
            bestT = 0.0f; hitTarget = -1; hitSomething = true;
        }
        // Height-above-ground fuse only for rounds without a valid laser spot: a guided round uses the spot
        // fuse (closest approach), so a shallow dive does not burst half a metre short of the target.
        bool guided = false;
        for (const Designator& d : designators_) guided = guided || (d.id == p.designator && d.valid);
        if (p.kind == ProjectileKind::Missile && !guided && h1 < kMissileFuse && p.velocity.y < 0.0f && bestT > 1.0f &&
            p.age > kMissileRampTime) {
            bestT = 1.0f; hitTarget = -1; hitSomething = true;   // proximity fuse to the ground
        }
    }
    if (hitSomething) {
        at = lerp(p.previous, p.position, clampf(bestT, 0.0f, 1.0f));
        target = hitTarget;
        blast = true;
        power = p.kind == ProjectileKind::Missile ? 1.0f : (p.kind == ProjectileKind::TankShell ? 1.6f : 0.15f);
        return true;
    }
    if (p.kind == ProjectileKind::Missile && p.age >= kMissileLife) {   // self destruct
        at = p.position; blast = true; power = 1.0f; return true;
    }
    if (p.kind == ProjectileKind::Tracer && p.age >= kTracerLife) return true;   // burnt out, no blast
    if (p.kind == ProjectileKind::TankShell && p.age > 20.0f) return true;
    return false;
}

// --------------------------------------------------------------------------------------------- stepping

void Combat::update(float dt, float time) {
    time_ = time;
    if (dt <= 0.0f) return;
    const int n = std::max(1, static_cast<int>(std::ceil(dt / kMaxSubstep)));
    const float h = dt / n;
    for (int s = 0; s < n; ++s) {
        for (size_t i = 0; i < projectiles_.size();) {
            Vec3 at;
            float power = 0.0f;
            int target = -1;
            bool blast = false;
            Projectile& p = projectiles_[i];
            if (stepProjectile(p, trailAcc_[i], h, at, power, target, blast)) {
                const int owner = p.owner;
                projectiles_.erase(projectiles_.begin() + static_cast<long>(i));
                trailAcc_.erase(trailAcc_.begin() + static_cast<long>(i));
                if (blast) explodeImpl(at, power, owner, target);
            } else {
                ++i;
            }
        }
    }
    updateParticles(dt);

    for (ScorchMark& sm : scorches_) sm.age += dt;
    scorches_.erase(std::remove_if(scorches_.begin(), scorches_.end(), [](const ScorchMark& m) { return m.age > kScorchLife; }),
                    scorches_.end());
    for (Shake& k : shakes_) k.age += dt;
    shakes_.erase(std::remove_if(shakes_.begin(), shakes_.end(), [](const Shake& k) { return k.age > kShakeDecay; }), shakes_.end());
    for (FlashLight& f : flashes_) f.age += dt;
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const FlashLight& f) { return f.age >= f.life; }),
                   flashes_.end());

    lights_.clear();
    for (const FlashLight& f : flashes_) {
        const float t = clampf(1.0f - f.age / f.life, 0.0f, 1.0f);
        DynamicLight l;
        l.position = f.position;
        l.color = Vec3{1.0f, 0.5f, 0.15f} * (14.0f * f.power * t * t);
        l.range = 9.0f * f.power;
        lights_.push_back(l);
    }
    for (const Projectile& p : projectiles_) {
        if (p.kind != ProjectileKind::Missile) continue;
        DynamicLight l;
        l.position = p.position - normalize(p.velocity) * 0.3f;
        l.color = Vec3{1.0f, 0.55f, 0.2f} * 12.0f;
        l.range = 7.0f;
        lights_.push_back(l);
    }
    for (const DynamicLight& l : extraLights_) lights_.push_back(l);
    if (particles_.size() > kParticleCap) enforceCap();
}

void Combat::updateParticles(float dt) {
    for (EffectParticle& p : particles_) {
        p.age += dt;
        switch (p.type) {
            case EffectType::Fire:
                p.velocity.y += 2.0f * dt;                                     // hot gas rises
                p.velocity = p.velocity * std::exp(-1.8f * dt);
                break;
            case EffectType::Smoke:
            case EffectType::Trail: {
                const float rise = p.type == EffectType::Smoke ? 0.9f : 0.2f;
                p.velocity.y += rise * dt;                                      // buoyant
                if (p.velocity.y > 3.0f) p.velocity.y = 3.0f;
                const float k = 1.0f - std::exp(-0.6f * dt);                    // drift with the wind
                p.velocity.x += (kWind.x - p.velocity.x) * k;
                p.velocity.z += (kWind.z - p.velocity.z) * k;
                break;
            }
            case EffectType::Spark:
                p.velocity.y -= kGravity * dt;
                p.velocity = p.velocity * std::exp(-0.4f * dt);
                break;
            case EffectType::Ember:
                p.velocity.y -= 0.6f * kGravity * dt;
                p.velocity = p.velocity * std::exp(-0.3f * dt);
                break;
            case EffectType::Dust:
                p.velocity = p.velocity * std::exp(-1.6f * dt);
                p.velocity.y += 0.15f * dt;
                break;
            default: break;
        }
        p.position = p.position + p.velocity * dt;
        if (p.type == EffectType::Spark || p.type == EffectType::Ember) {
            const float g = groundAt(p.position.x, p.position.z) + 0.02f;
            if (p.position.y < g) {
                p.position.y = g;
                p.velocity.y = -p.velocity.y * 0.25f;
                p.velocity.x *= 0.6f;
                p.velocity.z *= 0.6f;
            }
        }
        p.size += p.growth * dt * (p.type == EffectType::Smoke || p.type == EffectType::Trail || p.type == EffectType::Dust
                                       ? std::exp(-0.25f * p.age) : 1.0f);
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const EffectParticle& p) { return p.age >= p.life; }),
                     particles_.end());
}

}  // namespace ts
