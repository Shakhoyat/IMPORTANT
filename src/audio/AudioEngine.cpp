#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "scene/Controllable.h"
#include "scene/Garrison.h"
#include "scene/Scene.h"

namespace ts::audio {

namespace {

constexpr int kRadioTakes = 8;
constexpr int kFootTakes = 6;
constexpr Vec3 kGeneratorPos{3.2f, -2.6f, 2.65f};   // bunker plant room (Bunker.cpp, generator block)

struct LoopParams { float gain, ref; };
LoopParams loopParams(Loop k) {
    switch (k) {
        case Loop::Drone: return {0.9f, 4.0f};
        case Loop::Helicopter: return {1.0f, 8.0f};
        case Loop::Tank: return {0.9f, 6.0f};
        case Loop::Projectile: return {0.8f, 5.0f};
        case Loop::Rover: return {0.55f, 2.5f};
        case Loop::Generator: return {0.6f, 2.5f};
    }
    return {1.0f, 4.0f};
}
float clipRef(Clip c) {
    switch (c) {
        case Clip::Explosion: return 25.0f;
        case Clip::Cannon: return 12.0f;
        case Clip::Gatling: return 8.0f;
        case Clip::Whoosh: return 5.0f;
        case Clip::Door: return 4.0f;
        case Clip::Siren: return 12.0f;
        case Clip::Alarm: return 15.0f;
        case Clip::Footstep: return 1.5f;
        case Clip::Rifle: return 6.0f;
        case Clip::Impact: return 4.0f;
        default: return 1.0f;
    }
}
// Reports that echo off the camp structures.
bool impulsive(Clip c) { return c == Clip::Explosion || c == Clip::Cannon || c == Clip::Gatling || c == Clip::Rifle; }
// Cockpit / hull low-pass of the listener's own vehicle (tank hull is the darkest).
float hullCutoff(int kind) {
    switch (static_cast<Loop>(kind)) {
        case Loop::Tank: return 650.0f;
        case Loop::Helicopter: return 1500.0f;
        default: return 1800.0f;
    }
}
constexpr int kTicksPerSurface = 4;
constexpr float kTracerImpactMax = 20.0f;   // Explosion events up to power 0.20 are bullet / tracer impacts
bool sameEvent(const GameEvent& a, const GameEvent& b) {
    return a.type == b.type && a.uav == b.uav && a.track == b.track && a.value == b.value && a.time == b.time &&
           a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z;
}
float parseAfter(const std::string& s, const char* tag, float fallback) {
    const size_t p = s.find(tag);
    if (p == std::string::npos) return fallback;
    return static_cast<float>(std::atof(s.c_str() + p + std::strlen(tag)));
}
Vec3 finiteVec(const Vec3& v) { return std::isfinite(v.x + v.y + v.z) ? v : Vec3{}; }
inline float clipAt(const std::vector<float>& c, double p) {
    if (p < 0.0) return 0.0f;
    const size_t i0 = static_cast<size_t>(p);
    if (i0 + 1 >= c.size()) return 0.0f;
    const float f = static_cast<float>(p - static_cast<double>(i0));
    return c[i0] + (c[i0 + 1] - c[i0]) * f;
}
inline float ringAt(const float* ring, uint32_t w, float d) {
    const float fl = std::floor(d);
    const uint32_t di = static_cast<uint32_t>(fl);
    const float f = d - fl;
    return ring[(w - di) & 63u] * (1.0f - f) + ring[(w - di - 1u) & 63u] * f;
}

}  // namespace

AudioEngine::AudioEngine() : AudioEngine(99u) {}
AudioEngine::AudioEngine(uint32_t seed) : ambient_(seed), rng_(seed * 7u + 3u) { voices_.resize(kMaxVoices); }
AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::setMasterVolume(float v) {
    std::lock_guard<std::mutex> lk(mu_);
    masterVolume_ = clampf(v, 0.0f, 2.0f);
}
void AudioEngine::setAmbientEnabled(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    ambientOn_ = on;
}
void AudioEngine::setHrtf(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    hrtf_ = on;
}
void AudioEngine::setReverbEnabled(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    reverbOn_ = on;
}
void AudioEngine::setEchoesEnabled(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    echoesOn_ = on;
}
void AudioEngine::setTimeOfDay(float hour, float daylight) {
    std::lock_guard<std::mutex> lk(mu_);
    ambient_.setTimeOfDay(hour, daylight);
}

int AudioEngine::voiceCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    int n = 0;
    for (const Voice& v : voices_) n += v.active ? 1 : 0;
    return n;
}
int AudioEngine::oneShotCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    int n = 0;
    for (const Voice& v : voices_) n += (v.active && !v.loop) ? 1 : 0;
    return n;
}
float AudioEngine::duckGain() const {
    std::lock_guard<std::mutex> lk(mu_);
    return duck_;
}
float AudioEngine::listenerBunker() const {
    std::lock_guard<std::mutex> lk(mu_);
    return bunker_;
}
float AudioEngine::sourcePitch(Loop kind, int index) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const Voice& v : voices_)
        if (v.active && v.loop && !v.releasing && v.kind == static_cast<int>(kind) && v.index == index) return v.pitch;
    return -1.0f;
}
float AudioEngine::limiterMinGain() const {
    std::lock_guard<std::mutex> lk(mu_);
    return limiter_.minGain();
}
void AudioEngine::resetLimiterStats() {
    std::lock_guard<std::mutex> lk(mu_);
    limiter_.resetStats();
}
int AudioEngine::footstepsPlayed() const {
    std::lock_guard<std::mutex> lk(mu_);
    return footsteps_;
}
int AudioEngine::radioPlayed() const {
    std::lock_guard<std::mutex> lk(mu_);
    return radios_;
}

// ------------------------------------------------------------------------------------------------ clips
std::shared_ptr<const std::vector<float>> AudioEngine::getClip(Clip c, float power, int variant) {
    int bucket = 0;
    if (c == Clip::Explosion) bucket = static_cast<int>(std::lround(clampf(power, 0.05f, 4.0f) * 10.0f));
    else if (c == Clip::Footstep || c == Clip::Radio || c == Clip::Ui || c == Clip::Rifle || c == Clip::Impact) bucket = variant;
    const int key = static_cast<int>(c) * 1000 + bucket;
    std::lock_guard<std::mutex> lk(clipMu_);
    auto it = clips_.find(key);
    if (it != clips_.end()) return it->second;
    std::vector<float> v;
    switch (c) {
        case Clip::Explosion: v = synthExplosion(static_cast<float>(bucket) / 10.0f); break;
        case Clip::Cannon: v = synthCannon(); break;
        case Clip::Gatling: v = synthGatling(); break;
        case Clip::Whoosh: v = synthWhoosh(); break;
        case Clip::Door: v = synthDoor(); break;
        case Clip::Siren: v = synthSiren(); break;
        case Clip::Alarm: v = synthAlarm(); break;
        case Clip::LockTone: v = synthLockTone(); break;
        case Clip::ConfirmChirp: v = synthConfirmChirp(); break;
        case Clip::KillConfirm: v = synthKillConfirm(); break;
        case Clip::Footstep:
            v = synthFootstep(static_cast<Surface>(bucket / 8), static_cast<uint32_t>(bucket % 8));
            break;
        case Clip::Radio: v = synthRadioChatter(static_cast<uint32_t>(bucket) + 1u); break;
        case Clip::Ui: v = synthUiCue(static_cast<UiCue>(bucket)); break;
        case Clip::Rifle: v = synthRifleShot(static_cast<uint32_t>(bucket)); break;
        case Clip::Impact: v = synthImpact(static_cast<Surface>(bucket / 8), static_cast<uint32_t>(bucket % 8)); break;
    }
    auto sp = std::make_shared<const std::vector<float>>(std::move(v));
    clips_[key] = sp;
    return sp;
}

void AudioEngine::prewarm() {
    for (Clip c : {Clip::Cannon, Clip::Gatling, Clip::Whoosh, Clip::Door, Clip::Siren, Clip::Alarm, Clip::LockTone,
                   Clip::ConfirmChirp, Clip::KillConfirm})
        getClip(c, 1.0f);
    for (float p : {0.5f, 1.0f, 1.5f}) getClip(Clip::Explosion, p);
    for (int s = 0; s < kSurfaceCount; ++s)
        for (int k = 0; k < kFootTakes; ++k) getClip(Clip::Footstep, 1.0f, s * 8 + k);
    for (int k = 0; k < kRadioTakes; ++k) getClip(Clip::Radio, 1.0f, k);
    for (int k = 0; k < kUiCueCount; ++k) getClip(Clip::Ui, 1.0f, k);
    for (int k = 0; k < 4; ++k) getClip(Clip::Rifle, 1.0f, k);
    for (int s = 0; s < kSurfaceCount; ++s)
        for (int k = 0; k < kTicksPerSurface; ++k) getClip(Clip::Impact, 1.0f, s * 8 + k);
}

// ------------------------------------------------------------------------------------------------ voices
void AudioEngine::applySpatial(Voice& v) {
    if (v.interior) {   // the listener sits in this vehicle: centred, no Doppler, through the hull / cockpit
        v.targetL = v.targetR = v.gain * 0.8f;
        v.alpha = onePoleAlpha(hullCutoff(v.kind));
        v.pitch = 1.0f;
        v.tdL = v.tdR = 0.0f;
        v.shAL = v.shAR = v.rearA = v.occA = 1.0f;
        v.occGain = 1.0f;
        v.send = 0.0f;
        return;
    }
    if (!v.spatial) {
        v.targetL = v.targetR = v.gain * 0.7071f;
        v.alpha = 1.0f;
        v.pitch = 1.0f;
        v.tdL = v.tdR = 0.0f;
        v.shAL = v.shAR = v.rearA = v.occA = 1.0f;
        v.occGain = 1.0f;
        v.send = 0.0f;
        return;
    }
    float distance;
    if (hrtf_) {
        const SpatialHrtf h = computeHrtf(lpos_, lvel_, lright_, lfwd_, v.posW, v.velW, v.refDistance, v.gain, 1.0f);
        v.targetL = h.gainL;
        v.targetR = h.gainR;
        v.alpha = onePoleAlpha(h.airHz);
        v.pitch = h.doppler;
        v.tdL = h.delayL;
        v.tdR = h.delayR;
        v.shAL = onePoleAlpha(h.shadowHzL);
        v.shAR = onePoleAlpha(h.shadowHzR);
        v.rearA = onePoleAlpha(h.rearHz);
        distance = h.distance;
    } else {
        const Spatial s = computeSpatial(lpos_, lvel_, lright_, v.posW, v.velW, v.refDistance, v.gain);
        v.targetL = s.gainL;
        v.targetR = s.gainR;
        v.alpha = s.lpAlpha;
        v.pitch = s.doppler;
        v.tdL = v.tdR = 0.0f;
        v.shAL = v.shAR = v.rearA = 1.0f;
        distance = s.distance;
    }
    // Bunker roof / earth between the listener and the source: muffled and quieter.
    const float sb = bunkerAmount(v.posW);
    const float sep = std::fabs(bunker_ - sb);
    v.occGain = 1.0f - 0.75f * sep;
    v.occA = onePoleAlpha(20000.0f * std::pow(350.0f / 20000.0f, sep));
    v.targetL *= v.occGain;
    v.targetR *= v.occGain;
    // Reverb send: the wet level falls off slower than the direct sound (distance cue), and both ends inside the
    // bunker excite the concrete room fully.
    const float att = attenuation(distance, v.refDistance);
    const float far = 0.3f + 0.7f * clampf(distance / 80.0f, 0.0f, 1.0f);
    const float room = std::min(bunker_, sb);
    v.send = v.gain * std::sqrt(att) * v.occGain * lerpf(far, 1.6f, room);
}

AudioEngine::Voice* AudioEngine::allocate(float loudness, int priority) {
    Voice* quietest = nullptr;
    for (Voice& v : voices_) {
        if (!v.active) return &v;
        // footsteps only replace footsteps; events / sources compete by loudness; UI may replace anything
        const bool candidate = priority == 0 ? v.priority == 0 : (priority >= 3 || v.priority <= 2);
        if (!candidate) continue;
        if (!quietest || v.loudness() < quietest->loudness()) quietest = &v;
    }
    if (quietest && loudness > quietest->loudness()) return quietest;  // steal the quietest
    return nullptr;                                                    // the new sound is the quietest: drop it
}

void AudioEngine::setListenerLocked(const Vec3& pos, const Vec3& forward, const Vec3& up, const Vec3* vel, float bunker,
                                    float dt) {
    const Vec3 p = finiteVec(pos);
    if (vel) {
        Vec3 v = finiteVec(*vel);
        if (length(v) > 200.0f) v = Vec3{};
        lvel_ = v;
    } else if (haveListener_ && dt > 1e-5f) {
        Vec3 raw = (p - lpos_) / dt;
        if (length(raw) > 40.0f) raw = Vec3{};  // camera cut, not motion
        lvel_ = lvel_ + (raw - lvel_) * 0.5f;
    } else if (!haveListener_) {
        lvel_ = Vec3{};
    }
    lpos_ = p;
    haveListener_ = true;
    const Vec3 f = finiteVec(forward), u = finiteVec(up);
    Vec3 r = cross(f, u);
    if (length(r) > 1e-4f) lright_ = normalize(r);
    if (length(f) > 1e-4f) lfwd_ = normalize(f);
    bunker_ = bunker < 0.0f ? bunkerAmount(p) : clampf(bunker, 0.0f, 1.0f);
    // refresh every playing voice (the head turned / moved); one-shots keep their playback rate
    for (Voice& v : voices_)
        if (v.active && v.spatial && !v.releasing) applySpatial(v);
}

void AudioEngine::setListener(const Vec3& pos, const Vec3& forward, const Vec3& up, float dt) {
    std::lock_guard<std::mutex> lk(mu_);
    setListenerLocked(pos, forward, up, nullptr, -1.0f, dt);
}

void AudioEngine::setListener(const ListenerState& l, float dt) {
    std::lock_guard<std::mutex> lk(mu_);
    selfCtl_ = l.selfControllable;
    selfRover_ = l.selfRover;
    selfInterior_ = l.selfInterior;
    selfThrottle_ = std::isfinite(l.selfThrottle) ? clampf(std::fabs(l.selfThrottle), 0.0f, 1.0f) : 0.0f;
    setListenerLocked(l.position, l.forward, l.up, l.hasVelocity ? &l.velocity : nullptr, l.bunker, dt);
}

void AudioEngine::cameraCut() {
    std::lock_guard<std::mutex> lk(mu_);
    haveListener_ = false;
    lvel_ = Vec3{};
}

void AudioEngine::startVoice(Clip c, const std::shared_ptr<const std::vector<float>>& clip, const Vec3& pos, bool spatial,
                             float gain, int priority, float refDistance, float pitchJitter, bool echoes, float duckDepth) {
    Voice tmp;
    tmp.spatial = spatial;
    tmp.posW = finiteVec(pos);
    tmp.refDistance = refDistance;
    tmp.gain = gain;
    applySpatial(tmp);
    Voice* v = allocate(tmp.loudness(), priority);
    if (!v) return;
    *v = std::move(tmp);
    v->active = true;
    v->clip = clip;
    v->priority = priority;
    v->ui = !spatial;
    v->duckable = spatial && duckDepth <= 0.0f;
    v->curL = v->targetL;  // snap: the onset must be audible within the first block
    v->curR = v->targetR;
    v->curAlpha = v->alpha;
    v->dL = v->tdL;
    v->dR = v->tdR;
    v->curSend = v->send;
    v->step = v->pitch * (1.0f + pitchJitter);
    v->curPitch = v->pitch;
    if (spatial) {
        const float d = length(v->posW - lpos_);
        v->delay = static_cast<long>(propagationDelay(d) * kSampleRate);
        // ducking depth scales with how loud the blast is here
        v->duckOthers = duckDepth > 0.0f ? duckDepth * clampf((v->loudness() - 0.05f) / 0.45f, 0.0f, 1.0f) : 0.0f;
        if (echoes && echoesOn_) {
            EchoTap taps[2];
            const int n = echoTaps(lpos_, v->posW, taps);
            for (int k = 0; k < n; ++k) {
                Tap& t = v->taps[v->ntaps];
                const float g = std::max(v->targetL, v->targetR) * taps[k].gain;
                if (g < 1e-4f) continue;
                const Vec3 dir = taps[k].image - lpos_;
                const float len = length(dir);
                float pl, pr;
                panGains(len > 1e-3f ? 0.6f * dot(dir / len, lright_) : 0.0f, pl, pr);
                t.delay = taps[k].delaySec * kSampleRate;
                t.gL = g * pl * 1.41f;
                t.gR = g * pr * 1.41f;
                t.alpha = onePoleAlpha(std::min(taps[k].cutoffHz, airCutoffHz(len)));
                v->tail = std::max(v->tail, t.delay);
                ++v->ntaps;
            }
        }
    }
    (void)c;
}

void AudioEngine::playClip(Clip c, const Vec3& pos, bool spatial, float gain, float power) {
    const auto clip = getClip(c, power);  // synthesised outside the audio lock
    std::lock_guard<std::mutex> lk(mu_);
    const float duck = c == Clip::Explosion ? 0.65f : c == Clip::Cannon ? 0.4f : 0.0f;
    startVoice(c, clip, pos, spatial, gain, 1, clipRef(c), 0.0f, spatial && impulsive(c), duck);
}

void AudioEngine::uiCue(UiCue c, float gain) {
    const auto clip = getClip(Clip::Ui, 1.0f, static_cast<int>(c));
    std::lock_guard<std::mutex> lk(mu_);
    startVoice(Clip::Ui, clip, lpos_, false, gain, 3, 1.0f, 0.0f, false, 0.0f);
    if (c == UiCue::PovJump || c == UiCue::FreeCam || c == UiCue::Possess || c == UiCue::Release) {
        haveListener_ = false;   // the view is about to jump: no Doppler from the cut
        lvel_ = Vec3{};
    }
}

void AudioEngine::footstep(const Vec3& pos, Surface s, float intensity) {
    int take;
    float jitter;
    {
        std::lock_guard<std::mutex> lk(mu_);
        take = static_cast<int>(rng_.uni() * kFootTakes) % kFootTakes;
        jitter = 0.05f * rng_.bi();
    }
    const auto clip = getClip(Clip::Footstep, 1.0f, static_cast<int>(s) * 8 + take);
    std::lock_guard<std::mutex> lk(mu_);
    ++footsteps_;
    startVoice(Clip::Footstep, clip, pos, true, 0.6f * clampf(intensity, 0.1f, 1.5f), 0, clipRef(Clip::Footstep), jitter,
               false, 0.0f);
}

void AudioEngine::radioChatter(int variant) {
    int take;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (clock_ < radioBusyUntil_) return;   // one transmission at a time
        take = variant >= 0 ? variant % kRadioTakes : radioNext_;
        radioNext_ = (radioNext_ + 3) % kRadioTakes;
    }
    const auto clip = getClip(Clip::Radio, 1.0f, take);
    std::lock_guard<std::mutex> lk(mu_);
    radioBusyUntil_ = clock_ + clip->size() + static_cast<uint64_t>(0.8f * kSampleRate);
    ++radios_;
    startVoice(Clip::Radio, clip, lpos_, false, 0.55f, 3, 1.0f, 0.0f, false, 0.0f);
}

void AudioEngine::handleEvent(const GameEvent& e) {
    switch (e.type) {
        case GameEventType::WaveStart: playClip(Clip::Siren, e.position, true, 0.35f); radioChatter(); break;
        case GameEventType::EnemySpotted: playClip(Clip::ConfirmChirp, e.position, false, 0.5f); radioChatter(); break;
        case GameEventType::LockOn: playClip(Clip::LockTone, e.position, false, 0.6f); break;
        case GameEventType::RoundLaunched: playClip(Clip::Whoosh, e.position, true, 1.0f); break;
        case GameEventType::CombinedStrike: playClip(Clip::ConfirmChirp, e.position, false, 0.6f); radioChatter(); break;
        case GameEventType::Explosion:
            if (static_cast<float>(e.value) <= kTracerImpactMax) impact(e.position);   // bullet / tracer hit: a tick
            else playClip(Clip::Explosion, e.position, true, 1.0f, static_cast<float>(e.value) / 100.0f);
            break;
        case GameEventType::EnemyHit: uiCue(UiCue::HitMarker, 0.6f); break;
        case GameEventType::EnemyDestroyed: playClip(Clip::KillConfirm, e.position, false, 0.6f); radioChatter(); break;
        case GameEventType::Breach: playClip(Clip::Alarm, e.position, true, 0.8f); uiCue(UiCue::Alert, 0.5f); radioChatter(); break;
        case GameEventType::BaseLost: playClip(Clip::Siren, e.position, true, 0.4f); break;
        case GameEventType::CannonFire: playClip(Clip::Cannon, e.position, true, 1.0f); break;
        case GameEventType::GatlingBurst: playClip(Clip::Gatling, e.position, true, 0.9f); break;
        case GameEventType::DoorMoving: playClip(Clip::Door, e.position, true, 0.8f); break;
        // v6 day / night: soft non-spatial cues; the night siren is quieter than a wave's. v7: radio on every phase.
        case GameEventType::Dusk: playClip(Clip::ConfirmChirp, e.position, false, 0.3f); radioChatter(); break;
        case GameEventType::NightFalls: playClip(Clip::Siren, e.position, false, 0.18f); radioChatter(); break;
        case GameEventType::Dawn: playClip(Clip::ConfirmChirp, e.position, false, 0.3f); radioChatter(); break;
        case GameEventType::DayBreak: playClip(Clip::ConfirmChirp, e.position, false, 0.35f); radioChatter(); break;
        case GameEventType::ShiftChange: radioChatter(); break;  // + the soldiers' footsteps
        case GameEventType::Withdraw: playClip(Clip::ConfirmChirp, e.position, true, 0.4f); break;  // a rover gives up
    }
}

void AudioEngine::pollEvents(const Scene& scene) {
    const std::vector<GameEvent>& ev = scene.events();
    std::vector<GameEvent> fresh;
    for (const GameEvent& e : ev) {
        bool seen = false;
        for (const GameEvent& p : prevEvents_) seen = seen || sameEvent(e, p);
        if (!seen) fresh.push_back(e);
    }
    prevEvents_ = ev;
    for (const GameEvent& e : fresh) handleEvent(e);
}

void AudioEngine::gunshot(const Vec3& muzzle, float gain) {
    int take;
    float jitter;
    {
        std::lock_guard<std::mutex> lk(mu_);
        take = static_cast<int>(rng_.uni() * 4.0f) % 4;
        jitter = 0.03f * rng_.bi();
    }
    const auto clip = getClip(Clip::Rifle, 1.0f, take);
    std::lock_guard<std::mutex> lk(mu_);
    startVoice(Clip::Rifle, clip, muzzle, true, 0.8f * gain, 1, clipRef(Clip::Rifle), jitter, true, 0.25f);
}

void AudioEngine::impact(const Vec3& pos, float gain) {
    int take;
    float jitter;
    {
        std::lock_guard<std::mutex> lk(mu_);
        take = static_cast<int>(rng_.uni() * kTicksPerSurface) % kTicksPerSurface;
        jitter = 0.06f * rng_.bi();
    }
    const auto clip = getClip(Clip::Impact, 1.0f, static_cast<int>(surfaceAt(pos)) * 8 + take);
    std::lock_guard<std::mutex> lk(mu_);
    startVoice(Clip::Impact, clip, pos, true, 0.8f * gain, 1, clipRef(Clip::Impact), jitter, false, 0.0f);
}

void AudioEngine::setSource(Loop kind, int index, const Vec3& pos, const Vec3* vel, float param, float dt, float load,
                            bool interior) {
    const LoopParams lpar = loopParams(kind);
    std::lock_guard<std::mutex> lk(mu_);
    const Vec3 p = finiteVec(pos);
    Voice* v = nullptr;
    for (Voice& c : voices_)
        if (c.active && c.loop && !c.releasing && c.kind == static_cast<int>(kind) && c.index == index) v = &c;
    if (!v) {
        if (kind == Loop::Helicopter && param < 0.02f) return;  // rotor stopped: nothing to hear
        Voice tmp;
        tmp.posW = p;
        tmp.gain = lpar.gain;
        tmp.refDistance = lpar.ref;
        applySpatial(tmp);
        v = allocate(tmp.loudness(), 2);
        if (!v) return;
        *v = Voice{};
        v->active = v->loop = true;
        v->priority = 2;
        v->kind = static_cast<int>(kind);
        v->index = index;
        v->gain = lpar.gain;
        v->refDistance = lpar.ref;
        v->posW = p;
        const uint32_t seed = static_cast<uint32_t>(v->kind * 100 + index + 1);
        switch (kind) {
            case Loop::Drone: v->gen = std::make_unique<RotorHumGen>(seed); break;
            case Loop::Helicopter: v->gen = std::make_unique<HeliThumpGen>(seed); break;
            case Loop::Tank: v->gen = std::make_unique<TankEngineGen>(seed); break;
            case Loop::Projectile: v->gen = std::make_unique<RocketGen>(seed); break;
            case Loop::Rover: v->gen = std::make_unique<RoverWhineGen>(seed); break;
            case Loop::Generator: v->gen = std::make_unique<GeneratorGen>(seed); break;
        }
        if (vel) v->velW = finiteVec(*vel);
        applySpatial(*v);
        v->curL = 0.0f;  // continuous sources fade in
        v->curR = 0.0f;
        v->curAlpha = v->alpha;
        v->dL = v->tdL;
        v->dR = v->tdR;
        v->curSend = 0.0f;
        v->curPitch = v->pitch;
    }
    Vec3 raw;
    if (vel) raw = finiteVec(*vel);
    else if (v->hasLast && dt > 1e-5f) {
        raw = (p - v->lastPos) / dt;
        if (length(raw) > 80.0f) raw = Vec3{};
    }
    v->velW = v->hasLast ? v->velW + (raw - v->velW) * 0.5f : raw;
    v->lastPos = p;
    v->hasLast = true;
    v->posW = p;
    v->param = param;
    v->seen = true;
    v->interior = interior;
    v->gen->setLoad(clampf(load, 0.0f, 1.0f));
    applySpatial(*v);
}

void AudioEngine::endFrame(float) {
    std::lock_guard<std::mutex> lk(mu_);
    for (Voice& v : voices_) {
        if (!v.active || !v.loop) continue;
        if (!v.seen) {
            v.releasing = true;
            v.targetL = v.targetR = 0.0f;
            v.send = 0.0f;
        }
        v.seen = false;
    }
}

// Soldier footsteps from the gait: a new plant point of a foot is one step (on any floor, also underground).
void AudioEngine::trackFootsteps(const Scene& scene) {
    const Garrison* g = scene.garrison();
    if (!g) return;
    const int n = g->count();
    if (static_cast<int>(steps_.size()) != n) steps_.assign(static_cast<size_t>(n), Steps{});
    Vec3 lp;
    {
        std::lock_guard<std::mutex> lk(mu_);
        lp = lpos_;
    }
    for (int i = 0; i < n; ++i) {
        const Soldier* s = dynamic_cast<const Soldier*>(g->soldier(i));
        if (!s) continue;
        Steps& st = steps_[static_cast<size_t>(i)];
        for (int leg = 0; leg < 2; ++leg) {
            if (!s->planted(leg)) continue;
            const Vec3 p = s->plantPoint(leg);
            const bool fresh = !st.have[leg] || length(p - st.p[leg]) > 0.05f;
            const bool first = !st.have[leg];
            st.p[leg] = p;
            st.have[leg] = true;
            if (!fresh || first) continue;
            if (length(p - lp) > 30.0f) continue;   // too far to hear a step
            const float run = clampf(s->speed() / 4.0f, 0.0f, 1.0f);
            const float crouch = s->activity() == Activity::Crouch ? 0.6f : 1.0f;
            footstep(Vec3{p.x, p.y - 0.08f, p.z}, (0.55f + 0.6f * run) * crouch);
        }
    }
}

void AudioEngine::update(const Scene& scene, const Vec3& listenerPos, const Vec3& forward, const Vec3& up, float dt) {
    ListenerState l;
    l.position = listenerPos;
    l.forward = forward;
    l.up = up;
    update(scene, l, dt);
}

void AudioEngine::update(const Scene& scene, const ListenerState& listener, float dt) {
    setListener(listener, dt);
    {
        std::lock_guard<std::mutex> lk(mu_);
        ambient_.setTimeOfDay(scene.dayCycle().hour(), scene.dayCycle().daylight());
        ambient_.setUnderground(bunker_);
        reverb_.setBunker(bunker_);
    }
    pollEvents(scene);

    for (int i = 0; i < kUavCount; ++i) {  // both patrol UAVs
        const DroneState& d = scene.droneState(i);
        setSource(Loop::Drone, i, d.position, &d.velocity, length(d.velocity), dt);
    }

    int selfCtl, selfRover;
    bool interior;
    float throttle;
    {
        std::lock_guard<std::mutex> lk(mu_);
        selfCtl = selfCtl_;
        selfRover = selfRover_;
        interior = selfInterior_;
        throttle = selfThrottle_;
    }
    int i = 0;
    for (const auto& c : scene.controllables()) {
        const std::string name = c->name();
        const std::string st = c->status();
        const bool self = i == selfCtl;
        if (name.find("HELICOPTER") != std::string::npos)
            setSource(Loop::Helicopter, i, c->position(), nullptr, clampf(parseAfter(st, "ROTOR ", 0.0f) / 100.0f, 0.0f, 1.0f), dt,
                      self ? throttle : 0.0f, self && interior);
        else if (name.find("TANK") != std::string::npos)
            setSource(Loop::Tank, i, c->position(), nullptr, parseAfter(st, "SPEED ", 0.0f), dt, self ? throttle : 0.0f,
                      self && interior);
        ++i;
    }

    // v7: intruder rovers (electric drive), the bunker generator, soldiers' footsteps
    int k = 0;
    for (const auto& in : scene.intruders()) {
        if (in->life == Intruder::Life::Alive) {
            const float sp = 0.5f * std::fabs(in->state.leftSpeed + in->state.rightSpeed);
            const bool self = k == selfRover;
            setSource(Loop::Rover, k, in->state.position, nullptr, sp, dt, self ? throttle : 0.0f, self && interior);
        }
        ++k;
    }
    const Vec3 still{0.0f, 0.0f, 0.0f};
    setSource(Loop::Generator, 0, kGeneratorPos, &still, 0.6f, dt);
    trackFootsteps(scene);

    // Rounds in flight: match to last frame's tracks by predicted position so each keeps its voice.
    for (ProjTrack& t : projTrack_) t.used = false;
    std::vector<ProjTrack> next;
    int count = 0;
    for (const Projectile& p : scene.combat().projectiles()) {
        if (!p.alive || p.kind == ProjectileKind::Tracer || count >= 6) continue;
        ++count;
        int best = -1;
        float bestD = 6.0f + length(p.velocity) * std::max(dt, 0.02f);
        for (size_t q = 0; q < projTrack_.size(); ++q) {
            if (projTrack_[q].used) continue;
            const float dist = length((projTrack_[q].p + projTrack_[q].v * dt) - p.position);
            if (dist < bestD) { bestD = dist; best = static_cast<int>(q); }
        }
        int id;
        if (best >= 0) { projTrack_[best].used = true; id = projTrack_[best].id; }
        else id = nextProjId_++;
        next.push_back({p.position, p.velocity, id, true});
        setSource(Loop::Projectile, id, p.position, &p.velocity, p.kind == ProjectileKind::TankShell ? 1.0f : 0.0f, dt);
    }
    projTrack_ = next;
    endFrame(dt);
}

// ------------------------------------------------------------------------------------------------ mixing
void AudioEngine::mix(float* out, int frames) {
    constexpr int kBlock = 256;
    if (scratch_.size() < static_cast<size_t>(kBlock)) {
        scratch_.resize(kBlock);
        revIn_.resize(kBlock);
        ambBuf_.resize(2 * kBlock);
        duckBuf_.resize(kBlock);
    }
    const float smooth = 0.002f;
    const float kHullRumbleAlpha = onePoleAlpha(150.0f);
    for (int base = 0; base < frames; base += kBlock) {
        const int nb = std::min(kBlock, frames - base);
        float* o = out + 2 * base;
        // Duck envelope: hold, then release over ~1.2 s; attack ~10 ms. Interpolated across the block.
        const float blockSec = static_cast<float>(nb) / kSampleRate;
        if (duckHold_ > 0.0f) duckHold_ -= blockSec;
        else duckDepth_ *= std::exp(-blockSec / 0.6f);
        const float duckTarget = 1.0f - clampf(duckDepth_, 0.0f, 0.8f);
        const float duck0 = duck_;
        const float k = duckTarget < duck_ ? 1.0f - std::exp(-blockSec / 0.01f) : 1.0f - std::exp(-blockSec / 0.25f);
        duck_ += (duckTarget - duck_) * k;
        for (int i = 0; i < nb; ++i) duckBuf_[i] = lerpf(duck0, duck_, static_cast<float>(i + 1) / static_cast<float>(nb));
        std::fill(revIn_.begin(), revIn_.begin() + nb, 0.0f);

        if (ambientOn_) {
            std::fill(ambBuf_.begin(), ambBuf_.begin() + 2 * nb, 0.0f);
            ambient_.render(ambBuf_.data(), nb, 1.0f);
            for (int i = 0; i < nb; ++i) {
                o[2 * i] += ambBuf_[2 * i] * duckBuf_[i];
                o[2 * i + 1] += ambBuf_[2 * i + 1] * duckBuf_[i];
            }
        }
        for (Voice& v : voices_) {
            if (!v.active) continue;
            if (v.loop) {
                const float p0 = v.curPitch;
                v.curPitch += (v.pitch - v.curPitch) * 0.3f;
                v.gen->render(scratch_.data(), nb, v.param, 0.5f * (p0 + v.curPitch));
            }
            const float dL0 = v.dL, dR0 = v.dR;
            const float dL1 = dL0 + clampf(v.tdL - dL0, -2.0f, 2.0f), dR1 = dR0 + clampf(v.tdR - dR0, -2.0f, 2.0f);
            v.dL = dL1;
            v.dR = dR1;
            v.curAlpha = v.alpha;
            const bool occluded = v.occA < 0.999f;
            const std::vector<float>* c = v.loop ? nullptr : v.clip.get();
            const double endPos = c ? static_cast<double>(c->size()) + static_cast<double>(v.tail) : 0.0;
            bool ended = false;
            for (int i = 0; i < nb; ++i) {
                float s = 0.0f;
                float tapIn[2] = {0.0f, 0.0f};
                if (c) {
                    if (v.delay > 0) {
                        --v.delay;
                        continue;
                    }
                    if (!v.started) {
                        v.started = true;
                        if (v.duckOthers > 0.0f) {
                            duckDepth_ = std::max(duckDepth_, v.duckOthers);
                            duckHold_ = 0.12f;
                        }
                    }
                    s = clipAt(*c, v.pos);
                    for (int t = 0; t < v.ntaps; ++t) tapIn[t] = clipAt(*c, v.pos - static_cast<double>(v.taps[t].delay));
                    v.pos += v.step;
                    if (v.pos >= endPos) ended = true;
                } else {
                    s = scratch_[i];
                }
                const float dk = v.duckable ? duckBuf_[i] : 1.0f;
                v.curL += smooth * (v.targetL - v.curL);
                v.curR += smooth * (v.targetR - v.curR);
                v.curSend += smooth * (v.send - v.curSend);
                s = v.lp.lp(s, v.curAlpha);
                if (v.interior) s += 0.9f * v.hullLp.lp(s, kHullRumbleAlpha);   // ~+5.6 dB hull rumble below 150 Hz
                if (occluded) s = v.occLp.lp(s, v.occA);
                if (v.spatial) {
                    s = v.rearLp.lp(s, v.rearA);
                    revIn_[i] += s * v.curSend * dk;
                    v.ring[v.widx & 63u] = s;
                    const float u = static_cast<float>(i + 1) / static_cast<float>(nb);
                    float l = ringAt(v.ring, v.widx, lerpf(dL0, dL1, u));
                    float r = ringAt(v.ring, v.widx, lerpf(dR0, dR1, u));
                    ++v.widx;
                    l = v.shL.lp(l, v.shAL);
                    r = v.shR.lp(r, v.shAR);
                    o[2 * i] += l * v.curL * dk;
                    o[2 * i + 1] += r * v.curR * dk;
                    for (int t = 0; t < v.ntaps; ++t) {
                        const float e = v.taps[t].lp.lp(tapIn[t], v.taps[t].alpha) * dk;
                        o[2 * i] += e * v.taps[t].gL;
                        o[2 * i + 1] += e * v.taps[t].gR;
                        revIn_[i] += e * v.curSend * 0.5f;
                    }
                } else {
                    o[2 * i] += s * v.curL;
                    o[2 * i + 1] += s * v.curR;
                }
                if (ended) break;
            }
            if (ended) v.active = false;
            if (v.releasing && v.curL < 1e-4f && v.curR < 1e-4f) v.active = false;
        }
        if (reverbOn_) reverb_.process(revIn_.data(), o, nb);
        clock_ += static_cast<uint64_t>(nb);
    }
}

void AudioEngine::render(float* out, int frames) {
    if (frames <= 0) return;
    std::fill(out, out + 2 * static_cast<size_t>(frames), 0.0f);
    std::lock_guard<std::mutex> lk(mu_);
    mix(out, frames);
    // Master bus: volume, look-ahead peak limiter, soft-knee clipper (never above kCeiling).
    const float vol = masterVolume_;
    for (size_t i = 0; i < 2 * static_cast<size_t>(frames); ++i) out[i] *= vol;
    limiter_.process(out, frames);
}

void AudioEngine::renderOffline(float seconds, std::vector<float>& out) {
    const double total = static_cast<double>(std::max(seconds, 0.0f)) * kSampleRate + offlineCarry_;
    const long frames = static_cast<long>(std::floor(total + 1e-6));
    offlineCarry_ = std::max(0.0, total - static_cast<double>(frames));
    out.assign(static_cast<size_t>(frames) * 2, 0.0f);
    render(out.data(), static_cast<int>(frames));
}

}  // namespace ts::audio
