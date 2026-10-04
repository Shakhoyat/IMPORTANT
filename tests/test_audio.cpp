// T160-T169: procedural audio (synthesis, spatialisation, mixer, offline render, WAV). CPU only, no device.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "TestKit.h"
#include "audio/AudioEngine.h"
#include "audio/Spatial.h"
#include "audio/Synth.h"
#include "audio/Wav.h"
#include "scene/Scene.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;
using namespace ts::audio;

namespace {

bool allFinite(const std::vector<float>& v) {
    for (float f : v) if (!std::isfinite(f)) return false;
    return true;
}
double energy(const std::vector<float>& v) {
    double e = 0;
    for (float f : v) e += static_cast<double>(f) * f;
    return e;
}
float peakOf(const std::vector<float>& v) {
    float m = 0;
    for (float f : v) m = std::max(m, std::fabs(f));
    return m;
}
template <class Gen>
std::vector<float> renderGen(uint32_t seed, float param, float pitch) {
    Gen g(seed);
    std::vector<float> out(48000), blk(256);
    for (size_t i = 0; i < out.size(); i += 256) {
        const int n = static_cast<int>(std::min<size_t>(256, out.size() - i));
        g.render(blk.data(), n, param, pitch);
        std::copy(blk.begin(), blk.begin() + n, out.begin() + static_cast<long>(i));
    }
    return out;
}


}  // namespace

TS_TEST_GROUP(Audio) {
    // T160 every synth is deterministic and finite
    {
        struct Item { const char* name; std::vector<float> a, b; };
        std::vector<Item> items;
        items.push_back({"explosion", synthExplosion(1.0f, 1), synthExplosion(1.0f, 1)});
        items.push_back({"cannon", synthCannon(), synthCannon()});
        items.push_back({"gatling", synthGatling(), synthGatling()});
        items.push_back({"whoosh", synthWhoosh(), synthWhoosh()});
        items.push_back({"door", synthDoor(), synthDoor()});
        items.push_back({"siren", synthSiren(), synthSiren()});
        items.push_back({"alarm", synthAlarm(), synthAlarm()});
        items.push_back({"lock", synthLockTone(), synthLockTone()});
        items.push_back({"chirp", synthConfirmChirp(), synthConfirmChirp()});
        items.push_back({"kill", synthKillConfirm(), synthKillConfirm()});
        items.push_back({"drone", renderGen<RotorHumGen>(1, 6.0f, 1.0f), renderGen<RotorHumGen>(1, 6.0f, 1.0f)});
        items.push_back({"heli", renderGen<HeliThumpGen>(2, 1.0f, 1.0f), renderGen<HeliThumpGen>(2, 1.0f, 1.0f)});
        items.push_back({"tank", renderGen<TankEngineGen>(3, 3.0f, 1.0f), renderGen<TankEngineGen>(3, 3.0f, 1.0f)});
        items.push_back({"rocket", renderGen<RocketGen>(4, 0.0f, 1.0f), renderGen<RocketGen>(4, 0.0f, 1.0f)});
        std::vector<float> amb1(2 * 48000, 0.0f), amb2(2 * 48000, 0.0f);
        AmbientGen a1(5), a2(5);
        a1.render(amb1.data(), 48000, 1.0f);
        a2.render(amb2.data(), 48000, 1.0f);
        items.push_back({"ambient", amb1, amb2});
        bool det = true, fin = true, audible = true;
        std::string bad;
        for (const Item& it : items) {
            const bool same = it.a == it.b;
            const bool f = allFinite(it.a) && !it.a.empty();
            const float pk = peakOf(it.a);
            if (!same || !f || pk < 1e-3f || pk > 3.0f) bad += std::string(it.name) + " ";
            det = det && same; fin = fin && f; audible = audible && pk > 1e-3f && pk <= 3.0f;
        }
        record("T160", "all 15 synths are deterministic, finite and audible (1e-3 < peak < 3)", "identical on two runs",
               det && fin && audible ? "all ok" : "failing: " + bad, det && fin && audible);
    }
    // T161 explosion energy scales with power
    {
        const double e1 = energy(synthExplosion(0.3f)), e2 = energy(synthExplosion(1.0f)), e3 = energy(synthExplosion(3.0f));
        record("T161", "explosion energy grows with power", "E(0.3) < E(1) < E(3)",
               fmt("%.1f < %.1f < %.1f", e1, e2, e3), e1 < e2 && e2 < e3);
    }
    // T162 equal-power pan
    {
        double worst = 0;
        for (int i = -10; i <= 10; ++i) {
            float l, r;
            panGains(static_cast<float>(i) / 10.0f, l, r);
            worst = std::max(worst, std::fabs(static_cast<double>(l) * l + static_cast<double>(r) * r - 1.0));
        }
        float l0, r0, lm, rm, lp, rp;
        panGains(0.0f, l0, r0); panGains(-1.0f, lm, rm); panGains(1.0f, lp, rp);
        const bool ok = worst < 1e-5 && std::fabs(l0 - r0) < 1e-6f && rm < 1e-6f && lp < 1e-6f && lm > 0.999f && rp > 0.999f;
        record("T162", "pan law is equal-power (L^2 + R^2 = 1), hard left / right / centre", "error < 1e-5",
               fmt("max |L2+R2-1| = %.2e, centre %.3f/%.3f", worst, l0, r0), ok);
        // direction convention: a source on the listener's right goes to the right channel
        const Spatial s = computeSpatial({0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {10, 0, 0}, {0, 0, 0}, 3.0f, 1.0f);
        record("T162b", "source on the listener's right is louder in the right channel", "R > 5 L",
               fmt("L %.3f R %.3f", s.gainL, s.gainR), s.gainR > 5.0f * s.gainL);
    }
    // T163 attenuation monotonic with a near clamp, air absorption monotonic
    {
        bool mono = true;
        float prev = 2.0f, prevC = 1e9f;
        for (float d = 0.0f; d < 400.0f; d += 0.5f) {
            const float a = attenuation(d), c = airCutoffHz(d);
            mono = mono && a <= prev + 1e-7f && c <= prevC + 1e-3f;
            prev = a; prevC = c;
        }
        const bool near = attenuation(0.0f) == 1.0f && attenuation(2.9f) == 1.0f;
        record("T163", "attenuation and air-absorption cutoff are monotonic; gain is 1 inside the near clamp",
               "non-increasing", fmt("mono %d, clamp %d, g(30 m) %.3f, g(120 m) %.3f", mono, near, attenuation(30.0f), attenuation(120.0f)),
               mono && near);
        const float up = dopplerRatio({0, 0, 0}, {0, 0, 0}, {50, 0, 0}, {-30, 0, 0});
        const float down = dopplerRatio({0, 0, 0}, {0, 0, 0}, {50, 0, 0}, {30, 0, 0});
        record("T163b", "Doppler: approaching source is pitched up, receding down, light (within 0.9..1.1)", "down < 1 < up",
               fmt("up %.3f down %.3f", up, down), up > 1.0f && down < 1.0f && up <= 1.1f && down >= 0.9f);
    }
    // T164 offline render length exact
    {
        AudioEngine e;
        std::vector<float> out;
        e.renderOffline(0.5f, out);
        const bool a = out.size() == static_cast<size_t>(0.5f * 48000 * 2);
        size_t total = 0;
        for (int i = 0; i < 30; ++i) { e.renderOffline(1.0f / 30.0f, out); total += out.size(); }
        e.renderOffline(0.0f, out);
        const bool z = out.empty();
        record("T164", "offline render length is exactly seconds x 48000 x 2 (and 30 x 1/30 s = 1 s)", "48000 / 96000 samples",
               fmt("0.5 s -> %zu, 30 x 1/30 s -> %zu", static_cast<size_t>(0.5f * 48000 * 2), total), a && z && total == 96000);
    }
    // T165 an event produces an onset within one frame
    {
        AudioEngine e;
        e.setAmbientEnabled(false);
        e.setListener({0, 1.7f, 0}, {0, 0, -1}, {0, 1, 0}, 1.0f / 60.0f);
        std::vector<float> before, after;
        e.renderOffline(1.0f / 60.0f, before);
        GameEvent ev;
        ev.type = GameEventType::Explosion;
        ev.position = {6.0f, 1.0f, -8.0f};
        ev.value = 100;
        e.handleEvent(ev);
        e.renderOffline(1.0f / 60.0f, after);
        size_t onset = after.size();
        for (size_t i = 0; i < after.size(); ++i) if (std::fabs(after[i]) > 1e-3f) { onset = i / 2; break; }
        record("T165", "an explosion event is audible within the next frame (silence before)", "pre peak 0, onset < 800 frames",
               fmt("pre peak %.4f, onset frame %zu, peak %.3f", peakOf(before), onset, peakOf(after)),
               peakOf(before) == 0.0f && onset < 800 && peakOf(after) > 0.05f);
    }
    // T166 voice limit respected, the loudest voice survives stealing
    {
        AudioEngine e;
        e.setAmbientEnabled(false);
        e.setListener({0, 1.7f, 0}, {0, 0, -1}, {0, 1, 0}, 1.0f / 60.0f);
        GameEvent ev;
        ev.type = GameEventType::Explosion;
        ev.value = 100;
        ev.position = {0, 1.7f, -4.0f};  // the loud one first
        e.handleEvent(ev);
        int maxSeen = 0;
        for (int i = 0; i < 200; ++i) {
            ev.position = {static_cast<float>(40 + i), 1.0f, -60.0f};
            e.handleEvent(ev);
            e.playClip(Clip::Cannon, {static_cast<float>(i), 1.0f, 30.0f});
            maxSeen = std::max(maxSeen, e.voiceCount());
        }
        for (int i = 0; i < 40; ++i) e.setSource(Loop::Projectile, i, {static_cast<float>(i), 5, 10}, nullptr, 0.0f, 1.0f / 60.0f);
        maxSeen = std::max(maxSeen, e.voiceCount());
        std::vector<float> out;
        e.renderOffline(0.1f, out);
        record("T166", "voice pool never exceeds the limit under 400 one-shots + 40 sources; the loud near blast survives",
               fmt("<= %d voices", AudioEngine::voiceLimit()), fmt("max %d, peak after %.3f", maxSeen, peakOf(out)),
               maxSeen <= AudioEngine::voiceLimit() && peakOf(out) > 0.2f);
    }
    // T167 WAV header
    {
        std::vector<float> s(2 * 480, 0.25f);
        const std::vector<uint8_t> w = makeWav16(s, 48000, 2);
        auto u32 = [&](size_t o) { return static_cast<uint32_t>(w[o]) | static_cast<uint32_t>(w[o + 1]) << 8 | static_cast<uint32_t>(w[o + 2]) << 16 | static_cast<uint32_t>(w[o + 3]) << 24; };
        auto u16 = [&](size_t o) { return static_cast<uint32_t>(w[o]) | static_cast<uint32_t>(w[o + 1]) << 8; };
        const bool tags = std::string(w.begin(), w.begin() + 4) == "RIFF" && std::string(w.begin() + 8, w.begin() + 12) == "WAVE" &&
                          std::string(w.begin() + 12, w.begin() + 16) == "fmt " && std::string(w.begin() + 36, w.begin() + 40) == "data";
        const bool fieldsOk = u32(4) == w.size() - 8 && u32(16) == 16 && u16(20) == 1 && u16(22) == 2 && u32(24) == 48000 &&
                              u32(28) == 48000 * 4 && u16(32) == 4 && u16(34) == 16 && u32(40) == s.size() * 2 && w.size() == 44 + s.size() * 2;
        const int16_t first = static_cast<int16_t>(u16(44));
        // streaming writer round trip
        const std::string path = "build/test_audio_tmp.wav";
        bool streamOk = false;
        {
            WavWriter ww;
            if (ww.open(path)) {
                ww.write(s.data(), 480);
                ww.write(s.data() + 480, s.size() - 480);
                streamOk = ww.close();
            }
        }
        std::vector<uint8_t> file;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            file.resize(w.size());
            const size_t got = std::fread(file.data(), 1, file.size(), f);
            file.resize(got);
            std::fclose(f);
            std::remove(path.c_str());
        }
        record("T167", "WAV header is valid (RIFF/WAVE/fmt/data, PCM16, 48 kHz stereo, sizes) and the streaming writer matches",
               "44-byte header, sizes consistent", fmt("tags %d fields %d first sample %d stream %d", tags, fieldsOk, first, streamOk && file == w),
               tags && fieldsOk && first == 8192 && streamOk && file == w);
    }
    // T168 master limiter: no clipping even with a stress mix and master volume 2
    {
        AudioEngine e;
        e.setMasterVolume(2.0f);
        e.setListener({0, 1.7f, 0}, {0, 0, -1}, {0, 1, 0}, 1.0f / 60.0f);
        GameEvent ev;
        ev.type = GameEventType::Explosion;
        ev.value = 400;
        float pk = 0;
        for (int i = 0; i < 12; ++i) {
            ev.position = {static_cast<float>(i % 3), 1.0f, -3.0f - static_cast<float>(i)};
            e.handleEvent(ev);
            e.playClip(Clip::Siren, {0, 3, -2});
            std::vector<float> out;
            e.renderOffline(0.25f, out);
            pk = std::max(pk, peakOf(out));
        }
        record("T168", "master soft limiter keeps the peak below -1 dBFS (0.891) at master volume 2", "peak < 0.891",
               fmt("peak %.4f (%.2f dBFS)", pk, 20.0 * std::log10(std::max(pk, 1e-9f))), pk < 0.891f && pk > 0.5f);
    }
    // T169 scene integration: update() reads the scene, creates a drone voice, stays finite, events are not repeated
    {
        auto scene = std::make_unique<Scene>();
        AudioEngine e;
        e.setAmbientEnabled(false);
        Detection det;
        for (int i = 0; i < 120; ++i) scene->update(1.0f / 60.0f, det);
        std::vector<float> out;
        bool finite = true;
        for (int i = 0; i < 30; ++i) {
            e.update(*scene, {0, 1.7f, 14.0f}, {0, 0, -1}, {0, 1, 0}, 1.0f / 30.0f);
            e.renderOffline(1.0f / 30.0f, out);
            finite = finite && allFinite(out);
        }
        const int voices = e.voiceCount();
        const float pk = peakOf(out);
        record("T169", "update(scene) creates the drone rotor voice (and the helicopter / tank if present), output finite",
               ">= 1 continuous voice, audible", fmt("voices %d, peak %.3f, finite %d", voices, pk, finite), voices >= 1 && pk > 0.005f && finite);
    }
}
