// v9 (game feel, game look, performance): T330+. CPU only.
#include <cmath>
#include <string>
#include <vector>

#include "TestKit.h"
#include "scene/InputScript.h"

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
}
