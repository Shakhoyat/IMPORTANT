// v9 M0: scripted input for the real (windowed) program. A text script of timed key / mouse events is fed through
// the same callbacks GLFW calls (App::onKey / onMouseButton / onCursor) and through the held-key queries the loop
// polls, so every control path can be exercised in ThermalSense.exe and checked from its frames and log lines.
// Later (M9) the same event stream records a play session for the deterministic replay.
//
// Script lines (one event each, '#' starts a comment; time in seconds from the start, or '+dt' after the previous):
//   1.0   key W down | up | tap          GLFW key names: A..Z, 0..9, SPACE, ESCAPE, ENTER, TAB, LEFT_SHIFT, F1..F12, ...
//   2.0   button left | right | middle down | up | click
//   2.5   look DX DY [SECONDS]           relative mouse move in pixels, spread evenly over SECONDS (default: at once)
//   3.0   pointer X Y                    absolute pointer position in 0..1 of the window (selection clicks)
//   3.5   shot NAME                      save the next presented frame as NAME.png (--frames DIR)
//   4.0   log LABEL                      print the camera / control state and the frame timing since the last log
//   9.0   quit
#pragma once

#include <string>
#include <vector>

namespace ts {

struct ScriptEvent {
    enum class Kind { Key, Button, Look, Pointer, Shot, Log, Quit } kind = Kind::Key;
    double time = 0.0;
    int code = 0;          // GLFW key code / mouse button (0 left, 1 right, 2 middle)
    int action = 0;        // 1 press, 0 release
    double x = 0.0, y = 0.0, duration = 0.0;   // Look: pixels (+ seconds); Pointer: 0..1
    std::string text;      // Shot / Log
};

// GLFW key code of a key name ("W", "SPACE", "LEFT_SHIFT", "F1", ...); -1 when unknown.
int scriptKeyCode(const std::string& name);

// Parses a script. 'tap' / 'click' expand into a press and a release 0.08 s later. Events come out sorted by time
// (stable: same-time events keep their order). Returns false with a message naming the line on a syntax error.
bool parseInputScript(const std::string& text, std::vector<ScriptEvent>& out, std::string& error);

// Plays a parsed script against a clock. due(t) returns the events whose time has come, in order; Look events are
// returned once per call while active, with the share of the move for the elapsed interval in x / y.
class ScriptPlayer {
public:
    void load(std::vector<ScriptEvent> events);
    bool active() const { return !events_.empty() && (next_ < events_.size() || !looks_.empty()); }
    bool finished() const { return !events_.empty() && next_ >= events_.size() && looks_.empty(); }
    std::vector<ScriptEvent> due(double t);
    bool keyHeld(int code) const;
    bool buttonHeld(int button) const;

private:
    std::vector<ScriptEvent> events_;
    size_t next_ = 0;
    struct ActiveLook {
        double start, end, x, y, lastT;
    };
    std::vector<ActiveLook> looks_;
    std::vector<int> keys_;
    bool buttons_[3] = {false, false, false};
};

}  // namespace ts
