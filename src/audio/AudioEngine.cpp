#include "audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "scene/Controllable.h"
#include "scene/Scene.h"

namespace ts::audio {

namespace {

struct LoopParams { float gain, ref; };
LoopParams loopParams(Loop k) {
    switch (k) {
        case Loop::Drone: return {0.9f, 4.0f};
        case Loop::Helicopter: return {1.0f, 8.0f};
        case Loop::Tank: return {0.9f, 6.0f};
        case Loop::Projectile: return {0.8f, 5.0f};
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
        default: return 1.0f;
    }
}
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

}  // namespace

AudioEngine::AudioEngine() : ambient_(99) { voices_.resize(kMaxVoices); }
AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::setMasterVolume(float v) {
    std::lock_guard<std::mutex> lk(mu_);
    masterVolume_ = clampf(v, 0.0f, 2.0f);
}
void AudioEngine::setAmbientEnabled(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    ambientOn_ = on;
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

// ------------------------------------------------------------------------------------------------ clips
std::shared_ptr<const std::vector<float>> AudioEngine::getClip(Clip c, float power) {
    const int bucket = c == Clip::Explosion ? static_cast<int>(std::lround(clampf(power, 0.05f, 4.0f) * 10.0f)) : 0;
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
}

// ------------------------------------------------------------------------------------------------ voices
void AudioEngine::applySpatial(Voice& v) {
    if (!v.spatial) {
        v.targetL = v.targetR = v.gain * 0.7071f;
        v.alpha = 1.0f;
        v.pitch = 1.0f;
        return;
    }
    const Spatial s = computeSpatial(lpos_, lvel_, lright_, v.posW, v.velW, v.refDistance, v.gain);
    v.targetL = s.gainL;
    v.targetR = s.gainR;
    v.alpha = s.lpAlpha;
    v.pitch = s.doppler;
}

AudioEngine::Voice* AudioEngine::allocate(float loudness) {
    Voice* quietest = nullptr;
    for (Voice& v : voices_) {
        if (!v.active) return &v;
        if (!quietest || v.loudness() < quietest->loudness()) quietest = &v;
    }
    if (quietest && loudness > quietest->loudness()) return quietest;  // steal the quietest
    return nullptr;                                                    // the new sound is the quietest: drop it
}

void AudioEngine::setListener(const Vec3& pos, const Vec3& forward, const Vec3& up, float dt) {
    std::lock_guard<std::mutex> lk(mu_);
    const Vec3 p = finiteVec(pos);
    if (haveListener_ && dt > 1e-5f) {
        Vec3 raw = (p - lpos_) / dt;
        if (length(raw) > 40.0f) raw = Vec3{};  // camera cut, not motion
        lvel_ = lvel_ + (raw - lvel_) * 0.5f;
    }
    lpos_ = p;
    haveListener_ = true;
    Vec3 r = cross(finiteVec(forward), finiteVec(up));
    if (length(r) > 1e-4f) lright_ = normalize(r);
}

void AudioEngine::playClip(Clip c, const Vec3& pos, bool spatial, float gain, float power) {
    const auto clip = getClip(c, power);  // synthesised outside the audio lock
    std::lock_guard<std::mutex> lk(mu_);
    Voice tmp;
    tmp.spatial = spatial;
    tmp.posW = finiteVec(pos);
    tmp.refDistance = clipRef(c);
    tmp.gain = gain;
    applySpatial(tmp);
    Voice* v = allocate(tmp.loudness());
    if (!v) return;
    *v = Voice{};
    v->active = true;
    v->spatial = spatial;
    v->clip = clip;
    v->posW = tmp.posW;
    v->refDistance = tmp.refDistance;
    v->gain = gain;
    v->targetL = v->curL = tmp.targetL;  // snap: the onset must be audible within the first block
    v->targetR = v->curR = tmp.targetR;
    v->alpha = v->curAlpha = tmp.alpha;
    v->step = tmp.pitch;
}

void AudioEngine::handleEvent(const GameEvent& e) {
    switch (e.type) {
        case GameEventType::WaveStart: playClip(Clip::Siren, e.position, true, 0.35f); break;
        case GameEventType::EnemySpotted: playClip(Clip::ConfirmChirp, e.position, false, 0.5f); break;
        case GameEventType::LockOn: playClip(Clip::LockTone, e.position, false, 0.6f); break;
        case GameEventType::RoundLaunched: playClip(Clip::Whoosh, e.position, true, 1.0f); break;
        case GameEventType::CombinedStrike: playClip(Clip::ConfirmChirp, e.position, false, 0.6f); break;
        case GameEventType::Explosion:
            playClip(Clip::Explosion, e.position, true, 1.0f, static_cast<float>(e.value) / 100.0f);
            break;
        case GameEventType::EnemyDestroyed: playClip(Clip::KillConfirm, e.position, false, 0.6f); break;
        case GameEventType::Breach: playClip(Clip::Alarm, e.position, true, 0.8f); break;
        case GameEventType::BaseLost: playClip(Clip::Siren, e.position, true, 0.4f); break;
        case GameEventType::CannonFire: playClip(Clip::Cannon, e.position, true, 1.0f); break;
        case GameEventType::GatlingBurst: playClip(Clip::Gatling, e.position, true, 0.9f); break;
        case GameEventType::DoorMoving: playClip(Clip::Door, e.position, true, 0.8f); break;
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

void AudioEngine::setSource(Loop kind, int index, const Vec3& pos, const Vec3* vel, float param, float dt) {
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
        v = allocate(tmp.loudness());
        if (!v) return;
        *v = Voice{};
        v->active = v->loop = true;
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
        }
        applySpatial(*v);
        v->curL = 0.0f;  // continuous sources fade in
        v->curR = 0.0f;
        v->curAlpha = v->alpha;
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
    applySpatial(*v);
}

void AudioEngine::endFrame(float) {
    std::lock_guard<std::mutex> lk(mu_);
    for (Voice& v : voices_) {
        if (!v.active || !v.loop) continue;
        if (!v.seen) {
            v.releasing = true;
            v.targetL = v.targetR = 0.0f;
        }
        v.seen = false;
    }
}

void AudioEngine::update(const Scene& scene, const Vec3& listenerPos, const Vec3& forward, const Vec3& up, float dt) {
    setListener(listenerPos, forward, up, dt);
    pollEvents(scene);

    for (int i = 0; i < kUavCount; ++i) {  // both patrol UAVs
        const DroneState& d = scene.droneState(i);
        setSource(Loop::Drone, i, d.position, &d.velocity, length(d.velocity), dt);
    }

    int i = 0;
    for (const auto& c : scene.controllables()) {
        const std::string name = c->name();
        const std::string st = c->status();
        if (name.find("HELICOPTER") != std::string::npos)
            setSource(Loop::Helicopter, i, c->position(), nullptr, clampf(parseAfter(st, "ROTOR ", 0.0f) / 100.0f, 0.0f, 1.0f), dt);
        else if (name.find("TANK") != std::string::npos)
            setSource(Loop::Tank, i, c->position(), nullptr, parseAfter(st, "SPEED ", 0.0f), dt);
        ++i;
    }

    // Rounds in flight: match to last frame's tracks by predicted position so each keeps its voice.
    for (ProjTrack& t : projTrack_) t.used = false;
    std::vector<ProjTrack> next;
    int count = 0;
    for (const Projectile& p : scene.combat().projectiles()) {
        if (!p.alive || p.kind == ProjectileKind::Tracer || count >= 6) continue;
        ++count;
        int best = -1;
        float bestD = 6.0f + length(p.velocity) * std::max(dt, 0.02f);
        for (size_t k = 0; k < projTrack_.size(); ++k) {
            if (projTrack_[k].used) continue;
            const float dist = length((projTrack_[k].p + projTrack_[k].v * dt) - p.position);
            if (dist < bestD) { bestD = dist; best = static_cast<int>(k); }
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
    if (scratch_.size() < static_cast<size_t>(kBlock)) scratch_.resize(kBlock);
    for (int base = 0; base < frames; base += kBlock) {
        const int nb = std::min(kBlock, frames - base);
        float* o = out + 2 * base;
        if (ambientOn_) ambient_.render(o, nb, 1.0f);
        for (Voice& v : voices_) {
            if (!v.active) continue;
            if (v.loop) v.gen->render(scratch_.data(), nb, v.param, v.pitch);
            else {
                const std::vector<float>& c = *v.clip;
                const double size = static_cast<double>(c.size());
                for (int i = 0; i < nb; ++i) {
                    const size_t i0 = static_cast<size_t>(v.pos);
                    if (i0 + 1 >= c.size()) { std::fill(scratch_.begin() + i, scratch_.begin() + nb, 0.0f); v.active = false; break; }
                    const float f = static_cast<float>(v.pos - static_cast<double>(i0));
                    scratch_[i] = c[i0] + (c[i0 + 1] - c[i0]) * f;
                    v.pos += v.step;
                    if (v.pos >= size) v.pos = size;
                }
            }
            v.curAlpha = v.alpha;
            const float smooth = 0.002f;
            for (int i = 0; i < nb; ++i) {
                v.curL += smooth * (v.targetL - v.curL);
                v.curR += smooth * (v.targetR - v.curR);
                const float s = v.lp.lp(scratch_[i], v.curAlpha);
                o[2 * i] += s * v.curL;
                o[2 * i + 1] += s * v.curR;
            }
            if (v.releasing && v.curL < 1e-4f && v.curR < 1e-4f) v.active = false;
        }
    }
}

void AudioEngine::render(float* out, int frames) {
    if (frames <= 0) return;
    std::fill(out, out + 2 * static_cast<size_t>(frames), 0.0f);
    std::lock_guard<std::mutex> lk(mu_);
    mix(out, frames);
    // Master bus: volume then a tanh soft limiter that can never exceed the ceiling.
    const float vol = masterVolume_;
    for (size_t i = 0; i < 2 * static_cast<size_t>(frames); ++i) {
        float x = out[i] * vol;
        if (!std::isfinite(x)) x = 0.0f;
        out[i] = kCeiling * std::tanh(x / kCeiling);
    }
}

void AudioEngine::renderOffline(float seconds, std::vector<float>& out) {
    const double total = static_cast<double>(std::max(seconds, 0.0f)) * kSampleRate + offlineCarry_;
    const long frames = static_cast<long>(std::floor(total + 1e-6));
    offlineCarry_ = std::max(0.0, total - static_cast<double>(frames));
    out.assign(static_cast<size_t>(frames) * 2, 0.0f);
    render(out.data(), static_cast<int>(frames));
}

}  // namespace ts::audio
