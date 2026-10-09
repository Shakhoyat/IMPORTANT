// v9 M9: session recording (F9) and the deterministic replay (--replay FILE [--record OUT.mp4]).
//
// Recording: every interactive run logs its frames from the first one on (scene/SessionLog.h): frame time, the
// external inputs (window callbacks, input script) at the point of the frame where they arrived, the pointer reads,
// and a world checkpoint every 60 ticks. F9 marks a clip; the second F9 writes the whole log with the clip range to
// recordings/session_*.tsrec. Replaying builds the same world (seed, world arguments, settings), creates the window
// at the recorded size, and runs the recorded frames with their recorded frame times through the same loop, giving
// the recorded inputs to the same handlers at the same points; the window's own input is ignored (Esc stops). With
// --record the ticks inside the clip are also rendered offline, one 60 fps video frame per tick (Record.cpp).
#include <glad/gl.h>

#include "app/App.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "rendering/Hud.h"
#include "scene/GameMenu.h"

#if !defined(TS_NO_GLFW)
#include <GLFW/glfw3.h>
#endif

namespace ts {

uint64_t App::worldHash() const {
    uint64_t h = fnv1a(nullptr, 0);
    for (const SceneNode* n : scene_->leaves()) {
        h = fnv1a(n->world.m, sizeof(n->world.m), h);
        const unsigned char vis = n->visible ? 1 : 0;
        h = fnv1a(&vis, 1, h);
    }
    const float t = scene_->time();
    h = fnv1a(&t, sizeof(t), h);
    const MissionState& ms = scene_->mission().state();
    const int m[6] = {ms.wave, ms.score, ms.kills, ms.combinedKills, ms.breaches, ms.spawned};
    h = fnv1a(m, sizeof(m), h);
    h = fnv1a(&ms.integrity, sizeof(ms.integrity), h);
    const int ctl[3] = {possessed_, possessedRover_, paused_ ? 1 : 0};
    h = fnv1a(ctl, sizeof(ctl), h);
    if (tickPoseValid_[1]) {   // the camera the player steers (spring arm, head bob, FOV kick) is part of the run
        const RigPose& p = tickPose_[1];
        const float c[9] = {p.eye.x, p.eye.y, p.eye.z, p.rot.w, p.rot.x, p.rot.y, p.rot.z, p.fovDeg, p.nearZ};
        h = fnv1a(c, sizeof(c), h);
    }
    return h;
}

std::string App::sessionFilePath() const {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char name[64];
    std::strftime(name, sizeof(name), "session_%Y%m%d_%H%M%S.tsrec", &tm);
    return (std::filesystem::path(opt_.sessionDir) / name).string();
}

bool App::saveSession(const std::string& path, long long clipIn, long long clipOut) {
    SessionHeader h;
    h.seed = opt_.seed;
    h.worldArgs = opt_.worldArgs;
    h.settings = sessionSettings_;
    h.quality = sessionQuality_;
    h.clipIn = clipIn;
    h.clipOut = clipOut;
    for (const SessionEntry& e : session_.entries())
        if (e.kind == 'F') {
            h.windowW = e.a;
            h.windowH = e.b;
            h.fbW = e.c;
            h.fbH = e.d;
            break;
        }
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f) f << serializeSession(h, session_.entries());
    const bool ok = static_cast<bool>(f);
    std::printf("Session %s %s (%zu frames, clip ticks %lld..%lld = %.1f s; replay: --replay \"%s\" --record OUT.mp4)\n",
                ok ? "saved to" : "could NOT be written to", path.c_str(), session_.frames(), clipIn, clipOut,
                (clipOut - clipIn) / 60.0, path.c_str());
    std::fflush(stdout);
    return ok;
}

bool App::loadReplay() {
    std::ifstream f(opt_.replayPath, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "--replay: cannot open %s\n", opt_.replayPath.c_str());
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<SessionEntry> entries;
    std::string error;
    if (!parseSession(ss.str(), replayHeader_, entries, error)) {
        std::fprintf(stderr, "--replay %s: %s\n", opt_.replayPath.c_str(), error.c_str());
        return false;
    }
    replayFrames_ = sessionFrames(entries);
    if (replayFrames_.empty()) {
        std::fprintf(stderr, "--replay %s: no frames\n", opt_.replayPath.c_str());
        return false;
    }
    // The settings of the recorded run (mouse sensitivity, invert Y, FOV, head bob change what the inputs do).
    std::vector<std::string> warnings;
    gs_ = parseSettings(replayHeader_.settings, &warnings);
    for (const std::string& w : warnings) std::fprintf(stderr, "--replay settings: %s\n", w.c_str());
    opt_.iniPath.clear();   // a replay never writes the player's ThermalSense.ini
    if (replayHeader_.quality >= 0) opt_.quality = replayHeader_.quality;
    // The window is the recorded one (pointer coordinates, menu layout); --size is the video size.
    videoW_ = opt_.width;
    videoH_ = opt_.height;
    opt_.width = replayFrames_.front().windowW > 0 ? replayFrames_.front().windowW : opt_.width;
    opt_.height = replayFrames_.front().windowH > 0 ? replayFrames_.front().windowH : opt_.height;
    opt_.fullscreen = false;
    opt_.inputScript.clear();
    replaying_ = true;
    recordSession_ = false;
    long long ticks = 0;
    double seconds = 0.0;
    for (const SessionFrame& fr : replayFrames_) seconds += fr.dt;
    ticks = static_cast<long long>(seconds * 60.0);
    std::printf("Replay %s: seed %u, %zu frames (%.1f s, ~%lld ticks), clip ticks %lld..%lld, window %dx%d%s\n",
                opt_.replayPath.c_str(), replayHeader_.seed, replayFrames_.size(), seconds, ticks, replayHeader_.clipIn,
                replayHeader_.clipOut, opt_.width, opt_.height, opt_.outPath.empty() ? "" : (" -> " + opt_.outPath).c_str());
    std::fflush(stdout);
    return true;
}

void App::replayCheck(long long tick) {
    const SessionFrame& f = replayFrames_[replayFrame_ - 1];
    for (const SessionEntry& e : f.checks) {
        if (e.tick != tick) continue;
        if (e.hash == worldHash()) {
            ++replayChecksOk_;
        } else {
            ++replayChecksBad_;
            if (replayFirstBad_ < 0) {
                replayFirstBad_ = tick;
                std::printf("replay: checkpoint MISMATCH at tick %lld (%.2f s)\n", tick, tick / 60.0);
                std::fflush(stdout);
            }
        }
    }
}

void App::replayFinish() {
    if (replayFinished_) return;
    replayFinished_ = true;
    std::printf("replay check: %lld / %lld checkpoints identical%s (%zu frames, %lld ticks)\n", replayChecksOk_,
                replayChecksOk_ + replayChecksBad_, replayChecksBad_ ? "  -- DIVERGED" : "", replayFrames_.size(),
                simClock_.ticks());
    std::fflush(stdout);
    replayVideoClose();
}

#if !defined(TS_NO_GLFW)

void App::inputKey(int key, int action, int mods) {
    if (replaying_) {   // the window's own keys do not steer a replay; Esc stops it
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS && window_) glfwSetWindowShouldClose(window_, 1);
        return;
    }
    if (recordSession_) session_.key(key, action, mods);
    onKey(key, action, mods);
}

void App::inputButton(int button, int action) {
    if (replaying_) return;
    if (recordSession_) session_.button(button, action);
    onMouseButton(button, action);
}

void App::inputCursor(double x, double y) {
    if (replaying_) return;
    if (recordSession_) session_.cursor(x, y);
    onCursor(x, y);
}

void App::inputScroll(double dy) {
    if (replaying_) return;
    if (recordSession_) session_.scroll(dy);
    onScroll(dy);
}

void App::windowSize(int& w, int& h) const {
    if (replaying_ && replayFrame_ > 0) {
        const SessionFrame& f = replayFrames_[replayFrame_ - 1];
        w = f.windowW;
        h = f.windowH;
        return;
    }
    w = h = 1;
    if (window_) glfwGetWindowSize(window_, &w, &h);
}

void App::framebufferSize(int& w, int& h) const {
    if (replaying_ && replayFrame_ > 0) {
        const SessionFrame& f = replayFrames_[replayFrame_ - 1];
        w = f.fbW;
        h = f.fbH;
        return;
    }
    w = h = 0;
    if (window_) glfwGetFramebufferSize(window_, &w, &h);
}

void App::drawSessionIndicator(int w, int h) {
    if (!recordSession_) return;   // live sessions only: never in a replay or its video
    const double now = glfwGetTime();
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f;
    if (clipIn_ >= 0) {
        const int secs = static_cast<int>(now - clipWall_);
        char line[32];
        std::snprintf(line, sizeof(line), "REC %02d:%02d", secs / 60, secs % 60);
        const float x = w * 0.5f - 46 * s, y = 52 * s;
        hud.rect(x - 8 * s, y - 6 * s, 108 * s, 30 * s, Rgba{0.0f, 0.0f, 0.0f, 0.45f});
        if (static_cast<int>(now * 2.0) % 2 == 0) hud.rect(x, y + 3 * s, 12 * s, 12 * s, Rgba{1.0f, 0.2f, 0.15f, 1.0f});
        hud.text(x + 20 * s, y, line, 17 * s, Rgba{1.0f, 1.0f, 1.0f, 1.0f});
    } else if (now < sessionToastUntil_ && !sessionToast_.empty()) {
        const float tw = hud.textWidth(sessionToast_.c_str(), 15 * s);
        const float x = w * 0.5f - tw * 0.5f, y = 52 * s;
        hud.rect(x - 10 * s, y - 6 * s, tw + 20 * s, 28 * s, Rgba{0.0f, 0.0f, 0.0f, 0.45f});
        hud.text(x, y, sessionToast_.c_str(), 15 * s, Rgba{1.0f, 0.75f, 0.15f, 1.0f});
    }
}

void App::toggleSessionClip() {
    if (replaying_ || !recordSession_) return;
    const long long tick = simClock_.ticks();
    if (clipIn_ < 0) {
        clipIn_ = tick;
        clipWall_ = glfwGetTime();
        std::printf("Session clip: recording from tick %lld (F9 again saves it)\n", tick);
        std::fflush(stdout);
        return;
    }
    const std::string path = sessionFilePath();
    const bool ok = saveSession(path, clipIn_, tick);
    sessionToast_ = ok ? "CLIP SAVED  " + std::filesystem::path(path).filename().string() : "CLIP NOT SAVED (see console)";
    sessionToastUntil_ = glfwGetTime() + 4.0;
    clipIn_ = -1;
}

// Starts the next recorded frame: its frame time, the inputs that arrived at its top (input script). False when the
// recording is over.
bool App::replayBeginFrame(double& frameSeconds) {
    if (replayFrame_ >= replayFrames_.size()) return false;
    const SessionFrame& f = replayFrames_[replayFrame_++];
    frameSeconds = f.dt;
    replayPointer_.beginFrame(&f);
    for (const SessionEntry& e : f.top) replayDeliver(e);
    return true;
}

void App::replayDeliverPoll() {
    if (replayFrame_ == 0) return;
    for (const SessionEntry& e : replayFrames_[replayFrame_ - 1].poll) replayDeliver(e);
}

void App::replayDeliver(const SessionEntry& e) {
    replayDispatch_ = true;
    switch (e.kind) {
        case 'K':
            if (e.b == GLFW_PRESS) {
                if (std::find(replayKeys_.begin(), replayKeys_.end(), e.a) == replayKeys_.end()) replayKeys_.push_back(e.a);
            } else if (e.b == GLFW_RELEASE) {
                replayKeys_.erase(std::remove(replayKeys_.begin(), replayKeys_.end(), e.a), replayKeys_.end());
            }
            onKey(e.a, e.b, e.c);
            break;
        case 'M':
            if (e.a >= 0 && e.a < 8) replayButtons_[e.a] = e.b != GLFW_RELEASE;
            onMouseButton(e.a, e.b);
            break;
        case 'C': onCursor(e.x, e.y); break;
        case 'W': onScroll(e.x); break;
        default: break;
    }
    replayDispatch_ = false;
}

#else  // headless-only build: no window, no interactive session to record or replay

void App::inputKey(int, int, int) {}
void App::inputButton(int, int) {}
void App::inputCursor(double, double) {}
void App::inputScroll(double) {}
void App::windowSize(int& w, int& h) const { w = h = 1; }
void App::framebufferSize(int& w, int& h) const { w = h = 0; }
void App::toggleSessionClip() {}
void App::drawSessionIndicator(int, int) {}
bool App::replayBeginFrame(double&) { return false; }
void App::replayDeliverPoll() {}
void App::replayDeliver(const SessionEntry&) {}

#endif

}  // namespace ts
