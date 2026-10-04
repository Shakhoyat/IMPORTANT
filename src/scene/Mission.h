// Night-watch mission rules (v5): endless waves of intruder rovers against the camp.
//   wave n sends min(2 + n, kMaxRovers) rovers, one every kSpawnInterval s, from random points on the compound
//   edge (seeded, different every wave); each rover gets a random camp asset as its objective
//   kill +100, combined strike kill +150; a rover reaching its objective = breach, integrity -10 %
//   the next wave starts kWaveBreak s after the last rover of a wave is resolved (killed or breached)
//   integrity 0 = BASE LOST: the mission restarts at wave 1
#pragma once

#include <cstdint>
#include <vector>

#include "math/Math3D.h"
#include "scene/GameEvents.h"

namespace ts {

struct MissionState {
    int wave = 0;
    int score = 0;
    float integrity = 100.0f;         // %
    int kills = 0, combinedKills = 0, breaches = 0, basesLost = 0;
    int toSpawn = 0, spawned = 0, resolved = 0;  // this wave
    float spawnTimer = 0.0f;          // s until the next rover of this wave
    float breakTimer = 0.0f;          // s until the next wave (between waves)
    bool betweenWaves = true;
};

struct SpawnRequest {
    Vec3 position;     // on the compound edge (the scene snaps it to a free cell)
    float yaw = 0.0f;  // facing the camp
    Vec3 objective;
    uint32_t seed = 1; // the rover's random-route seed
};

constexpr int kKillScore = 100, kCombinedBonus = 50;
constexpr float kBreachDamage = 10.0f;
constexpr float kSpawnInterval = 3.5f, kWaveBreak = 6.0f, kFirstWaveDelay = 0.0f;

class Mission {
public:
    explicit Mission(uint32_t seed = 0x5eed1234u);
    // 'alive' = intruders currently on the field, 'freeSlots' = how many more the scene can host.
    void update(float dt, int alive, int freeSlots, std::vector<SpawnRequest>& spawns, std::vector<GameEvent>& events,
                float time);
    void onKill(bool combined, const Vec3& where, std::vector<GameEvent>& events, float time);
    void onBreach(const Vec3& where, std::vector<GameEvent>& events, float time);
    const MissionState& state() const { return st_; }
    static std::vector<Vec3> objectives();

private:
    float rnd();
    void startWave(std::vector<GameEvent>& events, float time);
    MissionState st_;
    uint32_t rng_;
    bool legacyFirst_ = true;  // the very first rover starts where the v3/v4 rover did (tests rely on it)
};

}  // namespace ts
