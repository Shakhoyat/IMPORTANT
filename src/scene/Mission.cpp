#include "scene/Mission.h"

#include <algorithm>
#include <cmath>

#include "objects/BaseLayout.h"

namespace ts {

Mission::Mission(uint32_t seed) : rng_(seed) {
    st_.betweenWaves = true;
    st_.breakTimer = kFirstWaveDelay;
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
        SpawnRequest r;
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
                if (fromNorth > radians(18.0f)) break;
            }
            r.position = {layout::kSpawnRadius * std::cos(a), 0.0f, layout::kSpawnRadius * std::sin(a)};
            r.yaw = std::atan2(-r.position.x, -r.position.z);
        }
        const int n = static_cast<int>(std::size(layout::kObjectives));
        r.objective = layout::kObjectives[std::min(n - 1, static_cast<int>(rnd() * n))];
        r.seed = 1u + static_cast<uint32_t>(rnd() * 1e6f);
        spawns.push_back(r);
        ++st_.spawned;
        st_.spawnTimer = kSpawnInterval;
    }
    if (st_.spawned >= st_.toSpawn && st_.resolved >= st_.toSpawn && alive == 0) {
        st_.betweenWaves = true;
        st_.breakTimer = kWaveBreak;
    }
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
