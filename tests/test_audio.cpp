// T160-T169: procedural audio (synthesis, spatialisation, mixer, offline render, WAV). CPU only, no device.
// T300-T309 (v7, WP-L): see the AudioV7 group at the end of the file.
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

// ================================================================================================ v7 (WP-L)
// T300-T309: game audio v7 (foley, UI cues, vehicles, time-of-day ambience, HRTF-lite, Doppler, bunker reverb,
// echoes, ducking, limiter / loudness, determinism). With TS_AUDIO_SAMPLES=<dir> the group also writes listening
// samples (WAV) into <dir>.
#include <cstdlib>

#include "audio/Acoustics.h"
#include "objects/Terrain.h"

using ASurface = ts::audio::Surface;   // ts::Surface exists elsewhere

namespace {

double rmsDb(const std::vector<float>& v, size_t from = 0, size_t to = 0) {
    if (to == 0 || to > v.size()) to = v.size();
    double e = 0;
    for (size_t i = from; i < to; ++i) e += static_cast<double>(v[i]) * v[i];
    const double r = std::sqrt(e / std::max<double>(1.0, static_cast<double>(to - from)));
    return 20.0 * std::log10(std::max(r, 1e-12));
}
// Goertzel power of 'f' Hz over mono x[from, from + n).
double goertzel(const std::vector<float>& x, size_t from, size_t n, double f, int stride = 1, int ch = 0) {
    const double w = 2.0 * 3.14159265358979 * f / 48000.0, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t k = (from + i) * static_cast<size_t>(stride) + static_cast<size_t>(ch);
        if (k >= x.size()) break;
        const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * static_cast<double>(i) / static_cast<double>(n));
        const double s0 = x[k] * win + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}
// Energy in [f0, f1] Hz (sum of Goertzel bins every 'step' Hz) of a mono buffer window.
double bandEnergy(const std::vector<float>& x, size_t from, size_t n, double f0, double f1, double step = 50.0,
                  int stride = 1, int ch = 0) {
    double e = 0;
    for (double f = f0; f <= f1; f += step) e += goertzel(x, from, n, f, stride, ch);
    return e;
}
// Spectral centroid (Hz) from Goertzel bins 100 Hz .. 12 kHz.
double centroid(const std::vector<float>& x) {
    const size_t n = std::min<size_t>(x.size(), 8192);
    double num = 0, den = 0;
    for (double f = 100; f <= 12000; f += 100) {
        const double p = goertzel(x, 0, n, f);
        num += f * p; den += p;
    }
    return den > 0 ? num / den : 0;
}
// Welch power spectrum (4096-point Hann windows, 50 % overlap) of x[from, end); radix-2 FFT.
std::vector<double> welch(const std::vector<float>& x, size_t from) {
    const size_t N = 4096;
    std::vector<double> p(N / 2, 0.0);
    int count = 0;
    std::vector<double> re(N), im(N);
    for (size_t s = from; s + N <= x.size(); s += N / 2, ++count) {
        for (size_t i = 0; i < N; ++i) {
            re[i] = x[s + i] * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * static_cast<double>(i) / N));
            im[i] = 0.0;
        }
        for (size_t i = 1, j = 0; i < N; ++i) {
            size_t bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (size_t len = 2; len <= N; len <<= 1) {
            const double ang = -2.0 * 3.14159265358979 / static_cast<double>(len);
            for (size_t i = 0; i < N; i += len)
                for (size_t k = 0; k < len / 2; ++k) {
                    const double wr = std::cos(ang * k), wi = std::sin(ang * k);
                    const size_t a = i + k, b = i + k + len / 2;
                    const double vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - vr; im[b] = im[a] - vi;
                    re[a] += vr; im[a] += vi;
                }
        }
        for (size_t i = 0; i < N / 2; ++i) p[i] += re[i] * re[i] + im[i] * im[i];
    }
    for (double& v : p) v /= std::max(count, 1);
    return p;
}
double psdBandDb(const std::vector<double>& p, double f0, double f1) {
    double e = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const double f = static_cast<double>(i) * 48000.0 / 4096.0;
        if (f >= f0 && f < f1) e += p[i];
    }
    return 10.0 * std::log10(e + 1e-30);
}
std::vector<float> mono(const std::vector<float>& st) {
    std::vector<float> m(st.size() / 2);
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (st[2 * i] + st[2 * i + 1]);
    return m;
}
void append(std::vector<float>& dst, const std::vector<float>& src) { dst.insert(dst.end(), src.begin(), src.end()); }
std::vector<float> stereoOf(const std::vector<float>& m, float gain = 1.0f) {
    std::vector<float> s(2 * m.size());
    for (size_t i = 0; i < m.size(); ++i) s[2 * i] = s[2 * i + 1] = m[i] * gain;
    return s;
}
const char* sampleDir() { return std::getenv("TS_AUDIO_SAMPLES"); }
void saveSample(const std::string& name, const std::vector<float>& stereo) {
    if (const char* d = sampleDir()) writeWav16(std::string(d) + "/" + name + ".wav", stereo);
}
// Engine with a listener at the origin looking down -Z.
void listenAt(AudioEngine& e, const Vec3& p, float bunker = -1.0f) {
    ListenerState l;
    l.position = p;
    l.hasVelocity = true;
    l.bunker = bunker;
    e.setListener(l, 1.0f / 60.0f);
}
// A continuous source moving along a line past the listener; returns the stereo render.
std::vector<float> flyBy(AudioEngine& e, Loop kind, float param, const Vec3& start, const Vec3& vel, float seconds,
                         std::vector<float>* pitches = nullptr) {
    std::vector<float> all, blk;
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < static_cast<int>(seconds * 60.0f); ++f) {
        const Vec3 p = start + vel * (dt * static_cast<float>(f));
        e.setSource(kind, 0, p, &vel, param, dt);
        e.endFrame(dt);
        if (pitches) pitches->push_back(e.sourcePitch(kind, 0));
        e.renderOffline(dt, blk);
        append(all, blk);
    }
    return all;
}

}  // namespace

TS_TEST_GROUP(AudioV7) {
    // T300 every v7 synth is deterministic, finite and audible
    {
        struct Item { std::string name; std::vector<float> a, b; };
        std::vector<Item> items;
        for (int s = 0; s < kSurfaceCount; ++s)
            items.push_back({std::string("step_") + surfaceName(static_cast<ASurface>(s)), synthFootstep(static_cast<ASurface>(s), 2),
                             synthFootstep(static_cast<ASurface>(s), 2)});
        items.push_back({"radio", synthRadioChatter(3), synthRadioChatter(3)});
        for (int c = 0; c < kUiCueCount; ++c)
            items.push_back({uiCueName(static_cast<UiCue>(c)), synthUiCue(static_cast<UiCue>(c)), synthUiCue(static_cast<UiCue>(c))});
        items.push_back({"rover", renderGen<RoverWhineGen>(6, 1.2f, 1.0f), renderGen<RoverWhineGen>(6, 1.2f, 1.0f)});
        items.push_back({"generator", renderGen<GeneratorGen>(7, 0.6f, 1.0f), renderGen<GeneratorGen>(7, 0.6f, 1.0f)});
        items.push_back({"tank_drive", renderGen<TankEngineGen>(3, 6.0f, 1.0f), renderGen<TankEngineGen>(3, 6.0f, 1.0f)});
        std::vector<float> d1(2 * 48000, 0.0f), d2(2 * 48000, 0.0f);
        AmbientGen a1(5), a2(5);
        a1.setTimeOfDay(7.0f, 1.0f); a2.setTimeOfDay(7.0f, 1.0f);
        a1.render(d1.data(), 48000, 1.0f); a2.render(d2.data(), 48000, 1.0f);
        items.push_back({"ambient_day", d1, d2});
        bool ok = true;
        std::string bad;
        for (const Item& it : items) {
            const float pk = peakOf(it.a);
            const bool good = it.a == it.b && allFinite(it.a) && !it.a.empty() && pk > 1e-3f && pk <= 1.5f;
            if (!good) bad += it.name + fmt("(%.3f) ", pk);
            ok = ok && good;
        }
        record("T300", fmt("all %zu v7 synths (5 footstep surfaces, radio, 9 UI cues, rover, generator, tank, day bed) are "
                           "deterministic, finite and audible", items.size()),
               "identical twice, 1e-3 < peak <= 1.5", ok ? "all ok" : "failing: " + bad, ok);
    }
    // T301 footsteps sound like their surface; the surface map follows the camp layout
    {
        double c[kSurfaceCount];
        for (int s = 0; s < kSurfaceCount; ++s) {
            double sum = 0;
            for (uint32_t v = 0; v < 3; ++v) sum += centroid(synthFootstep(static_cast<ASurface>(s), v));
            c[s] = sum / 3.0;
        }
        const double dirt = c[0], gravel = c[1], concrete = c[2], metal = c[3], grass = c[4];
        const bool spectra = gravel > dirt * 1.5 && concrete > dirt * 1.5 && metal > dirt * 1.2 && grass > dirt;
        const ASurface sBunker = surfaceAt({0.0f, -3.4f, -2.0f});
        const ASurface sPad = surfaceAt({20.3f, -0.3f, 17.0f});
        const ASurface sRoad = surfaceAt({0.0f, -0.3f, 26.0f});
        const ASurface sIn = surfaceAt({8.0f, terrainHeight(8.0f, -6.0f), -6.0f});
        const ASurface sOut = surfaceAt({60.0f, -0.3f, 40.0f});
        const ASurface sDeck = surfaceAt({10.5f, 4.0f, 10.0f});
        const bool map = sBunker == ASurface::Concrete && sPad == ASurface::Concrete && sRoad == ASurface::Gravel &&
                         sIn == ASurface::Dirt && sOut == ASurface::Grass && sDeck == ASurface::Metal;
        record("T301", "footstep spectra differ by surface (centroid: gravel / concrete >> dirt); surfaceAt() maps bunker, "
                       "helipad, road, compound, outside, tower deck",
               "ordering + 6/6 map", fmt("centroid dirt %.0f gravel %.0f concrete %.0f metal %.0f grass %.0f Hz; map %d",
                                         dirt, gravel, concrete, metal, grass, map),
               spectra && map);
    }
    // T302 HRTF-lite: ITD sign / size, ILD, head shadow, rear filter; measured on a rendered click
    {
        const SpatialHrtf r = computeHrtf({0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {0, 0, -1}, {10, 0, 0}, {0, 0, 0}, 3.0f, 1.0f);
        const SpatialHrtf b = computeHrtf({0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 10}, {0, 0, 0}, 3.0f, 1.0f);
        const SpatialHrtf f = computeHrtf({0, 0, 0}, {0, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, -10}, {0, 0, 0}, 3.0f, 1.0f);
        const float maxItdMs = r.delayL / 48.0f;
        const bool pure = r.delayL > 25.0f && r.delayR == 0.0f && maxItdMs < 0.70f && r.gainR > 2.5f * r.gainL &&
                          r.shadowHzL < 2500.0f && r.shadowHzR >= 19999.0f && b.rearHz < 7000.0f && f.rearHz >= 19999.0f &&
                          std::fabs(f.gainL - f.gainR) < 1e-6f;
        // rendered: a click on the right -> the right channel leads by the ITD
        AudioEngine e;
        e.setAmbientEnabled(false);
        e.setReverbEnabled(false);
        listenAt(e, {0, 1.7f, 0});
        e.playClip(Clip::Cannon, {8.0f, 1.7f, 0.0f}, true, 1.0f);   // broadband crack: unambiguous lag
        std::vector<float> out;
        e.renderOffline(0.2f, out);
        int bestLag = 0;
        double best = -1e30;
        for (int lag = -40; lag <= 40; ++lag) {
            double s = 0;
            for (size_t i = 100; i < 6000; ++i) {
                const long j = static_cast<long>(i) + lag;
                s += static_cast<double>(out[2 * i + 1]) * out[2 * static_cast<size_t>(j)];   // R(t) . L(t + lag)
            }
            if (s > best) { best = s; bestLag = lag; }
        }
        std::vector<float> L(out.size() / 2), R(out.size() / 2);
        for (size_t i = 0; i < L.size(); ++i) { L[i] = out[2 * i]; R[i] = out[2 * i + 1]; }
        const double ild = rmsDb(R) - rmsDb(L);
        record("T302", "HRTF-lite: source at +90 deg -> left ear lags (Woodworth ITD < 0.7 ms), R louder, left head shadow; "
                       "rear source low-passed; rendered: L lags R by the ITD",
               "ITD 26..40 samples (+ head-shadow group delay), ILD > 6 dB",
               fmt("ITD %.1f smp (%.3f ms), gains L %.3f R %.3f, shadow %.0f Hz, rear %.0f Hz; rendered lag %d smp, ILD %.1f dB",
                   r.delayL, maxItdMs, r.gainL, r.gainR, r.shadowHzL, b.rearHz, bestLag, ild),
               pure && bestLag >= 26 && bestLag <= 40 && ild > 6.0);
    }
    // T303 Doppler: a rover driving past at 25 m/s is pitched up while approaching and down while receding
    {
        AudioEngine e;
        e.setAmbientEnabled(false);
        e.setReverbEnabled(false);
        listenAt(e, {0, 1.0f, 0});
        std::vector<float> pitches;
        const std::vector<float> out = flyBy(e, Loop::Rover, 1.0f, {-50.0f, 1.0f, -4.0f}, {25.0f, 0, 0}, 4.0f, &pitches);
        saveSample("v7_rover_doppler", out);
        const std::vector<float> m = mono(out);
        // whine fundamental at param 1 m/s: 180 + 420 = 600 Hz; search the peak 480..760 Hz
        auto peakHz = [&](double t0) {
            double bf = 0, bp = -1;
            for (double f = 480; f <= 760; f += 2) {
                const double p = goertzel(m, static_cast<size_t>(t0 * 48000), 9600, f);
                if (p > bp) { bp = p; bf = f; }
            }
            return bf;
        };
        const double fA = peakHz(1.0), fR = peakHz(2.9);   // passes x = 0 at t = 2 s
        const float pA = pitches[60], pR = pitches[180];
        const double expA = 600.0 * 343.0 / (343.0 - 25.0 * 0.99), expR = 600.0 * 343.0 / (343.0 + 25.0 * 0.99);
        record("T303", "Doppler (physical, rendered): rover whine 600 Hz passing at 25 m/s -> ~647 Hz approaching, ~559 Hz "
                       "receding",
               fmt("approach > 630 Hz, recede < 575 Hz (theory %.0f / %.0f)", expA, expR),
               fmt("measured %.0f / %.0f Hz, engine pitch %.3f / %.3f", fA, fR, pA, pR),
               fA > 630.0 && fR < 575.0 && pA > 1.05f && pR < 0.95f);
    }
    // T304 limiter and loudness: a heavy battle never exceeds the ceiling and stays in a consistent loudness window;
    // the quiet night bed sits well below it
    {
        AudioEngine e;
        listenAt(e, {0, 1.7f, 10.0f});
        e.resetLimiterStats();
        std::vector<float> all, blk;
        GameEvent ev;
        for (int f = 0; f < 6 * 60; ++f) {
            if (f % 40 == 0) {
                ev.type = GameEventType::Explosion;
                ev.value = 150 + (f % 3) * 100;
                ev.position = {static_cast<float>((f * 7) % 30) - 15.0f, 0.5f, static_cast<float>((f * 13) % 30) - 10.0f};
                e.handleEvent(ev);
            }
            if (f % 55 == 10) { ev.type = GameEventType::CannonFire; ev.position = {-22, 1.5f, -16}; e.handleEvent(ev); }
            if (f % 90 == 20) { ev.type = GameEventType::GatlingBurst; ev.position = {21, 1.5f, -19}; e.handleEvent(ev); }
            e.renderOffline(1.0f / 60.0f, blk);
            append(all, blk);
        }
        saveSample("v7_battle_ducking_echoes", all);
        const float pk = peakOf(all);
        const double battle = rmsDb(all);
        AudioEngine q;
        listenAt(q, {0, 1.7f, 10.0f});
        std::vector<float> night;
        q.renderOffline(6.0f, night);
        const double bed = rmsDb(night);
        record("T304", "master: look-ahead limiter + soft clip keep a 6 s battle below the ceiling; battle loudness "
                       "-24..-8 dBFS RMS, night bed -50..-26 dBFS",
               "peak <= 0.88, limiter engaged", fmt("peak %.3f, limiter min gain %.2f, battle %.1f dBFS, night bed %.1f dBFS",
                                                     pk, e.limiterMinGain(), battle, bed),
               pk <= AudioEngine::kCeiling + 1e-6f && battle > -24.0 && battle < -8.0 && bed > -50.0 && bed < -26.0 &&
                   allFinite(all));
    }
    // T305 ambience follows the time of day and the bunker
    {
        auto bed = [](float hour, float daylight, float ug, float seconds) {
            AmbientGen a(5);
            a.setTimeOfDay(hour, daylight);
            a.setUnderground(ug);
            std::vector<float> out(static_cast<size_t>(2 * 48000 * seconds), 0.0f);
            for (size_t i = 0; i < out.size() / 2; i += 512) {
                const int n = static_cast<int>(std::min<size_t>(512, out.size() / 2 - i));
                a.render(out.data() + 2 * i, n, 1.0f);
            }
            return out;
        };
        const std::vector<float> night = mono(bed(23.0f, 0.0f, 0.0f, 20.0f)), day = mono(bed(13.0f, 1.0f, 0.0f, 20.0f));
        const std::vector<float> under = mono(bed(23.0f, 0.0f, 1.0f, 6.0f));
        const std::vector<double> pn = welch(night, 48000), pd = welch(day, 48000), pu = welch(under, 48000);
        const double nRatio = psdBandDb(pn, 4250, 5250) - psdBandDb(pn, 1800, 3900);
        const double dRatio = psdBandDb(pd, 4250, 5250) - psdBandDb(pd, 1800, 3900);
        const double birdGain = psdBandDb(pd, 1800, 3900) - psdBandDb(pn, 1800, 3900);
        const double muffle = psdBandDb(pn, 2000, 6000) - psdBandDb(pu, 2000, 6000);
        {
            std::vector<float> all;
            append(all, bed(6.2f, 0.6f, 0.0f, 12.0f));
            append(all, bed(13.0f, 1.0f, 0.0f, 12.0f));
            append(all, bed(23.0f, 0.0f, 0.0f, 12.0f));
            append(all, bed(23.0f, 0.0f, 1.0f, 6.0f));
            saveSample("v7_ambience_dawn_noon_night_bunker", all);
        }
        record("T305", "ambience: night = crickets (4.3-5.2 kHz) over the bird band, day = birds (1.7-3.9 kHz) up; underground "
                       "the bed is muffled (2-6 kHz)",
               "night ratio > day ratio + 6 dB, birds +8 dB by day, muffled > 20 dB",
               fmt("cricket/bird ratio night %.1f dB, day %.1f dB; bird band day vs night %+.1f dB; underground -%.1f dB", nRatio,
                   dRatio, birdGain, muffle),
               nRatio > dRatio + 6.0 && birdGain > 8.0 && muffle > 20.0);
    }
    // T306 ducking: a close blast ducks the bed and the other sources, then releases; a distant one barely ducks
    {
        AudioEngine e;
        listenAt(e, {0, 1.7f, 0});
        GameEvent ev;
        ev.type = GameEventType::Explosion;
        ev.value = 200;
        ev.position = {6.0f, 0.5f, -6.0f};
        std::vector<float> blk;
        e.renderOffline(0.5f, blk);
        e.handleEvent(ev);
        float minDuck = 1.0f;
        for (int i = 0; i < 12; ++i) { e.renderOffline(1.0f / 60.0f, blk); minDuck = std::min(minDuck, e.duckGain()); }
        e.renderOffline(4.0f, blk);
        const float after = e.duckGain();
        AudioEngine far;
        listenAt(far, {0, 1.7f, 0});
        ev.position = {0.0f, 0.5f, -150.0f};
        far.handleEvent(ev);
        float minFar = 1.0f;
        for (int i = 0; i < 60; ++i) { far.renderOffline(1.0f / 60.0f, blk); minFar = std::min(minFar, far.duckGain()); }
        record("T306", "ducking: blast at 8.5 m ducks the mix (< 0.6) within 0.2 s and releases (> 0.95) after 4 s; "
                       "a blast at 150 m ducks less (> 0.75)",
               "duck < 0.6, release > 0.95, far > 0.75", fmt("near min %.2f, after 4 s %.3f, far min %.2f", minDuck, after, minFar),
               minDuck < 0.6f && after > 0.95f && minFar > 0.75f && minFar > minDuck);
    }
    // T307 bunker: reverb zone from the position, long concrete tail inside, the outside world occluded
    {
        auto tailRatio = [](const Vec3& listener, const Vec3& src, float& bunker, std::vector<float>* keep) {
            AudioEngine e;
            e.setAmbientEnabled(false);
            ListenerState l;
            l.position = listener;
            l.hasVelocity = true;
            for (int i = 0; i < 60; ++i) {   // let the reverb preset glide into place
                e.setListener(l, 1.0f / 60.0f);
                std::vector<float> b;
                e.renderOffline(1.0f / 60.0f, b);
            }
            bunker = e.listenerBunker();
            e.playClip(Clip::LockTone, src, true, 1.0f);   // a dry 0.3 s beep pair
            std::vector<float> out;
            e.renderOffline(1.6f, out);
            if (keep) *keep = out;
            // energy 0.6..1.4 s (after the beeps) vs 0..0.3 s
            return rmsDb(out, 2 * 28800, 2 * 67200) - rmsDb(out, 0, 2 * 14400);
        };
        float bIn = 0, bOut = 0;
        std::vector<float> inside, outside;
        const double tIn = tailRatio({0.0f, -1.8f, -2.0f}, {2.0f, -2.0f, -5.0f}, bIn, &inside);
        const double tOut = tailRatio({0.0f, 1.7f, 26.0f}, {2.0f, 1.5f, 23.0f}, bOut, &outside);
        // occlusion: listener inside, a blast outside above vs the same blast heard outside at the same distance
        auto hiFrac = [](const Vec3& listener) {
            AudioEngine e;
            e.setAmbientEnabled(false);
            e.setReverbEnabled(false);
            listenAt(e, listener);
            e.playClip(Clip::Cannon, {0.0f, 1.0f, 14.0f}, true, 1.0f);
            std::vector<float> out;
            e.renderOffline(0.8f, out);
            const std::vector<float> m = mono(out);
            return std::make_pair(10 * std::log10(bandEnergy(m, 0, 24000, 1500, 6000, 100) / std::max(1e-12, bandEnergy(m, 0, 24000, 50, 400, 25))),
                                  rmsDb(out));
        };
        const auto occ = hiFrac({0.0f, -1.8f, -2.0f}), open = hiFrac({0.0f, 1.7f, -2.0f});
        if (sampleDir()) {
            std::vector<float> all = outside;
            append(all, inside);
            saveSample("v7_bunker_vs_open_reverb", all);
        }
        record("T307", "bunker zone: listenerBunker() 1 inside / 0 outside; inside the beep leaves a long concrete tail; a "
                       "cannon outside is muffled through the roof (HF/LF and level down)",
               "tail inside > outside + 12 dB, occluded HF/LF < open - 10 dB, level < open - 6 dB",
               fmt("bunker %.2f / %.2f, tail %.1f / %.1f dB, HF/LF occluded %.1f vs open %.1f dB, level %.1f vs %.1f dBFS",
                   bIn, bOut, tIn, tOut, occ.first, open.first, occ.second, open.second),
               bIn > 0.99f && bOut < 0.01f && tIn > tOut + 12.0 && occ.first < open.first - 10.0 && occ.second < open.second - 6.0);
    }
    // T308 echoes and propagation: image-source taps off the perimeter wall / treeline; a far blast arrives late
    {
        EchoTap taps[2];
        const Vec3 L{0.0f, 1.7f, 0.0f}, S{6.0f, 1.5f, 0.0f};
        const int n = echoTaps(L, S, taps);
        bool geom = n == 2;
        for (int k = 0; k < n; ++k) {
            const float expect = (length(taps[k].image - L) - length(S - L)) / kSpeedOfSound;
            geom = geom && std::fabs(taps[k].delaySec - expect) < 1e-4f && taps[k].gain > 0.0f && taps[k].gain < 0.6f;
        }
        EchoTap none[2];
        const bool noneInside = echoTaps({0.0f, -1.8f, -2.0f}, S, none) == 0;
        // rendered: the echo contribution (with - without echoes, everything else identical) starts exactly at the wall
        // delay and is comparable to the cannon's own tail at the treeline delay
        auto cannon = [&](bool echoes) {
            AudioEngine e;
            e.setAmbientEnabled(false);
            e.setReverbEnabled(false);
            e.setEchoesEnabled(echoes);
            listenAt(e, L);
            e.playClip(Clip::Cannon, S, true, 1.0f);
            std::vector<float> out;
            e.renderOffline(2.5f, out);
            return out;
        };
        const std::vector<float> wet = cannon(true), dry = cannon(false);
        saveSample("v7_cannon_echoes", wet);
        std::vector<float> echo(wet.size());
        for (size_t i = 0; i < wet.size(); ++i) echo[i] = wet[i] - dry[i];
        const size_t t0 = static_cast<size_t>(taps[0].delaySec * 48000.0f), t1 = static_cast<size_t>(taps[1].delaySec * 48000.0f);
        const float early = peakOf(std::vector<float>(echo.begin(), echo.begin() + 2 * static_cast<long>(t0)));
        const double echoDb = rmsDb(echo, 2 * t1, 2 * (t1 + 7200)), tailDb = rmsDb(dry, 2 * t1, 2 * (t1 + 7200));
        // propagation: blast at 120 m arrives (120 - 20) / 343 = 0.29 s later
        AudioEngine p;
        p.setAmbientEnabled(false);
        listenAt(p, L);
        GameEvent ev;
        ev.type = GameEventType::Explosion;
        ev.value = 300;
        ev.position = {0.0f, 1.0f, -120.0f};
        p.handleEvent(ev);
        std::vector<float> po;
        p.renderOffline(0.6f, po);
        size_t onset = po.size();
        for (size_t i = 0; i < po.size(); ++i) if (std::fabs(po[i]) > 1e-3f) { onset = i / 2; break; }
        const float onsetS = static_cast<float>(onset) / 48000.0f;
        record("T308", "echo taps (image source off the HESCO wall at 34 m and the treeline at 72 m) match the geometry; "
                       "none inside the bunker; the treeline echo is audible; a blast at 120 m arrives ~0.29 s late",
               "delay error < 0.1 ms, no echo before the wall delay, treeline echo > tail - 6 dB, onset 0.27..0.32 s",
               fmt("taps %d (%.3f s g %.2f, %.3f s g %.2f), inside %d, pre-wall echo %.1e, treeline echo %.1f dB vs tail %.1f dB, "
                   "onset %.3f s", n, taps[0].delaySec, taps[0].gain, taps[1].delaySec, taps[1].gain, noneInside, early, echoDb,
                   tailDb, onsetS),
               geom && noneInside && early < 1e-6f && echoDb > tailDb - 6.0 && onsetS > 0.27f && onsetS < 0.32f);
    }
    // T309 whole-scene determinism with a seed, UI cue API, footsteps and radio from the scene
    {
        auto run = [](uint32_t seed, int* steps, int* radios, std::vector<float>* keep, float seconds) {
            // just before dusk (+4 deg): the Dusk event, shift change and radio fall inside the run
            const float dusk = DayCycle::hourAtElevation(kDayAboveDeg, false, DayCycleConfig{});
            SceneOptions o = SceneOptions::v6(11, dusk - 0.02f, true);
            auto scene = std::make_unique<Scene>(o);
            AudioEngine e(seed);
            Detection det;
            std::vector<float> all, blk;
            for (int f = 0; f < static_cast<int>(seconds * 30.0f); ++f) {
                scene->update(1.0f / 60.0f, det);
                e.pollEvents(*scene);
                scene->update(1.0f / 60.0f, det);
                ListenerState l;
                l.position = {2.0f, 1.7f, 12.0f};
                e.update(*scene, l, 1.0f / 30.0f);
                if (f == 30) e.uiCue(UiCue::PickerOpen);
                if (f == 45) e.uiCue(UiCue::PovJump);
                if (f == 70) e.uiCue(UiCue::Possess);
                if (f == 90) e.uiCue(UiCue::HitMarker);
                e.renderOffline(1.0f / 30.0f, blk);
                append(all, blk);
            }
            if (steps) *steps = e.footstepsPlayed();
            if (radios) *radios = e.radioPlayed();
            if (keep) *keep = all;
            return all;
        };
        int steps = 0, radios = 0;
        std::vector<float> keep;
        const std::vector<float> a = run(7, &steps, &radios, &keep, 6.0f);
        const std::vector<float> b = run(7, nullptr, nullptr, nullptr, 6.0f);
        const std::vector<float> c = run(8, nullptr, nullptr, nullptr, 6.0f);
        size_t diff = 0;
        for (size_t i = 0; i < a.size() && i < c.size(); ++i) diff += a[i] != c[i] ? 1 : 0;
        saveSample("v7_scene_dusk_ui", keep);
        // UI bus: every cue is audible and never ducked / spatialised (same level in both ears)
        bool uiOk = true;
        for (int k = 0; k < kUiCueCount; ++k) {
            AudioEngine u;
            u.setAmbientEnabled(false);
            u.uiCue(static_cast<UiCue>(k));
            std::vector<float> out;
            u.renderOffline(0.3f, out);
            double dl = 0;
            for (size_t i = 0; i + 1 < out.size(); i += 2) dl = std::max(dl, static_cast<double>(std::fabs(out[i] - out[i + 1])));
            uiOk = uiOk && peakOf(out) > 0.05f && dl < 1e-6;
        }
        if (sampleDir()) {
            std::vector<float> s;
            for (int k = 0; k < kUiCueCount; ++k) { append(s, stereoOf(synthUiCue(static_cast<UiCue>(k)))); append(s, std::vector<float>(2 * 12000, 0.0f)); }
            for (uint32_t k = 1; k <= 3; ++k) { append(s, stereoOf(synthRadioChatter(k), 0.8f)); append(s, std::vector<float>(2 * 12000, 0.0f)); }
            saveSample("v7_ui_cues_radio", s);
            std::vector<float> f;
            for (int sf = 0; sf < kSurfaceCount; ++sf) {
                for (uint32_t k = 0; k < 6; ++k) {
                    std::vector<float> st = stereoOf(synthFootstep(static_cast<ASurface>(sf), k));
                    st.resize(2 * 26000, 0.0f);
                    append(f, st);
                }
                append(f, std::vector<float>(2 * 24000, 0.0f));
            }
            saveSample("v7_footsteps_dirt_gravel_concrete_metal_grass", f);
            for (const auto& item : {std::make_pair(Loop::Drone, 12.0f), std::make_pair(Loop::Tank, 4.0f), std::make_pair(Loop::Helicopter, 1.0f)}) {
                AudioEngine v;
                v.setAmbientEnabled(false);
                listenAt(v, {0, 1.7f, 0});
                const Vec3 vel = item.first == Loop::Tank ? Vec3{6.0f, 0, 0} : Vec3{18.0f, 0, 0};
                const Vec3 start = item.first == Loop::Tank ? Vec3{-30.0f, 0.5f, -6.0f} : Vec3{-70.0f, 12.0f, -10.0f};
                const char* name = item.first == Loop::Drone ? "v7_uav_flyby" : item.first == Loop::Tank ? "v7_tank_driveby" : "v7_helicopter_flyby";
                saveSample(name, flyBy(v, item.first, item.second, start, vel, item.first == Loop::Tank ? 10.0f : 8.0f));
            }
        }
        record("T309", "whole scene (dusk, seed 11): two runs with engine seed 7 are bit-identical, seed 8 differs; soldiers' "
                       "footsteps and phase radio are heard; all 9 UI cues are audible, centred and undocked",
               "identical, steps > 0, radio >= 1, UI ok",
               fmt("identical %d (%zu samples), seed 8 differs in %zu, footsteps %d, radio %d, ui %d", a == b, a.size(), diff,
                   steps, radios, uiOk),
               a == b && diff > 0 && steps > 0 && radios >= 1 && uiOk && allFinite(a));
    }
    // T309b small arms and the possessed vehicle: rifle report (spatial + echoes), tracer impacts as surface ticks (no
    // ducking), cockpit / hull filter on the own engine, rover whine following the throttle
    {
        auto shot = [](bool echoes) {
            AudioEngine e(3);
            e.setAmbientEnabled(false);
            e.setReverbEnabled(false);
            e.setEchoesEnabled(echoes);
            listenAt(e, {0.0f, 1.7f, 0.0f});
            e.gunshot({0.11f, 1.58f, -1.3f});   // muzzle in front of a first-person soldier
            std::vector<float> out;
            e.renderOffline(1.0f, out);
            return out;
        };
        const std::vector<float> r1 = shot(true), r2 = shot(true), dry = shot(false);
        saveSample("v7_rifle_first_person", r1);
        std::vector<float> echo(r1.size());
        for (size_t i = 0; i < r1.size(); ++i) echo[i] = r1[i] - dry[i];
        const bool rifleOk = r1 == r2 && allFinite(r1) && peakOf(r1) > 0.3f && peakOf(echo) > 0.005f;
        // impact ticks: power 0.15 hits are ticks (short, no duck); power 1 is still a blast that ducks
        auto hit = [](int value, const Vec3& at, float& duck) {
            AudioEngine e;
            e.setAmbientEnabled(false);
            listenAt(e, {0.0f, 1.7f, 0.0f});
            GameEvent ev;
            ev.type = GameEventType::Explosion;
            ev.value = value;
            ev.position = at;
            e.handleEvent(ev);
            std::vector<float> out, all;
            duck = 1.0f;
            for (int i = 0; i < 30; ++i) { e.renderOffline(1.0f / 60.0f, out); append(all, out); duck = std::min(duck, e.duckGain()); }
            e.renderOffline(1.0f, out);
            append(all, out);
            return all;
        };
        float duckTick = 0, duckBlast = 0;
        const std::vector<float> tickDirt = hit(15, {6.0f, terrainHeight(6.0f, -6.0f), -6.0f}, duckTick);
        hit(100, {6.0f, terrainHeight(6.0f, -6.0f), -6.0f}, duckBlast);
        const double tickLen = [&] {   // seconds above -40 dBFS
            size_t last = 0;
            for (size_t i = 0; i < tickDirt.size(); ++i) if (std::fabs(tickDirt[i]) > 0.01f) last = i;
            return static_cast<double>(last / 2) / 48000.0;
        }();
        const double cMetal = centroid(synthImpact(ASurface::Metal, 1)), cDirt = centroid(synthImpact(ASurface::Dirt, 1));
        // own tank engine heard from inside (first person) vs the same engine from outside at the same spot
        auto tank = [](bool interior) {
            AudioEngine e;
            e.setAmbientEnabled(false);
            e.setReverbEnabled(false);
            listenAt(e, {0.0f, 2.2f, 0.0f});
            std::vector<float> out, all;
            for (int f = 0; f < 90; ++f) {
                e.setSource(Loop::Tank, 4, {0.6f, 1.2f, 0.4f}, nullptr, 2.0f, 1.0f / 60.0f, 0.0f, interior);
                e.endFrame(1.0f / 60.0f);
                e.renderOffline(1.0f / 60.0f, out);
                if (f >= 30) append(all, out);
            }
            return welch(mono(all), 0);
        };
        const std::vector<double> pIn = tank(true), pOut = tank(false);
        const double tiltIn = psdBandDb(pIn, 1500, 6000) - psdBandDb(pIn, 30, 200);
        const double tiltOut = psdBandDb(pOut, 1500, 6000) - psdBandDb(pOut, 30, 200);
        const double rumble = psdBandDb(pIn, 30, 150) - psdBandDb(pOut, 30, 150);
        // possessed rover standing still: full throttle spins the drive whine up (peak frequency and level)
        auto rover = [](float load, double& level) {
            AudioEngine e;
            e.setAmbientEnabled(false);
            e.setReverbEnabled(false);
            listenAt(e, {0.0f, 1.0f, 0.0f});
            std::vector<float> out, all;
            const Vec3 still{0.0f, 0.0f, 0.0f};
            for (int f = 0; f < 90; ++f) {
                e.setSource(Loop::Rover, 0, {0.0f, 0.4f, -1.0f}, &still, 0.0f, 1.0f / 60.0f, load, true);
                e.endFrame(1.0f / 60.0f);
                e.renderOffline(1.0f / 60.0f, out);
                if (f >= 45) append(all, out);
            }
            const std::vector<float> m = mono(all);
            level = rmsDb(m);
            double bf = 0, bp = -1;
            for (double f = 150; f <= 1400; f += 5) {
                const double p = goertzel(m, 0, 19200, f);
                if (p > bp) { bp = p; bf = f; }
            }
            return bf;
        };
        double lIdle = 0, lFull = 0;
        const double fIdle = rover(0.0f, lIdle), fFull = rover(1.0f, lFull);
        record("T309b", "small arms + possessed vehicle: rifle report deterministic with structure echoes; tracer hits (power "
                        "<= 0.2) are short surface ticks that do not duck (metal brighter than dirt); own tank engine through "
                        "the hull (darker, rumble boosted); rover whine follows the throttle",
               "rifle ok; tick < 0.7 s, no duck, blast ducks; tilt in < out - 10 dB, rumble > +2 dB; whine up > 300 Hz, +3 dB",
               fmt("rifle peak %.2f echo %.3f det %d; tick %.2f s duck %.2f (blast %.2f), centroid metal %.0f dirt %.0f Hz; "
                   "HF/LF tilt in %.1f out %.1f dB, rumble %+.1f dB; rover %.0f -> %.0f Hz, %.1f -> %.1f dBFS",
                   peakOf(r1), peakOf(echo), r1 == r2, tickLen, duckTick, duckBlast, cMetal, cDirt, tiltIn, tiltOut, rumble,
                   fIdle, fFull, lIdle, lFull),
               rifleOk && tickLen < 0.7 && duckTick > 0.999f && duckBlast < 0.9f && cMetal > cDirt && tiltIn < tiltOut - 10.0 &&
                   rumble > 2.0 && fFull > fIdle + 300.0 && lFull > lIdle + 3.0);
    }
}
