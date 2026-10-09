#include "scene/GameMenu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace ts {

bool GameSettings::operator==(const GameSettings& o) const {
    return std::fabs(mouseSensitivity - o.mouseSensitivity) < 0.005f && invertY == o.invertY && std::fabs(fovDeg - o.fovDeg) < 0.05f &&
           vsync == o.vsync && std::fabs(fpsLimit - o.fpsLimit) < 0.05f && headBob == o.headBob && quality == o.quality;
}

const std::vector<float>& frameLimitChoices() {
    static const std::vector<float> k = {-1.0f, 30.0f, 60.0f, 90.0f, 120.0f, 144.0f, 165.0f, 240.0f, 0.0f};
    return k;
}

namespace {

const char* kQualityWords[4] = {"low", "high", "ultra", "epic"};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool parseFloat(const std::string& v, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(d)) return false;
    out = static_cast<float>(d);
    return true;
}

bool parseBool(const std::string& v, bool& out) {
    const std::string l = lower(v);
    if (l == "1" || l == "on" || l == "true" || l == "yes") { out = true; return true; }
    if (l == "0" || l == "off" || l == "false" || l == "no") { out = false; return true; }
    return false;
}

float snapSens(float v) { return std::round(clampf(v, kSensMin, kSensMax) * 100.0f) / 100.0f; }

}  // namespace

std::string serializeSettings(const GameSettings& s) {
    char buf[1024];
    char limit[32], quality[16];
    if (s.fpsLimit < 0.0f) std::snprintf(limit, sizeof(limit), "auto");
    else if (s.fpsLimit == 0.0f) std::snprintf(limit, sizeof(limit), "uncapped");
    else std::snprintf(limit, sizeof(limit), "%.0f", s.fpsLimit);
    std::snprintf(quality, sizeof(quality), "%s", s.quality >= 0 && s.quality < 4 ? kQualityWords[s.quality] : "auto");
    std::snprintf(buf, sizeof(buf),
                  "# ThermalSense settings (written by the pause menu: Esc -> SETTINGS). key = value; delete the file for the defaults.\n"
                  "mouse_sensitivity = %.2f\n"
                  "invert_y = %d\n"
                  "fov = %.0f\n"
                  "vsync = %d\n"
                  "fps_limit = %s\n"
                  "head_bob = %d\n"
                  "quality = %s\n",
                  s.mouseSensitivity, s.invertY ? 1 : 0, s.fovDeg, s.vsync ? 1 : 0, limit, s.headBob ? 1 : 0, quality);
    return buf;
}

GameSettings parseSettings(const std::string& text, std::vector<std::string>* warnings) {
    GameSettings s;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    auto warn = [&](const char* what, const std::string& l) {
        if (!warnings) return;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "line %d: %s (%s)", n, what, l.substr(0, 120).c_str());
        warnings->push_back(buf);
    };
    while (std::getline(in, line)) {
        ++n;
        const size_t hash = line.find('#');
        const std::string body = trim(hash == std::string::npos ? line : line.substr(0, hash));
        if (body.empty()) continue;
        const size_t eq = body.find('=');
        if (eq == std::string::npos) {
            warn("no '='", body);
            continue;
        }
        const std::string key = lower(trim(body.substr(0, eq))), value = lower(trim(body.substr(eq + 1)));
        float f = 0.0f;
        bool b = false;
        if (key == "mouse_sensitivity") {
            if (parseFloat(value, f)) s.mouseSensitivity = snapSens(f);
            else warn("bad number", body);
        } else if (key == "invert_y") {
            if (parseBool(value, b)) s.invertY = b;
            else warn("bad on / off", body);
        } else if (key == "fov") {
            if (parseFloat(value, f)) s.fovDeg = std::round(clampf(f, kFovMin, kFovMax));
            else warn("bad number", body);
        } else if (key == "vsync") {
            if (parseBool(value, b)) s.vsync = b;
            else warn("bad on / off", body);
        } else if (key == "fps_limit") {
            if (value == "auto" || value == "display") s.fpsLimit = -1.0f;
            else if (value == "uncapped" || value == "off") s.fpsLimit = 0.0f;
            else if (parseFloat(value, f)) s.fpsLimit = f < 0.0f ? -1.0f : (f == 0.0f ? 0.0f : clampf(std::round(f), 15.0f, 1000.0f));
            else warn("bad frame limit", body);
        } else if (key == "head_bob") {
            if (parseBool(value, b)) s.headBob = b;
            else warn("bad on / off", body);
        } else if (key == "quality") {
            int q = -2;
            if (value == "auto") q = -1;
            for (int i = 0; i < 4; ++i)
                if (value == kQualityWords[i]) q = i;
            if (q == -2 && parseFloat(value, f) && f >= -1.0f && f <= 3.0f && f == std::floor(f)) q = static_cast<int>(f);
            if (q >= -1) s.quality = q;
            else warn("bad quality", body);
        } else {
            warn("unknown key", body);
        }
    }
    return s;
}

void applyLookSettings(const GameSettings& s, float& dx, float& dy) {
    dx *= s.mouseSensitivity;
    dy *= s.mouseSensitivity * (s.invertY ? -1.0f : 1.0f);
}

const char* settingLabel(SettingId id) {
    switch (id) {
        case SettingId::Sensitivity: return "MOUSE SENSITIVITY";
        case SettingId::InvertY: return "INVERT Y";
        case SettingId::Fov: return "FIELD OF VIEW";
        case SettingId::Vsync: return "VSYNC";
        case SettingId::FrameLimit: return "FRAME LIMIT";
        case SettingId::HeadBob: return "HEAD BOB";
        case SettingId::Quality: return "QUALITY";
        case SettingId::Count: break;
    }
    return "?";
}

const char* settingHint(SettingId id) {
    switch (id) {
        case SettingId::Sensitivity: return "every mouse-look path: soldier, third-person arm, vehicles, ride-along, free camera";
        case SettingId::InvertY: return "mouse up looks down";
        case SettingId::Fov: return "on foot, vertical (first person; third person 3 deg narrower)";
        case SettingId::Vsync: return "swap with the display; the frame limiter is then off";
        case SettingId::FrameLimit: return "frame limiter when vsync is off (AUTO = the display refresh)";
        case SettingId::HeadBob: return "camera bob while walking (soldier camera)";
        case SettingId::Quality: return "render preset (AUTO = from the GPU)";
        case SettingId::Count: break;
    }
    return "";
}

std::string settingValueText(const GameSettings& s, SettingId id) {
    char buf[48];
    switch (id) {
        case SettingId::Sensitivity: std::snprintf(buf, sizeof(buf), "%.2fx", s.mouseSensitivity); return buf;
        case SettingId::InvertY: return s.invertY ? "ON" : "OFF";
        case SettingId::Fov: std::snprintf(buf, sizeof(buf), "%.0f DEG", s.fovDeg); return buf;
        case SettingId::Vsync: return s.vsync ? "ON" : "OFF";
        case SettingId::FrameLimit:
            if (s.fpsLimit < 0.0f) return "AUTO (DISPLAY)";
            if (s.fpsLimit == 0.0f) return "UNCAPPED";
            std::snprintf(buf, sizeof(buf), "%.0f FPS", s.fpsLimit);
            return buf;
        case SettingId::HeadBob: return s.headBob ? "ON" : "OFF";
        case SettingId::Quality: {
            static const char* k[5] = {"AUTO", "LOW", "HIGH", "ULTRA", "EPIC"};
            return k[std::clamp(s.quality, -1, 3) + 1];
        }
        case SettingId::Count: break;
    }
    return "";
}

void adjustSetting(GameSettings& s, SettingId id, int dir) {
    dir = dir < 0 ? -1 : 1;
    switch (id) {
        case SettingId::Sensitivity: s.mouseSensitivity = snapSens(s.mouseSensitivity + kSensStep * static_cast<float>(dir)); break;
        case SettingId::InvertY: s.invertY = !s.invertY; break;
        case SettingId::Fov: s.fovDeg = clampf(s.fovDeg + kFovStep * static_cast<float>(dir), kFovMin, kFovMax); break;
        case SettingId::Vsync: s.vsync = !s.vsync; break;
        case SettingId::FrameLimit: {
            const std::vector<float>& c = frameLimitChoices();
            int at = 0;
            float bestD = 1e9f;
            for (size_t i = 0; i < c.size(); ++i)
                if (std::fabs(c[i] - s.fpsLimit) < bestD) {
                    bestD = std::fabs(c[i] - s.fpsLimit);
                    at = static_cast<int>(i);
                }
            at = std::clamp(at + dir, 0, static_cast<int>(c.size()) - 1);
            s.fpsLimit = c[static_cast<size_t>(at)];
            break;
        }
        case SettingId::HeadBob: s.headBob = !s.headBob; break;
        case SettingId::Quality: s.quality = std::clamp(s.quality + dir, -1, 3); break;
        case SettingId::Count: break;
    }
}

const char* menuTabLabel(MenuTab t) {
    switch (t) {
        case MenuTab::Resume: return "RESUME";
        case MenuTab::Travel: return "TRAVEL";
        case MenuTab::Time: return "TIME";
        case MenuTab::View: return "VIEW";
        case MenuTab::Settings: return "SETTINGS";
        case MenuTab::Quit: return "QUIT";
        case MenuTab::Count: break;
    }
    return "?";
}

bool menuTabIsPage(MenuTab t) { return t == MenuTab::Travel || t == MenuTab::Time || t == MenuTab::View || t == MenuTab::Settings; }

MenuTab nextMenuPage(MenuTab t, int dir) {
    const MenuTab pages[4] = {MenuTab::Travel, MenuTab::Time, MenuTab::View, MenuTab::Settings};
    int at = 0;
    for (int i = 0; i < 4; ++i)
        if (pages[i] == t) at = i;
    return pages[((at + (dir < 0 ? -1 : 1)) % 4 + 4) % 4];
}

GuideSection menuTabSection(MenuTab t) {
    if (t == MenuTab::Time) return GuideSection::Time;
    if (t == MenuTab::View) return GuideSection::View;
    return GuideSection::Travel;
}

PauseMenuLayout pauseMenuLayout(int w, int h) {
    PauseMenuLayout l;
    l.guide = guideLayout(w, h);
    l.s = l.guide.s;
    l.page = l.guide.panel;
    const float s = l.s, ox = l.page.x, oy = l.page.y;
    auto R = [&](float x, float y, float rw, float rh) { return UiRect{ox + x * s, oy + y * s, rw * s, rh * s}; };
    // Tab bar: six equal tabs over the panel width, in the margin above it (the guide panel leaves 107 design units).
    const float gap = 8.0f, tw = (1260.0f - 5.0f * gap) / 6.0f;
    for (int i = 0; i < kMenuTabCount; ++i) l.tabs[i] = R(static_cast<float>(i) * (tw + gap), -58.0f, tw, 46.0f);
    l.settingsTitle = R(24, 16, 900, 40);
    for (int i = 0; i < kSettingCount; ++i) {
        const float y = 86.0f + static_cast<float>(i) * 56.0f;
        SettingRowRect r;
        r.id = static_cast<SettingId>(i);
        r.row = R(24, y, 1212, 50);
        r.minus = R(760, y + 5, 52, 40);
        r.value = R(820, y + 5, 356, 40);
        r.plus = R(1184, y + 5, 52, 40);
        l.rows.push_back(r);
    }
    l.settingsFooter = R(24, 492, 1212, 80);
    return l;
}

int menuTabAt(const PauseMenuLayout& l, float x, float y) {
    for (int i = 0; i < kMenuTabCount; ++i)
        if (l.tabs[i].contains(x, y)) return i;
    return -1;
}

SettingHit settingAt(const PauseMenuLayout& l, float x, float y) {
    SettingHit h;
    for (size_t i = 0; i < l.rows.size(); ++i) {
        const SettingRowRect& r = l.rows[i];
        if (!r.row.contains(x, y)) continue;
        h.row = static_cast<int>(i);
        if (r.minus.contains(x, y)) h.dir = -1;
        else if (r.plus.contains(x, y) || r.value.contains(x, y)) h.dir = 1;
        return h;
    }
    return h;
}

}  // namespace ts
