#include "scene/HudMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ts {

MarkerProjection projectMarker(const Mat4& viewProj, const Vec3& camPos, const Vec3& p, int w, int h) {
    MarkerProjection m;
    const Vec4 c = viewProj * Vec4{p.x, p.y, p.z, 1.0f};
    m.dist = length(p - camPos);
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    if (std::fabs(c.w) < 1e-5f) {
        m.x = W * 0.5f;
        m.y = H * 0.5f;
        return m;
    }
    // Behind the camera x / w and y / w are mirrored through the screen centre; keep that (edgeArrow flips it back).
    m.front = c.w > 0.01f;
    m.x = (c.x / c.w * 0.5f + 0.5f) * W;
    m.y = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * H;
    m.onScreen = m.front && m.x >= 0.0f && m.x <= W && m.y >= 0.0f && m.y <= H;
    return m;
}

float markerHalfSize(float dist, int h, float worldSize, float fovYDeg) {
    const float s = static_cast<float>(h) / 900.0f;
    const float f = 0.5f * static_cast<float>(h) / std::tan(radians(fovYDeg * 0.5f));  // pixels per (metre / metre)
    const float px = worldSize * f / std::max(dist, 0.5f);
    return clampf(px, 10.0f * s, 120.0f * s);
}

EdgeArrow edgeArrow(float sx, float sy, bool front, int w, int h, float margin) {
    const float cx = static_cast<float>(w) * 0.5f, cy = static_cast<float>(h) * 0.5f;
    float dx = sx - cx, dy = sy - cy;
    if (!front) {  // mirrored by the perspective divide: the real direction is the opposite one
        dx = -dx;
        dy = -dy;
    }
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) {  // straight behind / ahead: point down the screen
        dx = 0.0f;
        dy = 1.0f;
        len = 1.0f;
    }
    dx /= len;
    dy /= len;
    const float hx = std::max(cx - margin, 1.0f), hy = std::max(cy - margin, 1.0f);
    // Scale the direction until it touches the inset rectangle.
    const float kx = std::fabs(dx) > 1e-6f ? hx / std::fabs(dx) : 1e9f;
    const float ky = std::fabs(dy) > 1e-6f ? hy / std::fabs(dy) : 1e9f;
    const float k = std::min(kx, ky);
    EdgeArrow a;
    a.x = cx + dx * k;
    a.y = cy + dy * k;
    a.dirX = dx;
    a.dirY = dy;
    return a;
}

float bannerDuration(BannerKind k) {
    switch (k) {
        case BannerKind::Wave: return 2.5f;
        case BannerKind::Spotted: return 1.8f;
        case BannerKind::Kill: return 2.2f;
        case BannerKind::Hit: return 0.35f;
        case BannerKind::Breach: return 2.5f;
        case BannerKind::BaseLost: return 5.0f;
        case BannerKind::Strike: return 1.8f;
        case BannerKind::Dusk:
        case BannerKind::NightFalls:
        case BannerKind::Dawn:
        case BannerKind::DayBreak: return 3.5f;
        case BannerKind::Shift: return 2.5f;
        case BannerKind::Withdraw: return 2.5f;
    }
    return 2.0f;
}

const char* phaseBannerText(BannerKind k) {
    switch (k) {
        case BannerKind::Dusk: return "DUSK";
        case BannerKind::NightFalls: return "NIGHT FALLS";
        case BannerKind::Dawn: return "DAWN";
        case BannerKind::DayBreak: return "DAY BREAK";
        case BannerKind::Shift: return "SHIFT CHANGE";
        case BannerKind::Withdraw: return "INTRUDERS WITHDRAW";
        default: return nullptr;
    }
}

const char* phaseBannerDetail(BannerKind k) {
    switch (k) {
        case BannerKind::Dusk: return "SHIFT CHANGE  //  LAMPS ON";
        case BannerKind::NightFalls: return "SEARCHLIGHTS ON  //  INTRUDER WAVES POSSIBLE";
        case BannerKind::Dawn: return "SURVIVING ROVERS WITHDRAW";
        case BannerKind::DayBreak: return "DAY SHIFT  //  UAVS ON PATROL";
        case BannerKind::Shift: return "SOLDIERS MOVING TO THEIR POSTS";
        case BannerKind::Withdraw: return "ROVER LEAVING THE FIELD";
        default: return nullptr;
    }
}

float bannerAlpha(float age, float duration, float fadeIn, float fadeOut) {
    if (age < 0.0f || age > duration) return 0.0f;
    const float in = fadeIn > 1e-4f ? clampf(age / fadeIn, 0.0f, 1.0f) : 1.0f;
    const float out = fadeOut > 1e-4f ? clampf((duration - age) / fadeOut, 0.0f, 1.0f) : 1.0f;
    return std::min(in, out);
}

float bannerRise(float age, float duration) {
    const float u = clampf(age / std::max(duration, 1e-3f), 0.0f, 1.0f);
    return 1.0f - (1.0f - u) * (1.0f - u);  // ease-out
}

void HudEvents::prune(float time) {
    if (time < lastTime_ - 0.5f) reset();  // the scene was rebuilt
    lastTime_ = time;
    banners_.erase(std::remove_if(banners_.begin(), banners_.end(),
                                  [&](const Banner& b) { return time - b.start > b.duration || b.start > time + 0.5f; }),
                   banners_.end());
}

void HudEvents::consume(const std::vector<GameEvent>& events, float time) {
    prune(time);
    for (const GameEvent& e : events) {
        Banner b;
        b.start = e.time > 0.0f ? e.time : time;
        b.value = e.value;
        b.combined = e.combined;
        b.track = e.track;
        b.position = e.position;
        switch (e.type) {
            case GameEventType::WaveStart:
                b.kind = BannerKind::Wave;
                break;
            case GameEventType::EnemySpotted:
                b.kind = BannerKind::Spotted;
                if (time - lastSpotted_ < 6.0f && time >= lastSpotted_) continue;  // track churn must not spam the banner
                lastSpotted_ = time;
                banners_.erase(std::remove_if(banners_.begin(), banners_.end(),
                                              [](const Banner& o) { return o.kind == BannerKind::Spotted; }),
                               banners_.end());
                break;
            case GameEventType::EnemyDestroyed:
                b.kind = BannerKind::Kill;
                b.hasPosition = true;
                break;
            case GameEventType::EnemyHit:
                b.kind = BannerKind::Hit;
                b.hasPosition = true;
                break;
            case GameEventType::Breach:
                b.kind = BannerKind::Breach;
                b.hasPosition = true;
                edgeFlashStart_ = b.start;
                break;
            case GameEventType::BaseLost:
                b.kind = BannerKind::BaseLost;
                baseLostStart_ = b.start;
                break;
            case GameEventType::CombinedStrike:
                b.kind = BannerKind::Strike;
                break;
            case GameEventType::Dusk:
            case GameEventType::NightFalls:
            case GameEventType::Dawn:
            case GameEventType::DayBreak:
            case GameEventType::ShiftChange: {
                b.kind = e.type == GameEventType::Dusk         ? BannerKind::Dusk
                         : e.type == GameEventType::NightFalls ? BannerKind::NightFalls
                         : e.type == GameEventType::Dawn       ? BannerKind::Dawn
                         : e.type == GameEventType::DayBreak   ? BannerKind::DayBreak
                                                               : BannerKind::Shift;
                // One phase banner at a time: a scrubbed clock replaces the previous one instead of stacking (the
                // shift-change line is separate: it arrives together with DUSK / DAY BREAK).
                const bool shift = b.kind == BannerKind::Shift;
                banners_.erase(std::remove_if(banners_.begin(), banners_.end(),
                                              [shift](const Banner& o) {
                                                  return shift ? o.kind == BannerKind::Shift
                                                               : (o.kind == BannerKind::Dusk || o.kind == BannerKind::NightFalls ||
                                                                  o.kind == BannerKind::Dawn || o.kind == BannerKind::DayBreak);
                                              }),
                               banners_.end());
                break;
            }
            case GameEventType::Withdraw: {
                b.kind = BannerKind::Withdraw;
                b.hasPosition = true;
                b.value = 1;  // rovers withdrawing: merged into the running banner
                for (const Banner& o : banners_)
                    if (o.kind == BannerKind::Withdraw) b.value = o.value + 1;
                banners_.erase(std::remove_if(banners_.begin(), banners_.end(),
                                              [](const Banner& o) { return o.kind == BannerKind::Withdraw; }),
                               banners_.end());
                break;
            }
            default:
                continue;
        }
        b.duration = bannerDuration(b.kind);
        banners_.push_back(b);
    }
}

float HudEvents::breachFlash(float time) const {
    const float age = time - edgeFlashStart_;
    return age < 0.0f || age > 0.9f ? 0.0f : 1.0f - age / 0.9f;
}

float HudEvents::baseLostDim(float time) const {
    const float age = time - baseLostStart_;
    return 0.65f * bannerAlpha(age, bannerDuration(BannerKind::BaseLost), 0.4f, 0.8f);
}

const char* HudEvents::baseLostText(float time) const {
    const float age = time - baseLostStart_;
    if (age < 0.0f || age > bannerDuration(BannerKind::BaseLost)) return nullptr;
    return age < 2.2f ? "BASE LOST" : "RESTARTING";
}

const Banner* HudEvents::latest(BannerKind k) const {
    for (auto it = banners_.rbegin(); it != banners_.rend(); ++it)
        if (it->kind == k) return &*it;
    return nullptr;
}

MiniMapGeom miniMapGeometry(int w, int h) {
    const float s = static_cast<float>(h) / 900.0f;
    MiniMapGeom g;
    g.radius = 90.0f * s;
    g.cx = static_cast<float>(w) - 24.0f * s - g.radius;
    g.cy = static_cast<float>(h) - 40.0f * s - g.radius;
    g.worldRadius = 40.0f;
    return g;
}

bool miniMapToScreen(const MiniMapGeom& g, float wx, float wz, float& sx, float& sy, bool clampToRim) {
    const float k = g.radius / g.worldRadius;
    float dx = wx * k, dy = wz * k;
    const float len = std::sqrt(dx * dx + dy * dy);
    const bool inside = len <= g.radius;
    if (!inside && clampToRim) {
        dx *= g.radius / len;
        dy *= g.radius / len;
    }
    sx = g.cx + dx;
    sy = g.cy + dy;
    return inside;
}

void miniMapHeading(float yawRad, float& dx, float& dy) {
    dx = std::sin(yawRad);
    dy = std::cos(yawRad);
}

void integrityColor(float percent, float& r, float& g, float& b) {
    const float p = clampf(percent, 0.0f, 100.0f);
    const float green[3] = {0.30f, 0.95f, 0.40f}, amber[3] = {1.0f, 0.75f, 0.15f}, red[3] = {1.0f, 0.22f, 0.15f};
    const float* a = p >= 50.0f ? amber : red;
    const float* c = p >= 50.0f ? green : amber;
    const float t = p >= 50.0f ? (p - 50.0f) / 50.0f : clampf(p / 50.0f, 0.0f, 1.0f);
    r = a[0] + (c[0] - a[0]) * t;
    g = a[1] + (c[1] - a[1]) * t;
    b = a[2] + (c[2] - a[2]) * t;
}

SkyGlyph skyGlyph(float sunElevationDeg) {
    if (sunElevationDeg > 6.0f) return SkyGlyph::Sun;
    if (sunElevationDeg >= -6.0f) return SkyGlyph::LowSun;
    return SkyGlyph::Moon;
}

std::string formatClock(float hour) {
    float h = std::fmod(hour, 24.0f);
    if (h < 0.0f) h += 24.0f;
    int minutes = static_cast<int>(std::floor(h * 60.0f + 1e-3f)) % (24 * 60);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

bool parseTimeOfDay(const std::string& v, float& hour, bool& autoCycle) {
    struct Named { const char* name; float hour; };
    static const Named kNamed[] = {{"day", 12.0f}, {"noon", 12.0f}, {"night", 23.0f}, {"dusk", 17.75f}, {"dawn", 6.0f}};
    for (const Named& n : kNamed)
        if (v == n.name) {
            hour = n.hour;
            autoCycle = false;
            return true;
        }
    if (v == "cycle" || v == "auto") {
        hour = -1.0f;  // the mode's default start
        autoCycle = true;
        return true;
    }
    int hh = -1, mm = 0;
    char extra = 0;
    const int n = std::sscanf(v.c_str(), "%d:%d%c", &hh, &mm, &extra);
    if (n == 2 || (n == 1 && v.find(':') == std::string::npos)) {
        if (hh < 0 || hh > 24 || mm < 0 || mm > 59 || (hh == 24 && mm != 0)) return false;
        hour = std::fmod(static_cast<float>(hh) + static_cast<float>(mm) / 60.0f, 24.0f);
        autoCycle = false;
        return true;
    }
    return false;
}

uint32_t mixRunSeed(uint32_t randomDevice, uint64_t clockTicks) {
    uint32_t x = randomDevice ^ static_cast<uint32_t>(clockTicks) ^ static_cast<uint32_t>(clockTicks >> 32);
    // splitmix32-style finaliser so close clock readings give unrelated seeds
    x += 0x9e3779b9u;
    x = (x ^ (x >> 16)) * 0x85ebca6bu;
    x = (x ^ (x >> 13)) * 0xc2b2ae35u;
    x ^= x >> 16;
    return x == 0u ? 1u : x;
}

ScreenBox cvBoxToScreen(int minX, int minY, int maxX, int maxY, int imgW, int imgH, float cvFovYDeg, float viewFovYDeg,
                        int w, int h) {
    // Pixel edges -> normalised image coordinates (u right, v up, [-1, 1]) -> tangent space -> the view's NDC.
    const float k = std::tan(radians(cvFovYDeg * 0.5f)) / std::tan(radians(viewFovYDeg * 0.5f));
    const float aspectImg = static_cast<float>(imgW) / static_cast<float>(std::max(imgH, 1));
    const float aspectView = static_cast<float>(w) / static_cast<float>(std::max(h, 1));
    const auto sx = [&](float px) {
        const float u = px / static_cast<float>(imgW) * 2.0f - 1.0f;
        return (u * k * aspectImg / aspectView * 0.5f + 0.5f) * static_cast<float>(w);
    };
    const auto sy = [&](float py) {
        const float v = 1.0f - py / static_cast<float>(imgH) * 2.0f;
        return (1.0f - (v * k * 0.5f + 0.5f)) * static_cast<float>(h);
    };
    ScreenBox b;
    b.x0 = sx(static_cast<float>(minX));
    b.x1 = sx(static_cast<float>(maxX + 1));
    b.y0 = sy(static_cast<float>(minY));
    b.y1 = sy(static_cast<float>(maxY + 1));
    return b;
}

const std::vector<HelpLine>& helpLines() {
    // v9 M2: the unified game scheme (E use, Tab unit, V view, Esc menu, P pause, F3 HUD). Every key below is in the
    // binding table of scene/KeyMap.cpp (checked for conflicts by T341).
    static const std::vector<HelpLine> kLines = {
        {0, "PLAY"},
        {0, "E  use: door, barracks, take control, ride, release"},
        {0, "TAB / SHIFT+TAB  next / previous unit (soldiers, UAVs, vehicles)"},
        {0, "V  first / third person; vehicle chase / sight / alt"},
        {0, "ESC  release -> clear selection -> PAUSE MENU (quit there)"},
        {0, "P (or PAUSE)  pause     F3  HUD: game / debug / off"},
        {0, "CLICK select   DOUBLE-CLICK / ENTER take control"},
        {0, "SOLDIER: W A S D move   SHIFT sprint   SPACE jump   ALT crouch"},
        {0, "SOLDIER: MOUSE look   LMB / L-CTRL fire   RMB aim"},
        {0, "VEHICLE: W A S D drive   PGUP PGDN lift   ARROWS aim"},
        {0, "VEHICLE: ENTER / L-CTRL fire   \\ action   SHIFT boost"},
        {0, "RIDING ALONG: E or W A S D take control"},
        {0, "5 6 7 8 9 0  tank, heli, AA gun, radar, doors, UAV-1"},
        {0, "[ ]  every controllable   BACKSPACE release / give rover back"},
        {0, "MENU: TRAVEL, TIME, VIEW (field guide), SETTINGS"},
        {0, "SETTINGS: sensitivity, invert Y, FOV, vsync, frame limit"},
        {0, "SETTINGS: head bob, quality  -> ThermalSense.ini"},
        {1, "VIEW, CAMERA & GRAPHICS LAB"},
        {1, "C  camera: orbit, free, chase, UAV, walk (nothing controlled)"},
        {1, "MOUSE DRAG orbit / look   WHEEL zoom   W A S D free / walk"},
        {1, "F  view: visible, primitives, z-buffer, thermal"},
        {1, "O  shading: per-object, flat, Gouraud, Phong"},
        {1, "G explode   X edges   Z depth test   I indexed   H axes"},
        {1, "THERMAL: Y first-person  T palette  WHEEL zoom  Q NUC"},
        {1, "1 2 3 4  moon / beacon / searchlight / practicals"},
        {1, "F5 quality   F6 TAA  F7 SSR  F8 flare  F9 exposure  F10 DoF"},
        {1, "L foliage   J fireflies   R rain   B PBR   N v1   + - exp."},
        {1, "M flood field   U UAV feed   K cinematic   F12 screenshot"},
        {1, "TIME: F2 auto / manual   , . scrub   / next phase"},
        {1, "F4 POV list   MIDDLE CLICK jump in   INS / DEL next / prev"},
        {1, "HOME free fly camera (WHEEL speed, SHIFT x3)   END back"},
        {1, "F11 game mode: compass, FOV kick, letterbox jumps"},
        {1, "; render: raster, hybrid RT, Whitted, path / light"},
        {1, "' ray debugger: click a pixel -> its ray tree"},
        {1, "` Frame Explorer: freeze the frame, 16 stages"},
    };
    return kLines;
}

const std::vector<KeyBinding>& keyBindingsV6() {
    static const std::vector<KeyBinding> kKeys = {
        {"F2", "day/night cycle: automatic <-> manual clock", "v6"},
        {", .", "manual clock: scrub the time of day back / forward (held, 2 h/s)", "v6"},
        {"/", "jump to the next phase (dawn, day, dusk, night)", "v6"},
        {"Tab", "take control of the next unit, soldiers first (v6 had F3 = next soldier; v9 M2: F3 = HUD level)", "v6"},
        {"W A S D, Shift", "possessed soldier: walk / turn, run", "v6"},
        {"\\", "possessed soldier: stand / crouch / wave; SKY clock: auto / manual", "v6"},
        {"W S, Enter", "possessed SKY clock: scrub the time, jump to the next phase", "v6"},
    };
    return kKeys;
}

const std::vector<KeyBinding>& keyBindingsV7() {
    static const std::vector<KeyBinding> kKeys = {
        {"F4", "POV picker: every moving object by distance (arrows + Enter or click; Esc closes)", "v7"},
        {"Insert / Delete", "jump to the next / previous moving object's POV (spring-blended camera)", "v7"},
        {"middle mouse", "jump into the object under the cursor (ray pick; the crosshair in game mode)", "v7"},
        {"Home / End", "free fly camera from the current view (wheel = speed, Shift x3) / back to the previous view", "v7"},
        {"V (riding along)", "first person -> chase -> alt camera (tank commander, UAV belly, rover mast; C before v9 M2)", "v7"},
        {"E or W A S D / arrows (riding along)", "take control: possess the object or take the intruder rover's wheel", "v7"},
        {"Backspace (rover)", "give the rover back to its planner, keep riding along", "v7"},
        {"Left Ctrl / Left Alt (soldier)", "fire the rifle (tracers) / crouch - stand", "v7"},
        {"F11", "game mode: game HUD only, crosshair, hit marker, compass strip, FOV kick, letterbox jumps, mouse look", "v7"},
        {";", "render mode: raster -> hybrid RT (traced shadows + reflections) -> Whitted RT -> path tracing -> light tracing (reverse)", "v7"},
        {"'", "ray debugger on / off; left click traces that pixel and draws its ray tree (primary, shadow, reflection, refraction) in 3D", "v7"},
        {"`", "Frame Explorer: freeze this frame and step through its 16 stages (` or Esc closes)", "v7"},
    };
    return kKeys;
}

}  // namespace ts

// ---- v7.1 WP-U: field guide, both UAV feeds, context prompts --------------------------------------------------------------
namespace ts {

const char* guideActionName(GuideAction a) {
    switch (a) {
        case GuideAction::None: return "NONE";
        case GuideAction::RideUav1: return "RIDE_UAV1";
        case GuideAction::RideUav2: return "RIDE_UAV2";
        case GuideAction::SoldierFirst: return "SOLDIER_FIRST";
        case GuideAction::SoldierThird: return "SOLDIER_THIRD";
        case GuideAction::PickPov: return "PICK_POV";
        case GuideAction::FreeCam: return "FREE_CAM";
        case GuideAction::Tank: return "TANK";
        case GuideAction::Helicopter: return "HELICOPTER";
        case GuideAction::Rover: return "ROVER";
        case GuideAction::Dawn: return "DAWN";
        case GuideAction::Noon: return "NOON";
        case GuideAction::Dusk: return "DUSK";
        case GuideAction::Midnight: return "MIDNIGHT";
        case GuideAction::TimeSlider: return "TIME_SLIDER";
        case GuideAction::CycleAuto: return "CYCLE_AUTO";
        case GuideAction::CycleSpeed: return "CYCLE_SPEED";
        case GuideAction::DayOps: return "DAY_OPS";
        case GuideAction::PersonToggle: return "PERSON_TOGGLE";
        case GuideAction::RenderMode: return "RENDER_MODE";
        case GuideAction::FrameExplorer: return "FRAME_EXPLORER";
        case GuideAction::Thermal: return "THERMAL";
        case GuideAction::HudToggle: return "HUD";
        case GuideAction::Feeds: return "UAV_FEEDS";
        case GuideAction::Close: return "CLOSE";
    }
    return "?";
}

namespace {
struct GuideKey { GuideAction a; const char* key; int code; };
const GuideKey kGuideKeys[] = {
    {GuideAction::RideUav1, "1", '1'},      {GuideAction::RideUav2, "2", '2'},     {GuideAction::SoldierFirst, "3", '3'},
    {GuideAction::SoldierThird, "4", '4'},  {GuideAction::PickPov, "5", '5'},      {GuideAction::FreeCam, "6", '6'},
    {GuideAction::Tank, "7", '7'},          {GuideAction::Helicopter, "8", '8'},   {GuideAction::Rover, "9", '9'},
    {GuideAction::Dawn, "Q", 'Q'},          {GuideAction::Noon, "W", 'W'},         {GuideAction::Dusk, "E", 'E'},
    {GuideAction::Midnight, "R", 'R'},      {GuideAction::CycleAuto, "T", 'T'},    {GuideAction::CycleSpeed, "Y", 'Y'},
    {GuideAction::DayOps, "O", 'O'},
    {GuideAction::PersonToggle, "A", 'A'},  {GuideAction::RenderMode, "S", 'S'},   {GuideAction::FrameExplorer, "D", 'D'},
    {GuideAction::Thermal, "F", 'F'},       {GuideAction::HudToggle, "H", 'H'},    {GuideAction::Feeds, "U", 'U'},
    {GuideAction::Close, "G", 'G'},
};
}  // namespace

const char* guideActionKey(GuideAction a) {
    for (const GuideKey& k : kGuideKeys)
        if (k.a == a) return k.key;
    return "";
}

GuideAction guideKeyAction(int key) {
    for (const GuideKey& k : kGuideKeys)
        if (k.code == key) return k.a;
    return GuideAction::None;
}

float guidePresetHour(GuideAction a) {
    switch (a) {
        case GuideAction::Dawn: return 6.0f;
        case GuideAction::Noon: return 12.0f;
        case GuideAction::Dusk: return 17.75f;
        case GuideAction::Midnight: return 0.0f;
        default: return -1.0f;
    }
}

// Design canvas 1440 x 810 (16:9); the panel is 1260 x 596 design units, centred.
GuideLayout guideLayout(int w, int h) {
    GuideLayout l;
    const float W = static_cast<float>(std::max(w, 1)), H = static_cast<float>(std::max(h, 1));
    const float s = std::min(W / 1440.0f, H / 810.0f);
    l.s = s;
    const float pw = 1260.0f, ph = 596.0f;
    const float ox = std::floor((W - pw * s) * 0.5f), oy = std::floor((H - ph * s) * 0.5f);
    auto R = [&](float x, float y, float rw, float rh) { return UiRect{ox + x * s, oy + y * s, rw * s, rh * s}; };
    l.panel = R(0, 0, pw, ph);
    l.header = R(0, 0, pw, 72);
    l.close = R(pw - 24 - 40, 16, 40, 40);
    auto add = [&](GuideAction a, GuideSection sec, UiRect r, const char* label, const char* sub = "") {
        GuideButton b;
        b.action = a;
        b.section = sec;
        b.r = r;
        b.label = label;
        b.sub = sub;
        l.buttons.push_back(b);
    };
    // TRAVEL: 3 x 3 tiles (228 x 100, gap 12) in the left column (x 24, width 708).
    l.travelBox = R(24, 88, 708, 358);
    struct T { GuideAction a; const char* label; const char* sub; };
    const T travel[9] = {
        {GuideAction::RideUav1, "UAV-1 CAMERA", "RIDE THE LEAD GIMBAL"},
        {GuideAction::RideUav2, "UAV-2 CAMERA", "RIDE THE WINGMAN"},
        {GuideAction::SoldierFirst, "SOLDIER 1ST", "RANDOM, FIRST PERSON"},
        {GuideAction::SoldierThird, "SOLDIER 3RD", "RANDOM, OVER SHOULDER"},
        {GuideAction::PickPov, "ANY POV", "EVERY MOVING OBJECT"},
        {GuideAction::FreeCam, "FREE CAMERA", "FLY ANYWHERE"},
        {GuideAction::Tank, "TANK", "DRIVE, CHASE VIEW"},
        {GuideAction::Helicopter, "HELICOPTER", "FLY, CHASE VIEW"},
        {GuideAction::Rover, "INTRUDER ROVER", "SNEAK PAST THE UAVS"},
    };
    for (int i = 0; i < 9; ++i)
        add(travel[i].a, GuideSection::Travel, R(24 + static_cast<float>(i % 3) * 240, 122 + static_cast<float>(i / 3) * 112, 228, 100),
            travel[i].label, travel[i].sub);
    // HELP under the tiles.
    l.helpBox = R(24, 462, 708, 70);
    // TIME (right column x 760, width 476): clock, slider, presets, auto / speed.
    l.timeBox = R(760, 88, 476, 254);
    l.clock = R(760, 122, 476, 52);
    l.slider = R(774, 192, 448, 10);
    l.sliderHit = R(760, 180, 476, 46);
    const T presets[4] = {{GuideAction::Dawn, "DAWN", "06:00"}, {GuideAction::Noon, "NOON", "12:00"},
                          {GuideAction::Dusk, "DUSK", "17:45"}, {GuideAction::Midnight, "MIDNIGHT", "00:00"}};
    const float bw = (476.0f - 3.0f * 8.0f) / 4.0f;
    for (int i = 0; i < 4; ++i)
        add(presets[i].a, GuideSection::Time, R(760 + static_cast<float>(i) * (bw + 8), 234, bw, 46), presets[i].label, presets[i].sub);
    const float tw = (476.0f - 2.0f * 8.0f) / 3.0f;   // auto / speed / day ops (v9 M5: daytime enemies)
    add(GuideAction::CycleAuto, GuideSection::Time, R(760, 290, tw, 42), "AUTO CYCLE");
    add(GuideAction::CycleSpeed, GuideSection::Time, R(760 + tw + 8, 290, tw, 42), "SPEED");
    add(GuideAction::DayOps, GuideSection::Time, R(760 + 2 * (tw + 8), 290, tw, 42), "DAY OPS");
    // VIEW: 2 x 3 buttons.
    l.viewBox = R(760, 358, 476, 174);
    const T view[6] = {{GuideAction::PersonToggle, "1ST / 3RD PERSON", ""}, {GuideAction::RenderMode, "RENDER", ""},
                       {GuideAction::FrameExplorer, "FRAME EXPLORER", ""}, {GuideAction::Thermal, "THERMAL VIEW", ""},
                       {GuideAction::HudToggle, "HUD", ""},                 {GuideAction::Feeds, "UAV FEEDS", ""}};
    for (int i = 0; i < 6; ++i)
        add(view[i].a, GuideSection::View, R(760 + static_cast<float>(i % 2) * 242, 392 + static_cast<float>(i / 2) * 48, 234, 40),
            view[i].label);
    return l;
}

int guideButtonAt(const GuideLayout& l, float x, float y) {
    for (size_t i = 0; i < l.buttons.size(); ++i)
        if (l.buttons[i].r.contains(x, y)) return static_cast<int>(i);
    return -1;
}

GuideAction guideActionAt(const GuideLayout& l, float x, float y) {
    if (!l.panel.contains(x, y)) return GuideAction::None;
    if (l.close.contains(x, y)) return GuideAction::Close;
    const int i = guideButtonAt(l, x, y);
    if (i >= 0) return l.buttons[i].action;
    if (l.sliderHit.contains(x, y)) return GuideAction::TimeSlider;
    return GuideAction::None;
}

float guideSliderHour(const GuideLayout& l, float x) {
    const float u = clampf((x - l.slider.x) / std::max(l.slider.w, 1.0f), 0.0f, 1.0f);
    return std::min(u * 24.0f, 24.0f - 1.0f / 60.0f);
}

float guideSliderX(const GuideLayout& l, float hour) {
    float hh = std::fmod(hour, 24.0f);
    if (hh < 0.0f) hh += 24.0f;
    return l.slider.x + l.slider.w * hh / 24.0f;
}

float guideSpeedFactor(int index) {
    static const float k[kGuideSpeedCount] = {1.0f, 2.0f, 4.0f, 8.0f, 0.5f};
    const int i = ((index % kGuideSpeedCount) + kGuideSpeedCount) % kGuideSpeedCount;
    return k[i];
}

float guideHintAlpha(float t, float openedAt) {
    float a = bannerAlpha(t - 1.5f, 12.5f, 0.6f, 1.2f);
    if (openedAt >= 0.0f) a = std::min(a, clampf(1.0f - (t - openedAt) / 0.4f, 0.0f, 1.0f));
    return a;
}

HudLayout hudLayout(int w, int h, bool feeds, bool gameMode) {
    HudLayout l;
    const float W = static_cast<float>(std::max(w, 1)), H = static_cast<float>(std::max(h, 1));
    const float s = H / 900.0f;
    l.s = s;
    l.topBar = {(W - 700.0f * s) * 0.5f, 12.0f * s, 700.0f * s, 66.0f * s};
    if (gameMode) l.compass = {W * 0.5f - 280.0f * s, 78.0f * s, 560.0f * s, 90.0f * s};
    // Default HUD corner blocks (game mode: the POV / control panel bottom left).
    if (!gameMode) {
        l.cornerTop = {10.0f * s, 10.0f * s, 485.0f * s, 104.0f * s};
        l.cornerBottom = {10.0f * s, H - 76.0f * s, 520.0f * s, 70.0f * s};
    } else {
        l.cornerBottom = {14.0f * s, H - 70.0f * s, 520.0f * s, 56.0f * s};
    }
    const MiniMapGeom g = miniMapGeometry(w, h);
    l.miniMap = {g.cx - g.radius - 6.0f * s, g.cy - g.radius - 20.0f * s, 2.0f * g.radius + 12.0f * s, 2.0f * g.radius + 26.0f * s};
    l.clock = {g.cx - g.radius - 6.0f * s, g.cy - g.radius - 96.0f * s, 192.0f * s, 84.0f * s};
    // Feeds: two 256 x 236 cards stacked top right; below the top bar (and the compass) when the screen is too narrow.
    const float cw = 256.0f * s, ch = 236.0f * s, gap = 10.0f * s;
    float fx = W - 20.0f * s - cw, fy = 14.0f * s;
    UiRect probe{fx, fy, cw, 2.0f * ch + gap};
    if (probe.overlaps(l.topBar) || (gameMode && probe.overlaps(l.compass)))
        fy = (gameMode ? l.compass.bottom() : l.topBar.bottom()) + 10.0f * s;
    // Shrink the cards if they would reach the clock panel (very narrow / tall screens).
    float k = 1.0f;
    const float room = l.clock.y - 10.0f * s - fy;
    if (2.0f * ch + gap > room) k = std::max(0.4f, room / (2.0f * ch + gap));
    const float kw = cw * k, kh = ch * k;
    fx = W - 20.0f * s - kw;
    if (feeds) {
        for (int i = 0; i < 2; ++i) {
            l.feed[i] = {fx, fy + static_cast<float>(i) * (kh + gap), kw, kh};
            l.feedImage[i] = {fx + 6.0f * s * k, l.feed[i].y + 28.0f * s * k, 244.0f * s * k, 183.0f * s * k};
        }
        l.combatFeed = {fx - 14.0f * s - 120.0f * s, fy, 120.0f * s, 90.0f * s};
    } else {
        l.combatFeed = {W - 24.0f * s - 120.0f * s, 24.0f * s, 120.0f * s, 90.0f * s};
    }
    // Thermal palette scale: right edge, or left of the feeds.
    const float bx = feeds ? fx - 40.0f * s : W - 44.0f * s, by = H * 0.25f, bh = H * 0.28f;
    l.paletteBar = {bx - 60.0f * s, by - 40.0f * s, 74.0f * s, bh + 66.0f * s};
    // Prompts: the strip under the mini-map, right of the bottom-left block.
    l.prompts = {545.0f * s, H - 26.0f * s, W - 561.0f * s, 22.0f * s};
    return l;
}

std::string uavFeedTitle(int uav, const char* dutyState, bool airborne) {
    char buf[64];
    const char* duty = dutyState && *dutyState ? dutyState : (airborne ? "PATROL" : "DOCKED");
    if (!airborne) std::snprintf(buf, sizeof(buf), "UAV-%d  %s  //  DAY SHIFT", uav + 1, duty);
    else std::snprintf(buf, sizeof(buf), "UAV-%d  %s", uav + 1, duty);
    return buf;
}

std::vector<PromptChip> contextPrompts(PromptMode m, bool thirdPerson) {
    const char* person = thirdPerson ? "FIRST PERSON" : "THIRD PERSON";
    switch (m) {
        case PromptMode::Overview:
            return {{"G", "GUIDE"}, {"F4", "JUMP TO ANY POV"}, {"F3", "BE A SOLDIER"}, {"HOME", "FREE CAMERA"}, {"U", "UAV FEEDS"}};
        case PromptMode::Walk:
            return {{"W A S D", "WALK"}, {"SHIFT", "RUN"}, {"ENTER", "DOOR"}, {"G", "GUIDE"}, {"F4", "ANY POV"}};
        case PromptMode::FreeCam:
            return {{"W A S D", "FLY"}, {"PGUP PGDN", "UP / DOWN"}, {"WHEEL", "SPEED"}, {"G", "GUIDE"}, {"END", "BACK"}};
        case PromptMode::RideSoldier:
            return {{"W A S D", "TAKE CONTROL"}, {"V", person}, {"G", "GUIDE"}, {"F4", "ANY POV"}, {"END", "BACK"}};
        case PromptMode::RideVehicle:
            return {{"W A S D", "TAKE CONTROL"}, {"C", "VIEW"}, {"G", "GUIDE"}, {"F4", "ANY POV"}, {"END", "BACK"}};
        case PromptMode::RideUav:
            return {{"INS", "NEXT POV"}, {"C", "GIMBAL / BELLY / CHASE"}, {"G", "GUIDE"}, {"F4", "ANY POV"}, {"END", "BACK"}};
        case PromptMode::RideRound:
            return {{"G", "GUIDE"}, {"F4", "ANY POV"}, {"END", "BACK"}};
        case PromptMode::Soldier:
            return {{"W A S D", "MOVE"}, {"V", person}, {"L-CTRL", "FIRE"}, {"SHIFT", "RUN"}, {"G", "GUIDE"},
                    {"L-ALT", "CROUCH"}, {"BKSP", "LEAVE"}};
        case PromptMode::Vehicle:
            return {{"W A S D", "DRIVE"}, {"ARROWS", "AIM"}, {"ENTER", "FIRE"}, {"C", "VIEW"}, {"G", "GUIDE"}, {"BKSP", "LEAVE"}};
        case PromptMode::Rover:
            return {{"W S", "THROTTLE"}, {"A D", "STEER"}, {"SHIFT", "BOOST"}, {"G", "GUIDE"}, {"BKSP", "LET GO"}};
        case PromptMode::ThermalFpv:
            return {{"Y", "EXTERNAL VIEW"}, {"T", "PALETTE"}, {"WHEEL", "ZOOM"}, {"G", "GUIDE"}, {"F4", "ANY POV"}};
    }
    return {{"G", "GUIDE"}};
}

PromptLayout layoutPrompts(const std::vector<PromptChip>& chips, const UiRect& area, float s, float centreX) {
    PromptLayout l;
    l.px = 12.0f * s;
    const float pad = 6.0f * s, inner = 7.0f * s, gap = 14.0f * s, ch = area.h;
    std::vector<float> widths, keys;
    for (const PromptChip& c : chips) {
        const float kw = hudTextWidth(std::strlen(c.key), l.px) + 2.0f * pad;
        keys.push_back(kw);
        widths.push_back(kw + inner + hudTextWidth(std::strlen(c.label), l.px));
    }
    // Most important first: drop from the end until the row (plus the strip's margins) fits.
    size_t n = chips.size();
    auto rowWidth = [&](size_t k) {
        float t = 0.0f;
        for (size_t i = 0; i < k; ++i) t += widths[i] + (i ? gap : 0.0f);
        return t + 2.0f * 10.0f * s;
    };
    while (n > 0 && rowWidth(n) > area.w) --n;
    if (n == 0) return l;
    const float rw = rowWidth(n);
    const float centre = centreX >= 0.0f ? centreX : area.x + area.w * 0.5f;
    const float x0 = std::max(area.x, std::min(centre - rw * 0.5f, area.right() - rw));
    l.bar = {x0, area.y, rw, ch};
    float x = x0 + 10.0f * s;
    for (size_t i = 0; i < n; ++i) {
        l.chips.push_back({x, area.y + 3.0f * s, widths[i], ch - 6.0f * s});
        l.keyW.push_back(keys[i]);
        x += widths[i] + gap;
    }
    return l;
}

}  // namespace ts
