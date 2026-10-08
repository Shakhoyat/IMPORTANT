#include "scene/Mission.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "objects/BaseLayout.h"

namespace ts {

Mission::Mission(uint32_t seed) : rng_(seed) {
    st_.betweenWaves = true;
    st_.breakTimer = kFirstWaveDelay;
}

void Mission::configure(const MissionConfig& cfg) {
    cfg_ = cfg;
    legacyFirst_ = legacyFirst_ && cfg.legacyFirst;
}

float Mission::rnd() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>(rng_ >> 8) / 16777216.0f;
}

std::vector<Vec3> Mission::objectives() {
    return std::vector<Vec3>(std::begin(layout::kObjectives), std::end(layout::kObjectives));
}

void Mission::startWave(std::vector<GameEvent>& events, float time) {
    ++st_.wave;
    st_.toSpawn = std::min(2 + st_.wave, layout::kMaxRovers);
    st_.spawned = st_.resolved = 0;
    st_.spawnTimer = 0.0f;
    st_.betweenWaves = false;
    GameEvent ev;
    ev.type = GameEventType::WaveStart;
    ev.value = st_.wave;
    ev.time = time;
    events.push_back(ev);
}

void Mission::update(float dt, int alive, int freeSlots, std::vector<SpawnRequest>& spawns,
                     std::vector<GameEvent>& events, float time) {
    if (st_.betweenWaves) {
        st_.breakTimer -= dt;
        if (st_.breakTimer <= 0.0f) startWave(events, time);
        return;
    }
    st_.spawnTimer -= dt;
    if (st_.spawned < st_.toSpawn && st_.spawnTimer <= 0.0f && freeSlots > 0) {
        spawns.push_back(nextSpawn());
        ++st_.spawned;
        st_.spawnTimer = kSpawnInterval;
    }
    if (st_.spawned >= st_.toSpawn && st_.resolved >= st_.toSpawn && alive == 0) {
        st_.betweenWaves = true;
        st_.breakTimer = kWaveBreak;
    }
}

SpawnRequest Mission::nextSpawn() {
    SpawnRequest r;
    if (cfg_.randomRoutes && !legacyFirst_) {
        randomRoute(r);
        return r;
    }
    if (legacyFirst_) {
        r.position = {6.0f, 0.0f, 7.5f};
        r.yaw = 0.0f;
        legacyFirst_ = false;
    } else {
        // Random point on the compound edge, avoiding the gate road (north, +Z) where the camp would see it.
        float a = 0.0f;
        for (int tries = 0; tries < 20; ++tries) {
            a = rnd() * 2.0f * PI;
            const float fromNorth = std::fabs(std::atan2(std::cos(a), std::sin(a)));  // 0 = +Z
            if (fromNorth > radians(kNorthGapDeg)) break;
        }
        r.position = {layout::kSpawnRadius * std::cos(a), 0.0f, layout::kSpawnRadius * std::sin(a)};
        r.yaw = std::atan2(-r.position.x, -r.position.z);
    }
    const int n = static_cast<int>(std::size(layout::kObjectives));
    r.objective = layout::kObjectives[std::min(n - 1, static_cast<int>(rnd() * n))];
    r.seed = 1u + static_cast<uint32_t>(rnd() * 1e6f);
    return r;
}

void Mission::randomRoute(SpawnRequest& r) {
    // Uniform bearing on the ring (rejection: never within kNorthGapDeg of the gate road), jittered radius.
    float a = 0.0f;
    for (int tries = 0; tries < 64; ++tries) {
        a = rnd() * 2.0f * PI;
        const float fromNorth = std::fabs(std::atan2(std::cos(a), std::sin(a)));  // 0 = +Z
        if (fromNorth > radians(kNorthGapDeg)) break;
    }
    const float radius = layout::kSpawnRadius + (2.0f * rnd() - 1.0f) * kSpawnJitter;
    r.position = {radius * std::cos(a), 0.0f, radius * std::sin(a)};
    r.yaw = std::atan2(-r.position.x, -r.position.z);
    const int n = static_cast<int>(std::size(layout::kObjectives));
    const int obj = std::min(n - 1, static_cast<int>(rnd() * n));
    r.objective = layout::kObjectives[obj];
    r.seed = 1u + static_cast<uint32_t>(rnd() * 1e6f);
    r.randomRoute = true;

    // 0-3 via points in a +-70 deg sector round the spawn bearing (rejection-sampled on free cells, away from
    // the spawn and the objective), driven outermost first so the chain closes in on the camp.
    const int count = std::min(kMaxVia, static_cast<int>(rnd() * (kMaxVia + 1)));
    for (int k = 0; k < count; ++k) {
        for (int tries = 0; tries < 40; ++tries) {
            const float va = a + (2.0f * rnd() - 1.0f) * radians(kViaSectorDeg);
            const float vr = kViaMinR + rnd() * (kViaMaxR - kViaMinR);
            const Vec3 p{vr * std::cos(va), 0.0f, vr * std::sin(va)};
            const Vec3 dObj{p.x - r.objective.x, 0.0f, p.z - r.objective.z}, dSp{p.x - r.position.x, 0.0f, p.z - r.position.z};
            if (length(dObj) < 3.0f || length(dSp) < 3.0f) continue;
            if (cfg_.freeCell && !cfg_.freeCell(p)) continue;
            r.via.push_back(p);
            break;
        }
    }
    std::sort(r.via.begin(), r.via.end(), [](const Vec3& x, const Vec3& y) {
        return x.x * x.x + x.z * x.z > y.x * y.x + y.z * y.z;
    });

    // Personality.
    RoverPersonality& p = r.personality;
    p.noiseWeight = 8.0f + 32.0f * rnd();
    p.exposureWeight = 12.0f + 36.0f * rnd();
    p.speedScale = 0.8f + 0.45f * rnd();
    p.pauseInCover = 0.5f * rnd();
    p.noiseCells = 3 + std::min(4, static_cast<int>(rnd() * 5.0f));
    // v8 natural motion, hashed from the route seed (no extra draws: the stream above is unchanged).
    {
        uint32_t h = r.seed * 2654435761u ^ 0x9E3779B9u;
        auto hr = [&h] {
            h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
            return static_cast<float>(h >> 8) / 16777216.0f;
        };
        p.speedWander = 0.15f + 0.20f * hr();
        p.weave = 0.15f + 0.30f * hr();
        p.weaveWavelength = 3.5f + 3.5f * hr();
        p.hesitate = 0.02f + 0.05f * hr();
        p.detour = 0.15f + 0.25f * hr();
    }

    // Objective switch (a different camp asset) part-way along the route.
    if (rnd() < kSwitchProbability) {
        const int other = (obj + 1 + std::min(n - 2, static_cast<int>(rnd() * (n - 1)))) % n;
        r.altObjective = layout::kObjectives[other];
        r.switchAfter = kSwitchMinS + (kSwitchMaxS - kSwitchMinS) * rnd();
    }
}

void Mission::endNight(std::vector<GameEvent>&, float) {
    ++st_.nightsEnded;
    st_.betweenWaves = true;
    st_.breakTimer = kFirstWaveDelay;  // the next night opens with a new wave straight away
    st_.toSpawn = st_.spawned;
    legacyFirst_ = false;
}

void Mission::onWithdraw(const Vec3& where, std::vector<GameEvent>& events, float time) {
    ++st_.withdrawn;
    GameEvent ev;
    ev.type = GameEventType::Withdraw;
    ev.position = where;
    ev.time = time;
    events.push_back(ev);
}

void Mission::onKill(bool combined, const Vec3& where, std::vector<GameEvent>& events, float time) {
    ++st_.kills;
    ++st_.resolved;
    int award = kKillScore;
    if (combined) {
        ++st_.combinedKills;
        award += kCombinedBonus;
    }
    st_.score += award;
    GameEvent ev;
    ev.type = GameEventType::EnemyDestroyed;
    ev.position = where;
    ev.value = award;
    ev.combined = combined;
    ev.time = time;
    events.push_back(ev);
}

void Mission::onBreach(const Vec3& where, std::vector<GameEvent>& events, float time) {
    ++st_.breaches;
    ++st_.resolved;
    st_.integrity = std::max(0.0f, st_.integrity - kBreachDamage);
    GameEvent ev;
    ev.type = GameEventType::Breach;
    ev.position = where;
    ev.value = static_cast<int>(kBreachDamage);
    ev.time = time;
    events.push_back(ev);
    if (st_.integrity <= 0.0f) {
        ++st_.basesLost;
        GameEvent lost;
        lost.type = GameEventType::BaseLost;
        lost.position = where;
        lost.time = time;
        events.push_back(lost);
        const int lostCount = st_.basesLost;
        const int score = st_.score;
        st_ = MissionState{};
        st_.basesLost = lostCount;
        st_.score = score;  // the score survives a lost base (it is the session's score)
        st_.betweenWaves = true;
        st_.breakTimer = kWaveBreak;
    }
}

}  // namespace ts
