// Procedural game audio: voice pool + spatial mixer shared by live playback (miniaudio device callback) and offline
// rendering (--record). All sounds are synthesised in code (Synth.h); this class decides what plays, where, and mixes.
//
// Threading: one mutex protects the voice state. update() / handleEvent() run on the game thread, render() runs on the
// device thread (or on the caller's thread for renderOffline()). Clips are synthesised before the lock is taken.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/Dsp.h"
#include "audio/Spatial.h"
#include "audio/Synth.h"
#include "math/Math3D.h"
#include "scene/GameEvents.h"

namespace ts {
class Scene;
}

namespace ts::audio {

constexpr int kMaxVoices = 24;   // simultaneously mixed voices (one-shots + continuous), ambient bed excluded

enum class Clip { Explosion, Cannon, Gatling, Whoosh, Door, Siren, Alarm, LockTone, ConfirmChirp, KillConfirm };
enum class Loop { Drone, Helicopter, Tank, Projectile };

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // ---- device (Device.cpp, miniaudio) ----
    bool startDevice();   // false (silently) when there is no playback device, e.g. headless / CI
    void stop();
    bool deviceRunning() const { return device_ != nullptr; }

    // ---- per frame ----
    // listener = camera position / forward / up. Reads scene.events(), drones, controllables, projectiles.
    void update(const Scene& scene, const Vec3& listenerPos, const Vec3& forward, const Vec3& up, float dt);
    // Only collects the scene's events (deduplicated against the previous call). The recorder calls it after every
    // 60 Hz simulation step, because scene.events() only holds the events of the last step.
    void pollEvents(const Scene& scene);

    // ---- lower-level control (used by update(), tests and the demo tool) ----
    void setListener(const Vec3& pos, const Vec3& forward, const Vec3& up, float dt);
    void handleEvent(const GameEvent& e);
    void playClip(Clip c, const Vec3& pos, bool spatial = true, float gain = 1.0f, float power = 1.0f);
    // Marks a continuous source alive this frame. key = (kind, index). param: speed (m/s) for drone / tank, rotor
    // fraction for helicopter, 0 missile / 1 shell for projectiles. The velocity is derived from position changes
    // when 'vel' is null.
    void setSource(Loop kind, int index, const Vec3& pos, const Vec3* vel, float param, float dt);
    void endFrame(float dt);  // fades out continuous sources not refreshed since the previous endFrame()

    // ---- rendering ----
    // Mixes 'frames' stereo frames (interleaved) at 48 kHz. Device callback + offline use this.
    void render(float* out, int frames);
    // Exactly seconds * 48000 frames (a fractional remainder is carried to the next call); 'out' is resized.
    void renderOffline(float seconds, std::vector<float>& out);

    void setMasterVolume(float v);
    void setAmbientEnabled(bool on);
    void prewarm();  // synthesises the fixed clips now (called by startDevice)

    // ---- inspection ----
    int voiceCount() const;
    int oneShotCount() const;
    static constexpr int voiceLimit() { return kMaxVoices; }
    static constexpr float kCeiling = 0.88f;  // master soft-limiter ceiling (-1.1 dBFS)

private:
    struct Voice {
        bool active = false, loop = false, releasing = false, seen = false, spatial = true;
        int kind = 0, index = 0;
        std::shared_ptr<const std::vector<float>> clip;
        double pos = 0.0;
        float step = 1.0f;
        std::unique_ptr<ContinuousGen> gen;
        float param = 0.0f, pitch = 1.0f;
        float targetL = 0.0f, targetR = 0.0f, curL = 0.0f, curR = 0.0f, alpha = 1.0f, curAlpha = 1.0f;
        float refDistance = 3.0f, gain = 1.0f;
        Vec3 posW, velW, lastPos;
        bool hasLast = false;
        OnePole lp;
        float loudness() const { return (targetL > targetR ? targetL : targetR) * gain; }
    };

    std::shared_ptr<const std::vector<float>> getClip(Clip c, float power);
    void applySpatial(Voice& v);
    Voice* allocate(float loudness);
    void mix(float* out, int frames);

    mutable std::mutex mu_;
    std::vector<Voice> voices_;
    Vec3 lpos_{0, 0, 0}, lvel_{0, 0, 0}, lright_{1, 0, 0};
    bool haveListener_ = false;
    float masterVolume_ = 1.0f;
    bool ambientOn_ = true;
    AmbientGen ambient_;
    std::vector<GameEvent> prevEvents_;
    struct ProjTrack { Vec3 p, v; int id; bool used; };
    std::vector<ProjTrack> projTrack_;
    int nextProjId_ = 0;
    double offlineCarry_ = 0.0;
    std::vector<float> scratch_;

    std::mutex clipMu_;
    std::map<int, std::shared_ptr<const std::vector<float>>> clips_;

    void* device_ = nullptr;  // ma_device*
};

}  // namespace ts::audio
