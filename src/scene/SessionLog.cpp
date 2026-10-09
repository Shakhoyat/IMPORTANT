#include "scene/SessionLog.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace ts {

namespace {

std::string hexd(double v) {   // exact round trip through text
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%a", v);
    return buf;
}

bool readDouble(std::istringstream& in, double& v) {
    std::string tok;
    if (!(in >> tok)) return false;
    char* end = nullptr;
    v = std::strtod(tok.c_str(), &end);
    return end && *end == '\0';
}

}  // namespace

uint64_t fnv1a(const void* data, size_t bytes, uint64_t h) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

std::string serializeSession(const SessionHeader& h, const std::vector<SessionEntry>& entries) {
    std::ostringstream o;
    o << "#! ThermalSense session 1\n";
    o << "#! seed " << h.seed << "\n";
    for (const std::string& a : h.worldArgs) o << "#! arg " << a << "\n";
    std::istringstream ini(h.settings);
    for (std::string line; std::getline(ini, line);)
        if (!line.empty()) o << "#! ini " << line << "\n";
    o << "#! window " << h.windowW << " " << h.windowH << " " << h.fbW << " " << h.fbH << "\n";
    o << "#! quality " << h.quality << "\n";
    o << "#! clip " << h.clipIn << " " << h.clipOut << "\n";
    for (const SessionEntry& e : entries) {
        switch (e.kind) {
            case 'F': o << "F " << hexd(e.x) << " " << e.a << " " << e.b << " " << e.c << " " << e.d << "\n"; break;
            case 'B': o << "B\n"; break;
            case 'K': o << "K " << e.a << " " << e.b << " " << e.c << "\n"; break;
            case 'M': o << "M " << e.a << " " << e.b << "\n"; break;
            case 'C': o << "C " << hexd(e.x) << " " << hexd(e.y) << "\n"; break;
            case 'W': o << "W " << hexd(e.x) << "\n"; break;
            case 'P': o << "P " << e.a << " " << hexd(e.x) << " " << hexd(e.y) << "\n"; break;
            case 'H': {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(e.hash));
                o << "H " << e.tick << " " << buf << "\n";
                break;
            }
            default: break;
        }
    }
    return o.str();
}

bool parseSession(const std::string& text, SessionHeader& h, std::vector<SessionEntry>& entries, std::string& error) {
    h = SessionHeader{};
    entries.clear();
    std::istringstream all(text);
    std::string line;
    int n = 0;
    bool magic = false;
    auto fail = [&](const char* what) {
        error = "line " + std::to_string(n) + ": " + what;
        return false;
    };
    while (std::getline(all, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("#!", 0) == 0) {
            std::istringstream in(line.substr(2));
            std::string key;
            in >> key;
            std::string rest;
            std::getline(in >> std::ws, rest);
            std::istringstream r(rest);
            if (key == "ThermalSense") magic = true;
            else if (key == "seed") r >> h.seed;
            else if (key == "arg") h.worldArgs.push_back(rest);
            else if (key == "ini") h.settings += rest + "\n";
            else if (key == "window") r >> h.windowW >> h.windowH >> h.fbW >> h.fbH;
            else if (key == "quality") r >> h.quality;
            else if (key == "clip") r >> h.clipIn >> h.clipOut;
            continue;
        }
        if (line[0] == '#') continue;
        if (!magic) return fail("not a ThermalSense session (missing '#! ThermalSense session 1')");
        std::istringstream in(line);
        SessionEntry e;
        std::string kind;
        in >> kind;
        if (kind.size() != 1) return fail("unknown entry");
        e.kind = kind[0];
        bool ok = true;
        switch (e.kind) {
            case 'F': ok = readDouble(in, e.x) && static_cast<bool>(in >> e.a >> e.b >> e.c >> e.d); break;
            case 'B': break;
            case 'K': ok = static_cast<bool>(in >> e.a >> e.b >> e.c); break;
            case 'M': ok = static_cast<bool>(in >> e.a >> e.b); break;
            case 'C': ok = readDouble(in, e.x) && readDouble(in, e.y); break;
            case 'W': ok = readDouble(in, e.x); break;
            case 'P': ok = static_cast<bool>(in >> e.a) && readDouble(in, e.x) && readDouble(in, e.y); break;
            case 'H': {
                std::string hx;
                ok = static_cast<bool>(in >> e.tick >> hx);
                if (ok) {
                    char* end = nullptr;
                    e.hash = std::strtoull(hx.c_str(), &end, 16);
                    ok = end && *end == '\0';
                }
                break;
            }
            default: return fail("unknown entry");
        }
        if (!ok) return fail("bad values");
        entries.push_back(e);
    }
    if (!magic) {
        error = "not a ThermalSense session (missing '#! ThermalSense session 1')";
        return false;
    }
    return true;
}

std::vector<SessionFrame> sessionFrames(const std::vector<SessionEntry>& entries) {
    std::vector<SessionFrame> frames;
    bool top = true;
    for (const SessionEntry& e : entries) {
        if (e.kind == 'F') {
            frames.emplace_back();
            SessionFrame& f = frames.back();
            f.dt = e.x;
            f.windowW = e.a;
            f.windowH = e.b;
            f.fbW = e.c;
            f.fbH = e.d;
            top = true;
            continue;
        }
        if (frames.empty()) continue;
        SessionFrame& f = frames.back();
        if (e.kind == 'B') top = false;
        else if (e.isInput()) (top ? f.top : f.poll).push_back(e);
        else if (e.kind == 'P') f.reads[e.a] = {e.x, e.y};
        else if (e.kind == 'H') f.checks.push_back(e);
    }
    return frames;
}

void SessionRecorder::frame(double dt, int ww, int wh, int fw, int fh) {
    SessionEntry e{'F'};
    e.x = dt;
    e.a = ww;
    e.b = wh;
    e.c = fw;
    e.d = fh;
    push(e);
    ++frames_;
    reads_ = 0;
}

void SessionRecorder::key(int key, int action, int mods) {
    SessionEntry e{'K'};
    e.a = key;
    e.b = action;
    e.c = mods;
    push(e);
}

void SessionRecorder::button(int button, int action) {
    SessionEntry e{'M'};
    e.a = button;
    e.b = action;
    push(e);
}

void SessionRecorder::cursor(double x, double y) {
    SessionEntry e{'C'};
    e.x = x;
    e.y = y;
    push(e);
}

void SessionRecorder::scroll(double dy) {
    SessionEntry e{'W'};
    e.x = dy;
    push(e);
}

void SessionRecorder::pointerRead(double x, double y) {
    const int i = reads_++;
    if (havePointer_ && x == px_ && y == py_) return;
    havePointer_ = true;
    px_ = x;
    py_ = y;
    SessionEntry e{'P'};
    e.a = i;
    e.x = x;
    e.y = y;
    push(e);
}

void SessionRecorder::check(long long tick, uint64_t hash) {
    SessionEntry e{'H'};
    e.tick = tick;
    e.hash = hash;
    push(e);
}

void SessionPointer::read(double& x, double& y) {
    const int i = reads_++;
    if (frame_) {
        const auto it = frame_->reads.find(i);
        if (it != frame_->reads.end()) {
            px_ = it->second.first;
            py_ = it->second.second;
        }
    }
    x = px_;
    y = py_;
}

}  // namespace ts
