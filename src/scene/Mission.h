// Night-watch mission rules (v5): endless waves of intruder rovers against the camp.
//   wave n sends min(2 + n, kMaxRovers) rovers, one every kSpawnInterval s, from random points on the compound
//   edge (seeded, different every wave); each rover gets a random camp asset as its objective
//   kill +100, combined strike kill +150; a rover reaching its objective = breach, integrity -10 %
//   the next wave starts kWaveBreak s after the last rover of a wave is resolved (killed or breached)
//   integrity 0 = BASE LOST: the mission restarts at wave 1
// v6: waves run only at night (the scene stops calling update() by day); at dawn endNight() closes the wave and
// the surviving rovers withdraw (onWithdraw: no score, no integrity loss). With MissionConfig::randomRoutes every
// spawn is drawn from a uniform ring (radius jitter), gets 0-3 via points, a personality and maybe a mid-route
// objective switch; without it the random draws are exactly the v5 ones.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "ai/HideController.h"
#include "math/Math3D.h"
#include "scene/GameEvents.h"

namespace ts {

struct MissionState {
    int wave = 0;
    int score = 0;
    float integrity = 100.0f;         // %
    int kills = 0, combinedKills = 0, breaches = 0, basesLost = 0;
    int withdrawn = 0, nightsEnded = 0;  // v6: rovers that left at dawn, dawns that closed a wave
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
    // v6 random routes (empty / defaults = the v5 rover)
    bool randomRoute = false;
    std::vector<Vec3> via;            // free cells in a +-70 deg sector round the spawn bearing, outermost first
    RoverPersonality personality;
    float switchAfter = -1.0f;        // s after spawning the rover switches to altObjective (< 0: never)
    Vec3 altObjective;
};

struct MissionConfig {
    bool randomRoutes = false;
    bool legacyFirst = true;          // the very first rover starts where the v3/v4 rover did
    std::function<bool(const Vec3&)> freeCell;  // via point test (null: every point is accepted)
};

// Random-route parameters (also exported for the report).
constexpr float kSpawnJitter = 1.2f;            // spawn radius kSpawnRadius +- this
constexpr float kNorthGapDeg = 18.0f;           // no spawns within this of the gate road (north)
constexpr float kViaSectorDeg = 70.0f;          // via points within this of the spawn bearing
constexpr float kViaMinR = 4.0f, kViaMaxR = 15.0f;
constexpr int kMaxVia = 3;
constexpr float kSwitchProbability = 0.3f;
constexpr float kSwitchMinS = 8.0f, kSwitchMaxS = 22.0f;

constexpr int kKillScore = 100, kCombinedBonus = 50;
constexpr float kBreachDamage = 10.0f;
constexpr float kSpawnInterval = 3.5f, kWaveBreak = 6.0f, kFirstWaveDelay = 0.0f;

class Mission {
public:
    explicit Mission(uint32_t seed = 0x5eed1234u);
    void configure(const MissionConfig& cfg);
    const MissionConfig& config() const { return cfg_; }
    // 'alive' = intruders currently on the field, 'freeSlots' = how many more the scene can host.
    void update(float dt, int alive, int freeSlots, std::vector<SpawnRequest>& spawns, std::vector<GameEvent>& events,
                float time);
    void onKill(bool combined, const Vec3& where, std::vector<GameEvent>& events, float time);
    void onBreach(const Vec3& where, std::vector<GameEvent>& events, float time);
    // v6 dawn: the current wave ends (the next one starts as soon as waves are allowed again).
    void endNight(std::vector<GameEvent>& events, float time);
    // A rover left the field at dawn: Withdraw event, no score, integrity unchanged.
    void onWithdraw(const Vec3& where, std::vector<GameEvent>& events, float time);
    // The next spawn of the current mode (draws from the mission's random stream).
    SpawnRequest nextSpawn();
    const MissionState& state() const { return st_; }
    static std::vector<Vec3> objectives();

private:
    float rnd();
    void startWave(std::vector<GameEvent>& events, float time);
    void randomRoute(SpawnRequest& r);
    MissionState st_;
    uint32_t rng_;
    bool legacyFirst_ = true;  // the very first rover starts where the v3/v4 rover did (tests rely on it)
    MissionConfig cfg_;
};

}  // namespace ts
