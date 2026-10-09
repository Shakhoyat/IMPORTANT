// v9 M9: play-session recording for a deterministic replay ("F9 records, --replay re-renders").
//
// The interactive loop is a sequence of frames. Each frame has a wall-clock length (which decides how many fixed
// 1/60 s ticks run and the render blend), input delivered at two points (the input script at the top of the frame,
// the window callbacks in glfwPollEvents at its end) and pointer reads made by the code in between. The log keeps
// exactly that, from the first frame on:
//
//   F dt ww wh fw fh  frame start, dt = its frame time (hex float: the replay gets the same double), window and
//                   framebuffer size of that frame
//   B               end of the top-of-frame input (script); inputs after it are delivered at the frame's poll
//   K key act mods  key callback            M button act        mouse button callback
//   C x y           cursor callback         W dy                scroll callback
//   P i x y         the i-th pointer read of this frame returned (x, y) (written only when the value changed)
//   H tick hash     world checkpoint after that tick (FNV-1a over the parts' world matrices, clock, mission)
//
// The header (lines '#! key value') holds what the world is built from: seed, world arguments, the player's
// settings, the window and framebuffer size, the starting quality and the clip (F9 in / out ticks).
// Replaying runs the same frames with the same dt and delivers the same inputs at the same points, so every tick
// sees exactly the same state as in the live session; the checkpoints prove it (or name the first tick that differs).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ts {

struct SessionEntry {
    char kind = 'F';                  // F B K M C W P H
    int a = 0, b = 0, c = 0, d = 0;   // K: key action mods; M: button action; P: read index; F: ww wh fw fh
    double x = 0.0, y = 0.0;          // F: dt (x); C: x y; W: dy (x); P: x y
    long long tick = 0;               // H
    uint64_t hash = 0;                // H
    bool isInput() const { return kind == 'K' || kind == 'M' || kind == 'C' || kind == 'W'; }
};

struct SessionHeader {
    uint32_t seed = 0;
    std::vector<std::string> worldArgs;   // command-line arguments that shape the world (--time, --day-ops, ...)
    std::string settings;                 // ThermalSense.ini text in effect (serializeSettings)
    int windowW = 0, windowH = 0, fbW = 0, fbH = 0;
    int quality = -1;
    long long clipIn = 0, clipOut = -1;   // ticks; clipOut < 0 = to the end
};

std::string serializeSession(const SessionHeader& h, const std::vector<SessionEntry>& entries);
// Parses a session file; false with a message naming the line on an error.
bool parseSession(const std::string& text, SessionHeader& h, std::vector<SessionEntry>& entries, std::string& error);

// FNV-1a 64 over raw bytes (checkpoint hashes are over bit patterns: equal hash <=> identical floats, practically).
uint64_t fnv1a(const void* data, size_t bytes, uint64_t h = 1469598103934665603ull);

// One frame of a session as the replay consumes it.
struct SessionFrame {
    double dt = 0.0;
    int windowW = 0, windowH = 0, fbW = 0, fbH = 0;
    std::vector<SessionEntry> top, poll;            // inputs before B / after B
    std::map<int, std::pair<double, double>> reads;  // pointer read index -> value
    std::vector<SessionEntry> checks;                // H entries
};
// Splits the entry stream into frames (entries before the first F are ignored).
std::vector<SessionFrame> sessionFrames(const std::vector<SessionEntry>& entries);

// Recorder side: appends entries; pointer reads are numbered per frame and logged only when the value changes.
class SessionRecorder {
public:
    void frame(double dt, int ww, int wh, int fw, int fh);
    void topDone() { push({'B'}); }
    void key(int key, int action, int mods);
    void button(int button, int action);
    void cursor(double x, double y);
    void scroll(double dy);
    void pointerRead(double x, double y);
    void check(long long tick, uint64_t hash);
    const std::vector<SessionEntry>& entries() const { return entries_; }
    size_t frames() const { return frames_; }
    bool empty() const { return entries_.empty(); }

private:
    void push(const SessionEntry& e) { entries_.push_back(e); }
    std::vector<SessionEntry> entries_;
    size_t frames_ = 0;
    int reads_ = 0;
    bool havePointer_ = false;
    double px_ = 0.0, py_ = 0.0;
};

// Replay side of the pointer reads: returns what the live session's read with the same index returned.
class SessionPointer {
public:
    void beginFrame(const SessionFrame* f) {
        frame_ = f;
        reads_ = 0;
    }
    void read(double& x, double& y);

private:
    const SessionFrame* frame_ = nullptr;
    int reads_ = 0;
    double px_ = 0.0, py_ = 0.0;
};

}  // namespace ts
