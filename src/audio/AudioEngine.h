// Procedural game audio: voice pool + spatial mixer shared by live playback (miniaudio device callback) and offline
// rendering (--record). All sounds are synthesised in code (Synth.h); this class decides what plays, where, and mixes.
// v7: HRTF-lite voices (ITD / ILD / head shadow / rear filter), sound propagation delay beyond 20 m, echo taps of
// loud reports off the perimeter wall and the treeline, a room reverb bus that becomes the concrete bunker when the
// listener is inside it (sources on the other side of the bunker roof are occluded), ducking under blasts, a look-ahead
// limiter, footsteps per surface, radio chatter on phase events, UI cues and a time-of-day ambience.
//
// Threading: one mutex protects the voice state. update() / handleEvent() run on the game thread, render() runs on the
// device thread (or on the caller's thread for renderOffline()). Clips are synthesised before the lock is taken.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/Acoustics.h"
#include "audio/Dsp.h"
#include "audio/Mixing.h"
#include "audio/Spatial.h"
#include "audio/Synth.h"
#include "math/Math3D.h"
#include "scene/GameEvents.h"

namespace ts {
class Scene;
}

namespace ts::audio {

constexpr int kMaxVoices = 32;   // simultaneously mixed voices (one-shots + continuous), ambient bed excluded

enum class Clip { Explosion, Cannon, Gatling, Whoosh, Door, Siren, Alarm, LockTone, ConfirmChirp, KillConfirm,
                  // v7
                  Footstep, Radio, Ui, Rifle, Impact };
enum class Loop { Drone, Helicopter, Tank, Projectile,
                  // v7
                  Rover, Generator };

// v7 listener description for the game camera rig (agent G). Pass the possessed object's / camera's velocity when
// it is known (Doppler stays clean across POV blends); otherwise it is derived from position changes, ignoring
// jumps faster than 40 m/s (camera cuts).
struct ListenerState {
    Vec3 position{0.0f, 1.7f, 0.0f};
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    Vec3 velocity{0.0f, 0.0f, 0.0f};
    bool hasVelocity = false;
    float bunker = -1.0f;   // bunker reverb zone: < 0 auto (from the position), else 0 (open air) .. 1 (inside)
    // The vehicle the player controls (possession): its engine follows the throttle; with selfInterior (first-person
    // camera inside / on it) its own engine goes through the cockpit / hull filter (low-pass + hull rumble boost).
    int selfControllable = -1;   // scene.controllables() index (tank, helicopter), -1 none
    int selfRover = -1;          // intruder slot of the rover the player drives, -1 none
    bool selfInterior = false;
    float selfThrottle = 0.0f;   // W / S axis, -1..1
};

class AudioEngine {
public:
    AudioEngine();
    explicit AudioEngine(uint32_t seed);   // seed of the ambience and of the take variations (default 99)
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // ---- device (Device.cpp, miniaudio) ----
    bool startDevice();   // false (silently) when there is no playback device, e.g. headless / CI
    void stop();
    bool deviceRunning() const { return device_ != nullptr; }

    // ---- per frame ----
    // listener = camera position / forward / up. Reads scene.events(), drones, controllables, projectiles, v7: rovers,
    // soldiers' footsteps, the bunker generator, the time of day (ambience) and the bunker zone (reverb).
    void update(const Scene& scene, const Vec3& listenerPos, const Vec3& forward, const Vec3& up, float dt);
    void update(const Scene& scene, const ListenerState& listener, float dt);   // v7 (camera rig / POV)
    // Only collects the scene's events (deduplicated against the previous call). The recorder calls it after every
    // 60 Hz simulation step, because scene.events() only holds the events of the last step.
    void pollEvents(const Scene& scene);

    // ---- lower-level control (used by update(), tests and the demo tool) ----
    void setListener(const Vec3& pos, const Vec3& forward, const Vec3& up, float dt);
    void setListener(const ListenerState& listener, float dt);
    void cameraCut();   // the next listener update is a teleport: no Doppler from the jump (POV switch, free cam)
    void handleEvent(const GameEvent& e);
    void playClip(Clip c, const Vec3& pos, bool spatial = true, float gain = 1.0f, float power = 1.0f);
    // Marks a continuous source alive this frame. key = (kind, index). param: speed (m/s) for drone / tank / rover,
    // rotor fraction for helicopter, 0 missile / 1 shell for projectiles, load 0..1 for the generator. The velocity
    // is derived from position changes when 'vel' is null.
    // v7: load = the player's throttle 0..1 on the possessed vehicle; interior = cockpit / hull filter (the listener
    // sits in this source).
    void setSource(Loop kind, int index, const Vec3& pos, const Vec3* vel, float param, float dt, float load = 0.0f,
                   bool interior = false);
    void endFrame(float dt);  // fades out continuous sources not refreshed since the previous endFrame()

    // ---- v7 game cues ----
    void uiCue(UiCue c, float gain = 1.0f);                                // 2D UI bus (never ducked / reverberated)
    void footstep(const Vec3& pos, Surface s, float intensity = 1.0f);     // one step (0.3 quiet .. 1.3 sprint)
    void footstep(const Vec3& pos, float intensity = 1.0f) { footstep(pos, surfaceAt(pos), intensity); }
    void radioChatter(int variant = -1);                                   // -1: next take; skipped while busy
    void gunshot(const Vec3& muzzle, float gain = 1.0f);                   // possessed soldier's rifle report
    void impact(const Vec3& pos, float gain = 1.0f);                       // bullet impact tick (surface from pos)
    void setTimeOfDay(float hour, float daylight);                         // ambience (update() does this)
    void setHrtf(bool on);                                                 // false: v6 equal-power pan
    void setReverbEnabled(bool on);
    void setEchoesEnabled(bool on);                                        // structure echo taps (default on)

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
    float duckGain() const;                   // 1 = no ducking
    float listenerBunker() const;             // 0 open air .. 1 bunker
    float sourcePitch(Loop kind, int index) const;   // Doppler ratio of a live continuous source, -1 if none
    float limiterMinGain() const;             // smallest limiter gain since resetLimiterStats()
    void resetLimiterStats();
    int footstepsPlayed() const;
    int radioPlayed() const;

private:
    struct Tap { float delay = 0.0f, gL = 0.0f, gR = 0.0f, alpha = 1.0f; OnePole lp; };
    struct Voice {
        bool active = false, loop = false, releasing = false, seen = false, spatial = true;
        int kind = 0, index = 0;
        std::shared_ptr<const std::vector<float>> clip;
        double pos = 0.0;
        float step = 1.0f;
        std::unique_ptr<ContinuousGen> gen;
        float param = 0.0f, pitch = 1.0f, curPitch = 1.0f;
        float targetL = 0.0f, targetR = 0.0f, curL = 0.0f, curR = 0.0f, alpha = 1.0f, curAlpha = 1.0f;
        float refDistance = 3.0f, gain = 1.0f;
        Vec3 posW, velW, lastPos;
        bool hasLast = false;
        OnePole lp;
        // v7
        int priority = 1;            // 0 footsteps, 1 events, 2 continuous, 3 UI (stealing order)
        bool ui = false, duckable = true, started = false, interior = false;
        OnePole hullLp;
        float duckOthers = 0.0f;     // > 0: ducks the rest of the mix when it starts (explosions, cannon)
        long delay = 0;              // samples before a one-shot starts (sound propagation)
        float ring[64] = {};         // ITD delay line
        uint32_t widx = 0;
        float dL = 0.0f, dR = 0.0f, tdL = 0.0f, tdR = 0.0f;
        float shAL = 1.0f, shAR = 1.0f, rearA = 1.0f, occA = 1.0f, occGain = 1.0f;
        OnePole shL, shR, rearLp, occLp;
        float send = 0.0f, curSend = 0.0f;
        int ntaps = 0;
        Tap taps[2];
        float tail = 0.0f;           // samples after the clip end (echo taps)
        float loudness() const { return (targetL > targetR ? targetL : targetR) * gain; }
    };

    std::shared_ptr<const std::vector<float>> getClip(Clip c, float power, int variant = 0);
    void applySpatial(Voice& v);
    Voice* allocate(float loudness, int priority);
    void mix(float* out, int frames);
    void startVoice(Clip c, const std::shared_ptr<const std::vector<float>>& clip, const Vec3& pos, bool spatial,
                    float gain, int priority, float refDistance, float pitchJitter, bool echoes, float duckDepth);
    void setListenerLocked(const Vec3& pos, const Vec3& forward, const Vec3& up, const Vec3* vel, float bunker, float dt);
    void trackFootsteps(const Scene& scene);

    mutable std::mutex mu_;
    std::vector<Voice> voices_;
    Vec3 lpos_{0, 0, 0}, lvel_{0, 0, 0}, lright_{1, 0, 0}, lfwd_{0, 0, -1};
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

    // v7
    bool hrtf_ = true, reverbOn_ = true, echoesOn_ = true;
    float bunker_ = 0.0f;                   // listener in the bunker zone
    float duck_ = 1.0f, duckDepth_ = 0.0f, duckHold_ = 0.0f;
    uint64_t clock_ = 0;                    // samples rendered
    uint64_t radioBusyUntil_ = 0;
    int radioNext_ = 0, footsteps_ = 0, radios_ = 0;
    int selfCtl_ = -1, selfRover_ = -1;
    bool selfInterior_ = false;
    float selfThrottle_ = 0.0f;
    Rng rng_;
    RoomReverb reverb_;
    PeakLimiter limiter_;
    std::vector<float> revIn_, ambBuf_, duckBuf_;
    struct Steps { Vec3 p[2]; bool have[2] = {false, false}; };
    std::vector<Steps> steps_;

    std::mutex clipMu_;
    std::map<int, std::shared_ptr<const std::vector<float>>> clips_;

    void* device_ = nullptr;  // ma_device*
};

}  // namespace ts::audio
