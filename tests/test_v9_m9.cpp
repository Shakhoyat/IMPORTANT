// v9 M9 (session recording and deterministic replay): T400-T405. CPU only.
// The App records its interactive frames into a session log (scene/SessionLog.h) and --replay re-runs them. These
// tests cover the log itself (exact text round trip, frame splitting, pointer reads, errors) and the property the
// replay relies on: the world is a function of the seed, the frame times and the inputs, so a session written to text,
// parsed back and re-run through the same fixed-tick loop reproduces every world checkpoint bit for bit.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "TestKit.h"
#include "scene/Scene.h"
#include "scene/SessionLog.h"
#include "scene/TickInterp.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {

struct Lcg {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.0f;
    }
};

// Key codes of the test's own little key map (the values only have to be stable).
enum Key { kW = 87, kS = 83, kA = 65, kD = 68, kSpace = 32, kCtrl = 341, kShift = 340 };
const int kKeys[] = {kW, kS, kA, kD, kSpace, kCtrl, kShift};

ControlInput inputFrom(const std::vector<int>& held, bool& spaceWas, bool& ctrlWas) {
    auto down = [&](int k) { return std::find(held.begin(), held.end(), k) != held.end(); };
    ControlInput in;
    in.forward = (down(kW) ? 1.0f : 0.0f) - (down(kS) ? 1.0f : 0.0f);
    in.turn = (down(kD) ? 1.0f : 0.0f) - (down(kA) ? 1.0f : 0.0f);
    in.boost = down(kShift);
    in.fire = down(kCtrl);
    in.jump = down(kSpace) && !spaceWas;
    in.firePressed = down(kCtrl) && !ctrlWas;
    spaceWas = down(kSpace);
    ctrlWas = down(kCtrl);
    return in;
}

uint64_t sceneHash(const Scene& s) {
    uint64_t h = fnv1a(nullptr, 0);
    for (const SceneNode* n : s.leaves()) {
        h = fnv1a(n->world.m, sizeof(n->world.m), h);
        const unsigned char vis = n->visible ? 1 : 0;
        h = fnv1a(&vis, 1, h);
    }
    const float t = s.time();
    return fnv1a(&t, sizeof(t), h);
}

void applyKey(std::vector<int>& held, const SessionEntry& e) {
    if (e.kind != 'K') return;
    if (e.b == 1) {
        if (std::find(held.begin(), held.end(), e.a) == held.end()) held.push_back(e.a);
    } else if (e.b == 0) {
        held.erase(std::remove(held.begin(), held.end(), e.a), held.end());
    }
}

// The App's frame structure with a scene: F (frame time), B, the frame's ticks with the held keys, then the inputs of
// the frame's poll. Records when 'replay' is null; else runs the recorded frames and compares the checkpoints.
struct SessionRun {
    std::vector<SessionEntry> log;
    long long ticks = 0, checks = 0, mismatches = 0, firstBad = -1, dropped = 0;
};

SessionRun runSession(uint32_t seed, int possessed, double seconds, const std::vector<SessionFrame>* replay, int flipFrame = -1) {
    Scene scene(SceneOptions::v6(seed, 23.0f, false));
    FixedStepClock clock;
    SessionRecorder rec;
    std::vector<int> held;
    bool spaceWas = false, ctrlWas = false;
    Lcg rng{seed * 7919u + 13u};
    Detection det;
    SessionRun out;
    for (size_t f = 0;; ++f) {
        double dt = 0.0;
        const SessionFrame* fr = nullptr;
        if (replay) {
            if (f >= replay->size()) break;
            fr = &(*replay)[f];
            dt = fr->dt;
        } else {
            if (clock.ticks() >= static_cast<long long>(seconds * 60.0)) break;   // 60 s of simulated time
            // Frame times of a live game: 6-40 ms, an odd value (no exact tick multiples), a 130 ms hitch now and then
            dt = 0.006 + 0.034 * rng.next() + 1e-7 * rng.next();
            if (rng.next() < 0.004f) dt = 0.13;
            rec.frame(dt, 1600, 900, 1600, 900);
            rec.topDone();
        }
        const int n = clock.advance(dt);
        for (int t = 0; t < n; ++t) {
            ControlInput in = inputFrom(held, spaceWas, ctrlWas);
            scene.update(FixedStepClock::kTick, det, &in, possessed);
            const long long tick = clock.ticks() - n + t + 1;
            if (tick % 60 == 0) {
                const uint64_t h = sceneHash(scene);
                if (!replay) {
                    rec.check(tick, h);
                } else {
                    for (const SessionEntry& c : fr->checks)
                        if (c.tick == tick) {
                            ++out.checks;
                            if (c.hash != h) {
                                ++out.mismatches;
                                if (out.firstBad < 0) out.firstBad = tick;
                            }
                        }
                }
            }
        }
        // the frame's poll: key changes (recorded at random, or the recorded ones)
        if (replay) {
            for (SessionEntry e : fr->poll) {
                if (static_cast<int>(f) == flipFrame && e.kind == 'K') e.b = e.b == 1 ? 0 : 1;   // a tampered input
                applyKey(held, e);
            }
        } else if (rng.next() < 0.08f) {
            const int k = kKeys[static_cast<int>(rng.next() * 7.0f) % 7];
            const bool isDown = std::find(held.begin(), held.end(), k) != held.end();
            SessionEntry e{'K'};
            e.a = k;
            e.b = isDown ? 0 : 1;
            rec.key(k, e.b, 0);
            applyKey(held, e);
        }
    }
    out.ticks = clock.ticks();
    out.dropped = clock.dropped();
    out.log = rec.entries();
    return out;
}

}  // namespace

TS_TEST_GROUP(V9M9) {
    // ---- T400: the session text round trip is exact (hex floats) and keeps the header -----------------------------
    {
        SessionHeader h;
        h.seed = 4000000001u;
        h.worldArgs = {"--time 21:30", "--day-ops"};
        h.settings = "sensitivity=1.2\ninvert_y=0\nfov=65\n";
        h.windowW = 1600;
        h.windowH = 900;
        h.fbW = 3200;
        h.fbH = 1800;
        h.quality = 3;
        h.clipIn = 120;
        h.clipOut = 3720;
        SessionRecorder r;
        const double odd[] = {0.1, 1.0 / 3.0, 0.016666666666666666, 5e-324, -2.5e-17, 0.033333333333333333 + 1e-16};
        for (double v : odd) {
            r.frame(v, 1600, 900, 3200, 1800);
            r.cursor(v * 1000.0, -v);
            r.topDone();
            r.key(87, 1, 2);
            r.button(0, 1);
            r.scroll(-v);
            r.pointerRead(v, 2.0 * v);
            r.check(60, 0xfedcba9876543210ull);
        }
        const std::string text = serializeSession(h, r.entries());
        SessionHeader h2;
        std::vector<SessionEntry> e2;
        std::string err;
        const bool ok = parseSession(text, h2, e2, err);
        bool same = ok && e2.size() == r.entries().size();
        for (size_t i = 0; same && i < e2.size(); ++i) {
            const SessionEntry &a = r.entries()[i], &b = e2[i];
            same = a.kind == b.kind && a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.tick == b.tick &&
                   a.hash == b.hash && std::memcmp(&a.x, &b.x, sizeof(double)) == 0 && std::memcmp(&a.y, &b.y, sizeof(double)) == 0;
        }
        const bool header = h2.seed == h.seed && h2.worldArgs == h.worldArgs && h2.settings == h.settings && h2.windowW == 1600 &&
                            h2.windowH == 900 && h2.fbW == 3200 && h2.fbH == 1800 && h2.quality == 3 && h2.clipIn == 120 &&
                            h2.clipOut == 3720;
        record("T400", "Session log text round trip: every double bit-exact (hex floats), header (seed, world args, settings, window, clip) kept",
               "parse(serialize(x)) == x for 48 entries incl. 0.1, 1/3, a denormal, a 64-bit hash",
               fmt("parse %s, %zu / %zu entries, bit-exact %s, header %s", ok ? "ok" : err.c_str(), e2.size(), r.entries().size(),
                   same ? "yes" : "NO", header ? "same" : "DIFFERENT"),
               ok && same && header);
    }

    // ---- T401: frames: top-of-frame inputs before B, poll inputs after it, reads and checkpoints per frame ----------
    {
        SessionRecorder r;
        r.key(1, 1, 0);   // before the first frame: ignored
        r.frame(0.02, 800, 600, 800, 600);
        r.key(2, 1, 0);   // script, top of the frame
        r.topDone();
        r.pointerRead(10, 20);
        r.check(60, 7);
        r.key(3, 0, 0);   // poll
        r.cursor(5, 6);
        r.frame(0.03, 800, 600, 800, 600);
        r.topDone();
        r.scroll(1);
        const std::vector<SessionFrame> fr = sessionFrames(r.entries());
        const bool ok = fr.size() == 2 && fr[0].top.size() == 1 && fr[0].top[0].a == 2 && fr[0].poll.size() == 2 &&
                        fr[0].poll[0].a == 3 && fr[0].poll[1].kind == 'C' && fr[0].reads.count(0) == 1 && fr[0].checks.size() == 1 &&
                        fr[1].top.empty() && fr[1].poll.size() == 1 && fr[1].poll[0].kind == 'W' && fr[1].dt == 0.03;
        record("T401", "Session frames: inputs split at B into top-of-frame (script) and poll (window) inputs, reads / checkpoints per frame",
               "2 frames; frame 1: top {K2}, poll {K3, C}, read 0, 1 checkpoint; frame 2: poll {W}; the key before the first frame dropped",
               fmt("%zu frames; f1 top %zu poll %zu reads %zu checks %zu; f2 poll %zu", fr.size(), fr.empty() ? 0 : fr[0].top.size(),
                   fr.empty() ? 0 : fr[0].poll.size(), fr.empty() ? 0 : fr[0].reads.size(), fr.empty() ? 0 : fr[0].checks.size(),
                   fr.size() < 2 ? 0 : fr[1].poll.size()),
               ok);
    }

    // ---- T402: pointer reads: only changes are logged, the replay returns every read's value ------------------------
    {
        Lcg rng{11};
        SessionRecorder r;
        std::vector<std::vector<std::pair<double, double>>> live;
        double x = 100, y = 100;
        for (int f = 0; f < 200; ++f) {
            r.frame(0.016, 1, 1, 1, 1);
            r.topDone();
            live.emplace_back();
            const int reads = 1 + static_cast<int>(rng.next() * 4.0f);
            for (int i = 0; i < reads; ++i) {
                if (rng.next() < 0.3f) x += rng.next() * 7.0 - 3.0, y += 0.25;
                r.pointerRead(x, y);
                live.back().push_back({x, y});
            }
        }
        size_t logged = 0, total = 0;
        for (const SessionEntry& e : r.entries()) logged += e.kind == 'P';
        const std::vector<SessionFrame> fr = sessionFrames(r.entries());
        SessionPointer p;
        bool same = fr.size() == live.size();
        for (size_t f = 0; same && f < fr.size(); ++f) {
            p.beginFrame(&fr[f]);
            for (const auto& v : live[f]) {
                double rx = 0, ry = 0;
                p.read(rx, ry);
                same = same && rx == v.first && ry == v.second;
                ++total;
            }
        }
        record("T402", "Pointer reads: logged only when the value changes; the replay returns the live value for every read of every frame",
               "all reads identical; fewer entries than reads", fmt("%zu reads, %zu logged, replay identical %s", total, logged, same ? "yes" : "NO"),
               same && logged < total && logged > 0);
    }

    // ---- T403: errors name the line; Windows line ends and comments are accepted ------------------------------------
    {
        SessionHeader h;
        std::vector<SessionEntry> e;
        std::string e1, e2, e3, e4;
        const bool bad1 = !parseSession("F 0x1p-6 1 1 1 1\n", h, e, e1);
        const bool bad2 = !parseSession("#! ThermalSense session 1\nF 0x1p-6 1 1 1 1\nK 87 one 0\n", h, e, e2);
        const bool bad3 = !parseSession("#! ThermalSense session 1\nQ 1\n", h, e, e3);
        const bool good = parseSession("#! ThermalSense session 1\r\n# a note\r\n#! seed 9\r\nF 0x1p-6 1 1 1 1\r\nB\r\n", h, e, e4) &&
                          e.size() == 2 && h.seed == 9;
        const bool lines = e2.find("line 3") != std::string::npos && e3.find("line 2") != std::string::npos &&
                           e1.find("not a ThermalSense session") != std::string::npos;
        record("T403", "Session parse errors name the line; CRLF line ends and '#' comments are accepted",
               "no magic -> rejected; 'K 87 one 0' -> line 3; 'Q 1' -> line 2; CRLF file parses",
               fmt("'%s' | '%s' | '%s' | crlf %s", e1.c_str(), e2.c_str(), e3.c_str(), good ? "ok" : e4.c_str()),
               bad1 && bad2 && bad3 && good && lines);
    }

    // ---- T404 / T405: a 60 s session recorded, written as text, parsed and re-run: every checkpoint bit-identical -----
    {
        const uint32_t seed = 7;
        const int possessed = 0;   // the first controllable of the night scene, driven by the recorded keys
        const SessionRun live = runSession(seed, possessed, 60.0, nullptr);
        SessionHeader h;
        h.seed = seed;
        const std::string text = serializeSession(h, live.log);
        SessionHeader h2;
        std::vector<SessionEntry> parsed;
        std::string err;
        const bool ok = parseSession(text, h2, parsed, err);
        const std::vector<SessionFrame> frames = sessionFrames(parsed);
        size_t keys = 0, liveChecks = 0;
        for (const SessionEntry& e : live.log) keys += e.kind == 'K', liveChecks += e.kind == 'H';
        const SessionRun rep = ok ? runSession(h2.seed, possessed, 0.0, &frames) : SessionRun{};
        record("T404", "Replay: a 60 s session (variable frame times with hitches, random key input on a possessed unit, night scene with "
               "rovers / UAVs) written to text, parsed and re-run reproduces every per-second world hash bit for bit",
               ">= 3600 ticks, every checkpoint identical, same tick count",
               fmt("%zu frames, %zu key events, %lld ticks (%lld dropped by hitches); replay %lld ticks, %lld / %zu checkpoints, %lld mismatches",
                   frames.size(), keys, live.ticks, live.dropped, rep.ticks, rep.checks - rep.mismatches, liveChecks, rep.mismatches),
               ok && live.ticks >= 3600 && rep.ticks == live.ticks && rep.checks == static_cast<long long>(liveChecks) &&
                   rep.mismatches == 0 && keys > 20);

        // The checkpoints are sensitive: one flipped key event in the middle makes the replay diverge after it.
        int flip = -1;
        for (size_t f = frames.size() / 2; f < frames.size() && flip < 0; ++f)
            for (const SessionEntry& e : frames[f].poll)
                if (e.kind == 'K') flip = static_cast<int>(f);
        const SessionRun bad = ok && flip >= 0 ? runSession(h2.seed, possessed, 0.0, &frames, flip) : SessionRun{};
        long long flipTick = 0;
        {
            FixedStepClock c;
            for (int f = 0; f <= flip && f < static_cast<int>(frames.size()); ++f) c.advance(frames[f].dt);
            flipTick = c.ticks();
        }
        record("T405", "Replay checkpoints detect a divergence: one tampered key event mid-session makes the world hashes differ after it, never before",
               "first mismatch after the tampered frame's tick", fmt("tampered frame %d (tick %lld): first mismatch at tick %lld, %lld mismatches",
                                                                  flip, flipTick, bad.firstBad, bad.mismatches),
               flip >= 0 && bad.mismatches > 0 && bad.firstBad > flipTick);
    }
}
