#include "scene/InputScript.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ts {

int scriptKeyCode(const std::string& raw) {
    std::string n;
    for (char c : raw) n.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (n.size() == 1 && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= '0' && n[0] <= '9'))) return n[0];
    if (n.size() >= 2 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1]))) {
        const int f = std::atoi(n.c_str() + 1);
        if (f >= 1 && f <= 12) return 289 + f;   // GLFW_KEY_F1 = 290
    }
    struct Named {
        const char* name;
        int code;
    };
    static const Named kNames[] = {
        {"SPACE", 32},         {"APOSTROPHE", 39},  {"COMMA", 44},        {"MINUS", 45},       {"PERIOD", 46},
        {"SLASH", 47},         {"SEMICOLON", 59},   {"EQUAL", 61},        {"LEFT_BRACKET", 91}, {"BACKSLASH", 92},
        {"RIGHT_BRACKET", 93}, {"GRAVE", 96},       {"ESCAPE", 256},      {"ESC", 256},         {"ENTER", 257},
        {"TAB", 258},          {"BACKSPACE", 259},  {"INSERT", 260},      {"DELETE", 261},      {"RIGHT", 262},
        {"LEFT", 263},         {"DOWN", 264},       {"UP", 265},          {"PAGE_UP", 266},     {"PAGE_DOWN", 267},
        {"HOME", 268},         {"END", 269},        {"PAUSE", 284},       {"KP_ENTER", 335},    {"LEFT_SHIFT", 340},
        {"SHIFT", 340},        {"LEFT_CONTROL", 341}, {"CTRL", 341},      {"LEFT_ALT", 342},    {"ALT", 342},
        {"RIGHT_SHIFT", 344},  {"RIGHT_CONTROL", 345}, {"RIGHT_ALT", 346},
    };
    for (const Named& k : kNames)
        if (n == k.name) return k.code;
    return -1;
}

bool parseInputScript(const std::string& text, std::vector<ScriptEvent>& out, std::string& error) {
    out.clear();
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    double prev = 0.0;
    auto fail = [&](const std::string& why) {
        error = "line " + std::to_string(lineNo) + ": " + why;
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNo;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        std::string when, verb;
        if (!(ls >> when)) continue;   // blank / comment
        if (!(ls >> verb)) return fail("missing command after the time");
        char* end = nullptr;
        const bool relative = when[0] == '+';
        const double v = std::strtod(when.c_str() + (relative ? 1 : 0), &end);
        if (end == when.c_str() + (relative ? 1 : 0) || *end != '\0' || v < 0.0) return fail("bad time '" + when + "'");
        const double t = relative ? prev + v : v;
        prev = t;
        ScriptEvent e;
        e.time = t;
        if (verb == "key") {
            std::string name, act;
            if (!(ls >> name >> act)) return fail("key needs NAME down|up|tap");
            e.kind = ScriptEvent::Kind::Key;
            e.code = scriptKeyCode(name);
            if (e.code < 0) return fail("unknown key '" + name + "'");
            if (act == "down" || act == "tap") e.action = 1;
            else if (act == "up") e.action = 0;
            else return fail("key action must be down, up or tap");
            out.push_back(e);
            if (act == "tap") {
                e.action = 0;
                e.time = t + 0.08;
                out.push_back(e);
            }
        } else if (verb == "button") {
            std::string which, act;
            if (!(ls >> which >> act)) return fail("button needs left|right|middle down|up|click");
            e.kind = ScriptEvent::Kind::Button;
            e.code = which == "left" ? 0 : which == "right" ? 1 : which == "middle" ? 2 : -1;
            if (e.code < 0) return fail("button must be left, right or middle");
            if (act == "down" || act == "click") e.action = 1;
            else if (act == "up") e.action = 0;
            else return fail("button action must be down, up or click");
            out.push_back(e);
            if (act == "click") {
                e.action = 0;
                e.time = t + 0.08;
                out.push_back(e);
            }
        } else if (verb == "look") {
            e.kind = ScriptEvent::Kind::Look;
            if (!(ls >> e.x >> e.y)) return fail("look needs DX DY [SECONDS]");
            if (!(ls >> e.duration)) e.duration = 0.0;
            if (e.duration < 0.0) return fail("look duration must not be negative");
            out.push_back(e);
        } else if (verb == "pointer") {
            e.kind = ScriptEvent::Kind::Pointer;
            if (!(ls >> e.x >> e.y)) return fail("pointer needs X Y (0..1)");
            if (e.x < 0.0 || e.x > 1.0 || e.y < 0.0 || e.y > 1.0) return fail("pointer X Y must be in 0..1");
            out.push_back(e);
        } else if (verb == "shot" || verb == "log") {
            e.kind = verb == "shot" ? ScriptEvent::Kind::Shot : ScriptEvent::Kind::Log;
            std::getline(ls >> std::ws, e.text);
            while (!e.text.empty() && std::isspace(static_cast<unsigned char>(e.text.back()))) e.text.pop_back();
            if (e.kind == ScriptEvent::Kind::Shot && e.text.empty()) return fail("shot needs a NAME");
            if (e.kind == ScriptEvent::Kind::Shot && e.text.find_first_of("/\\:") != std::string::npos)
                return fail("shot NAME must be a plain file name");
            out.push_back(e);
        } else if (verb == "quit") {
            e.kind = ScriptEvent::Kind::Quit;
            out.push_back(e);
        } else {
            return fail("unknown command '" + verb + "'");
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ScriptEvent& a, const ScriptEvent& b) { return a.time < b.time; });
    error.clear();
    return true;
}

void ScriptPlayer::load(std::vector<ScriptEvent> events) {
    events_ = std::move(events);
    next_ = 0;
    looks_.clear();
    keys_.clear();
    buttons_[0] = buttons_[1] = buttons_[2] = false;
}

std::vector<ScriptEvent> ScriptPlayer::due(double t) {
    std::vector<ScriptEvent> out;
    while (next_ < events_.size() && events_[next_].time <= t) {
        const ScriptEvent& e = events_[next_++];
        if (e.kind == ScriptEvent::Kind::Key) {
            keys_.erase(std::remove(keys_.begin(), keys_.end(), e.code), keys_.end());
            if (e.action) keys_.push_back(e.code);
        } else if (e.kind == ScriptEvent::Kind::Button && e.code >= 0 && e.code < 3) {
            buttons_[e.code] = e.action != 0;
        }
        if (e.kind == ScriptEvent::Kind::Look && e.duration > 0.0) {
            looks_.push_back({e.time, e.time + e.duration, e.x, e.y, e.time});
            continue;
        }
        out.push_back(e);
    }
    // Active looks: the part of the move between the previous call and now (clamped to the look's interval).
    for (size_t i = 0; i < looks_.size();) {
        ActiveLook& l = looks_[i];
        const double now = std::min(t, l.end);
        const double share = (now - l.lastT) / (l.end - l.start);
        l.lastT = now;
        if (share > 0.0) {
            ScriptEvent e;
            e.kind = ScriptEvent::Kind::Look;
            e.time = now;
            e.x = l.x * share;
            e.y = l.y * share;
            out.push_back(e);
        }
        if (t >= l.end) looks_.erase(looks_.begin() + static_cast<long>(i));
        else ++i;
    }
    return out;
}

bool ScriptPlayer::keyHeld(int code) const { return std::find(keys_.begin(), keys_.end(), code) != keys_.end(); }

bool ScriptPlayer::buttonHeld(int button) const { return button >= 0 && button < 3 && buttons_[button]; }

}  // namespace ts
