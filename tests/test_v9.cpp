// v9 (game feel, game look, performance): T330+. CPU only.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "TestKit.h"
#include "scene/InputScript.h"
#include "scene/TickInterp.h"
#include "vision/VisionWorker.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

TS_TEST_GROUP(V9) {
    // ---- T330: input script: parsing, tap / click expansion, sorting, errors name the line --------------------------
    {
        const std::string text =
            "# comment\n"
            "1.0 key W down\n"
            "+1.5 key W up   # relative time\n"
            "0.5 key space tap\n"
            "3 button left click\n"
            "4 look 100 -20 0.5\n"
            "4.2 pointer 0.25 0.75\n"
            "5 shot first_frame\n"
            "5 log after walk\n"
            "6 quit\n";
        std::vector<ScriptEvent> ev;
        std::string err;
        const bool ok = parseInputScript(text, ev, err);
        bool sorted = true;
        for (size_t i = 1; i < ev.size(); ++i) sorted = sorted && ev[i - 1].time <= ev[i].time;
        int keyEvents = 0, wUpAt25 = 0, spaceUp = 0;
        for (const ScriptEvent& e : ev) {
            if (e.kind == ScriptEvent::Kind::Key) ++keyEvents;
            if (e.kind == ScriptEvent::Kind::Key && e.code == 'W' && e.action == 0 && std::fabs(e.time - 2.5) < 1e-9) ++wUpAt25;
            if (e.kind == ScriptEvent::Kind::Key && e.code == 32 && e.action == 0) ++spaceUp;
        }
        std::vector<ScriptEvent> bad;
        std::string badErr, badErr2;
        const bool rejects = !parseInputScript("1 key W down\n2 jump\n", bad, badErr) &&
                             !parseInputScript("1 shot ../escape\n", bad, badErr2);
        record("T330", "Input script: timed key / mouse events parse, tap and click expand, events sort, errors name the line",
               "11 events (tap / click -> press + release), '+1.5' after 1.0 = 2.5, sorted, 'jump' and a path in a shot name rejected",
               fmt("%zu events, %d key events, W up at 2.5: %d, space release: %d, sorted %d, errors '%s' / '%s'", ev.size(), keyEvents,
                   wUpAt25, spaceUp, sorted ? 1 : 0, badErr.c_str(), badErr2.c_str()),
               ok && ev.size() == 11 && keyEvents == 4 && wUpAt25 == 1 && spaceUp == 1 && sorted && rejects &&
                   badErr.find("line 2") != std::string::npos);
    }

    // ---- T331: script player: held keys / buttons follow the events, a timed look is spread over its duration --------
    {
        std::vector<ScriptEvent> ev;
        std::string err;
        parseInputScript("0 key W down\n1 key W up\n0 button right down\n0.5 look 100 40 1.0\n2 quit\n", ev, err);
        ScriptPlayer p;
        p.load(ev);
        double lookX = 0.0, lookY = 0.0;
        bool heldMid = false, rightMid = false, quit = false;
        for (int i = 0; i <= 150; ++i) {   // 60 Hz for 2.5 s
            const double t = i / 60.0;
            for (const ScriptEvent& e : p.due(t)) {
                if (e.kind == ScriptEvent::Kind::Look) {
                    lookX += e.x;
                    lookY += e.y;
                }
                if (e.kind == ScriptEvent::Kind::Quit) quit = true;
            }
            if (std::fabs(t - 0.5) < 1e-6) {
                heldMid = p.keyHeld('W');
                rightMid = p.buttonHeld(1);
            }
        }
        const bool releasedAfter = !p.keyHeld('W');
        record("T331", "Script player: held keys and buttons track the events; a timed look adds up to its full move",
               "W held at 0.5 s and released after 1 s, right button held, look sums to (100, 40) within 1e-6, quit delivered",
               fmt("W mid %d after %d, right %d, look (%.6f, %.6f), quit %d, finished %d", heldMid ? 1 : 0, releasedAfter ? 0 : 1,
                   rightMid ? 1 : 0, lookX, lookY, quit ? 1 : 0, p.finished() ? 1 : 0),
               heldMid && releasedAfter && rightMid && std::fabs(lookX - 100.0) < 1e-6 && std::fabs(lookY - 40.0) < 1e-6 && quit &&
                   p.finished());
    }

    // ---- T332: fixed simulation tick: the tick count follows the wall clock, not the render rate ----------------------
    {
        long long counts[4] = {0, 0, 0, 0};
        float alphaMin = 1.0f, alphaMax = 0.0f;
        const double rates[4] = {30.0, 60.0, 144.0, 47.3};   // 47.3: frames never line up with the tick
        for (int k = 0; k < 4; ++k) {
            FixedStepClock c;
            const int frames = static_cast<int>(10.0 * rates[k] + 0.5);
            for (int f = 0; f < frames; ++f) {
                counts[k] += c.advance(1.0 / rates[k]);
                alphaMin = std::min(alphaMin, c.alpha());
                alphaMax = std::max(alphaMax, c.alpha());
            }
        }
        FixedStepClock hitch;
        const int burst = hitch.advance(1.0);   // a 1 s hitch: at most kMaxTicksPerFrame, the rest dropped
        record("T332", "Fixed 60 Hz tick: 10 s of frames at 30 / 60 / 144 / 47.3 Hz run 600 ticks; alpha in [0, 1); hitches drop the backlog",
               "600 +- 1 ticks at every render rate, 0 <= alpha < 1, a 1 s hitch runs 4 ticks (56 dropped)",
               fmt("ticks %lld / %lld / %lld / %lld, alpha %.3f..%.3f, hitch %d ticks, %lld dropped", counts[0], counts[1], counts[2], counts[3],
                   alphaMin, alphaMax, burst, hitch.dropped()),
               std::llabs(counts[0] - 600) <= 1 && std::llabs(counts[1] - 600) <= 1 && std::llabs(counts[2] - 600) <= 1 &&
                   std::llabs(counts[3] - 600) <= 1 && alphaMin >= 0.0f && alphaMax < 1.0f && burst == FixedStepClock::kMaxTicksPerFrame &&
                   hitch.dropped() == 56);
    }

    // ---- T333: render interpolation: continuous motion at any render rate; cuts and spinning parts are not blended -------
    {
        // A part moving on a circle (r = 3 m, 0.5 rev/s) and turning with it, simulated at 60 Hz, shown at 144 Hz:
        // the displayed position never jumps more than one tick's travel, and is never off the true path by more than
        // the chord sag of one tick.
        auto worldAt = [](double t) {
            const float a = static_cast<float>(t * 3.14159265358979);   // 0.5 rev/s
            return translate({3.0f * std::cos(a), 0.0f, 3.0f * std::sin(a)}) * rotateY(-a);
        };
        FixedStepClock c;
        Mat4 prev = worldAt(0.0), cur = worldAt(0.0);
        double simT = 0.0;
        float maxStep = 0.0f, maxErr = 0.0f;
        Vec3 lastShown{3.0f, 0.0f, 0.0f};
        for (int f = 0; f < 144 * 4; ++f) {
            const int n = c.advance(1.0 / 144.0);
            for (int k = 0; k < n; ++k) {
                prev = cur;
                simT += FixedStepClock::kTick;
                cur = worldAt(simT);
            }
            Mat4 shown;
            blendTickWorld(prev, cur, c.alpha(), shown);
            const Vec3 p{shown.at(0, 3), shown.at(1, 3), shown.at(2, 3)};
            maxStep = std::max(maxStep, length(p - lastShown));
            maxErr = std::max(maxErr, std::fabs(length(p) - 3.0f));
            lastShown = p;
        }
        const float tickTravel = 3.0f * 3.14159265f * FixedStepClock::kTick;   // arc length of one tick
        Mat4 cut, spin;
        const bool cutRefused = !blendTickWorld(translate({0, 0, 0}), translate({10, 0, 0}), 0.5f, cut);
        const bool spinRefused = !blendTickWorld(rotateY(0.0f), rotateY(radians(90.0f)), 0.5f, spin);
        RigPose a = RigPose::look({0, 2, 0}, {0, 2, -10}, 60.0f), b = RigPose::look({1, 2, 0}, {1, 2, -10}, 70.0f), mid;
        const bool poseOk = blendTickPose(a, b, 0.5f, mid) && std::fabs(mid.eye.x - 0.5f) < 1e-5f && std::fabs(mid.fovDeg - 65.0f) < 1e-4f;
        RigPose behind = RigPose::look({0, 2, 0}, {0, 2, 10}, 60.0f), out;
        const bool poseCut = !blendTickPose(a, behind, 0.5f, out) && length(out.eye - behind.eye) < 1e-6f;
        record("T333", "Render interpolation: a part simulated at 60 Hz and shown at 144 Hz moves continuously; cuts / spinning parts / camera cuts snap",
               "max displayed step <= 1 tick of travel (0.157 m), off the circle < 2 mm, 10 m jump and 90 deg turn refused, camera mid-pose exact, 180 deg camera cut refused",
               fmt("max step %.4f m (tick travel %.4f), max radial error %.4f m, cut refused %d, spin refused %d, pose %d, camera cut %d", maxStep, tickTravel,
                   maxErr, cutRefused ? 1 : 0, spinRefused ? 1 : 0, poseOk ? 1 : 0, poseCut ? 1 : 0),
               maxStep <= tickTravel * 1.01f && maxErr < 0.002f && cutRefused && spinRefused && poseOk && poseCut);
    }

    // ---- T334: vision worker: the threaded pipeline gives exactly the inline results, in tick order ------------------------
    {
        constexpr int W = 320, H = 240;
        auto frame = [](int tick, int uav) {   // a warm target blob moving across a noisy-ish background
            std::vector<uint8_t> img(static_cast<size_t>(W) * H);
            const int cx = 40 + tick * 3 + uav * 20, cy = 100 + (tick % 7) * 2;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                    img[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>(d2 < 36 ? 250 : 40 + ((x * 7 + y * 13 + tick) % 23));
                }
            return img;
        };
        auto runPipeline = [&](bool threaded) {
            std::vector<SensorModel> sensors;
            sensors.emplace_back(101u);
            sensors.emplace_back(102u);
            for (SensorModel& s : sensors) s.nuc();
            ThermalTracker tracker;
            VisionWorker worker(sensors, tracker, threaded);
            std::vector<double> log;
            for (int tick = 0; tick < 40; ++tick) {
                VisionJob job;
                job.width = W;
                job.height = H;
                job.paused = tick == 20;
                for (int u = 0; u < 2; ++u) {
                    job.rendered[u] = !(u == 1 && tick % 5 == 3);   // UAV 2 drops some frames
                    if (job.rendered[u]) job.image[u] = frame(tick, u);
                }
                worker.submit(std::move(job));
                const VisionResult r = worker.join();
                for (int u = 0; u < 2; ++u) {
                    log.push_back(r.valid[u] ? 1.0 : 0.0);
                    for (const Measurement2D& m : r.targets[u]) {
                        log.push_back(m.u);
                        log.push_back(m.v);
                    }
                    long long sum = 0;
                    for (uint8_t v : r.image[u]) sum += v;
                    log.push_back(static_cast<double>(sum));
                }
                log.push_back(r.hasDetection ? r.detection.u + 10.0 * r.detection.v + (r.detection.found ? 100.0 : 0.0) : -1.0);
            }
            return log;
        };
        const std::vector<double> inl = runPipeline(false), thr = runPipeline(true);
        size_t diff = inl.size() == thr.size() ? 0 : 1;
        for (size_t i = 0; i < std::min(inl.size(), thr.size()); ++i) diff += inl[i] != thr[i] ? 1 : 0;
        record("T334", "UAV vision on the worker thread: 40 ticks (dropped frames, a paused tick) give bit-identical results to the inline run",
               "same number of logged values, 0 differences (sensor noise, blobs, image sums, v4 tracker)",
               fmt("%zu / %zu values, %zu differences", inl.size(), thr.size(), diff), diff == 0 && inl.size() > 200);
    }
}
