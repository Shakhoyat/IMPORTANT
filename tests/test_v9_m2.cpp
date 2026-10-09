// v9 M2 (unified game controls): T340-T349. CPU only: interaction pick, line of sight, key map, settings ini, menu
// layout, Tab order.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "TestKit.h"
#include "objects/BaseLayout.h"
#include "scene/GameMenu.h"
#include "scene/Interact.h"
#include "scene/KeyMap.h"
#include "scene/Scene.h"
#include "scene/WalkSpace.h"

using ts::testkit::fmt;
using ts::testkit::record;
using namespace ts;

namespace {
Interactable item(InteractAction a, const char* name, Vec3 p, float reach) {
    Interactable it;
    it.action = a;
    it.name = name;
    it.point = p;
    it.reach = reach;
    return it;
}
PovSource src(PovKind k, uint32_t key, const char* name, bool canControl = true) {
    PovSource s;
    s.kind = k;
    s.key = key;
    s.name = name;
    s.canControl = canControl;
    return s;
}
}  // namespace

TS_TEST_GROUP(V9M2) {
    // ---- T340: interaction pick: nearest in reach AND in the view cone; behind / out of reach / beside are skipped ------
    {
        InteractView v;
        v.player = {0, 0, 0};
        v.eye = {0, 1.6f, 0};
        v.forward = {0, -0.3f, 1};   // looking slightly down, along +z
        v.forward = normalize(v.forward);
        std::vector<Interactable> items = {
            item(InteractAction::OpenDoor, "far door", {0, 1, 2.2f}, 2.5f),        // in cone, 2.2 m
            item(InteractAction::TakeControl, "near soldier", {0.4f, 1, 1.4f}, 2.5f),  // in cone, 1.46 m  <- expected
            item(InteractAction::OpenDoor, "behind", {0, 1, -1.0f}, 2.5f),         // closer but behind
            item(InteractAction::OpenDoor, "beside", {1.2f, 1, 0.1f}, 2.5f),       // 85 deg off the view
            item(InteractAction::TakeControl, "tank", {0, 1, 6.0f}, 4.5f),         // in cone, out of reach
        };
        const InteractPick p = pickInteractable(items, v, {});
        // turn around: the "behind" door becomes the pick; nothing in reach in the cone -> -1
        InteractView back = v;
        back.forward = {0, 0, -1};
        const InteractPick pb = pickInteractable(items, back, {});
        std::vector<Interactable> none = {item(InteractAction::OpenDoor, "far", {0, 1, 9}, 2.5f)};
        const InteractPick pn = pickInteractable(none, v, {});
        // an 'always' item (the selection / release) wins over everything
        std::vector<Interactable> withAlways = items;
        Interactable rel;
        rel.action = InteractAction::Release;
        rel.always = true;
        withAlways.push_back(rel);
        const InteractPick pa = pickInteractable(withAlways, v, {});
        record("T340", "Interaction pick: the nearest usable thing within its reach and inside the view cone",
               "near soldier (1.46 m) over the far door; facing back -> the door behind; nothing in reach -> none; "
               "an 'always' item (release / selection) wins",
               fmt("pick '%s' at %.2f m %.0f deg, back '%s', none %d, always '%s'", p.index >= 0 ? items[p.index].name.c_str() : "-",
                   p.distance, p.angleDeg, pb.index >= 0 ? items[pb.index].name.c_str() : "-", pn.index,
                   pa.index >= 0 ? interactActionName(withAlways[pa.index].action) : "-"),
               p.index == 1 && pb.index == 2 && pn.index == -1 && pa.index == static_cast<int>(withAlways.size()) - 1);
    }

    // ---- T341: a wall between the player and the nearest door blocks it; the farther, visible one is picked -----------
    {
        WalkSpace ws;
        ws.addWall(Box{{-1.0f, 0.0f, 0.9f}, {0.5f, 3.0f, 1.1f}});   // a wall 1 m ahead, in front of door A
        std::vector<Box> solids = {Box{{1.7f, 0.0f, 1.6f}, {2.3f, 2.4f, 2.4f}}};   // the target B sits inside its own box
        InteractView v;
        v.player = {0, 0, 0};
        v.eye = {0, 1.6f, 0};
        v.forward = {0, 0, 1};
        std::vector<Interactable> items = {item(InteractAction::OpenDoor, "door A (behind the wall)", {0, 1, 1.6f}, 2.5f),
                                           item(InteractAction::OpenDoor, "door B", {2.0f, 1, 2.0f}, 3.0f)};
        const LineOfSight los = [&](const Vec3& a, const Vec3& b) { return segmentClear(&ws, solids, a, b); };
        const InteractPick blocked = pickInteractable(items, v, los);
        const InteractPick open = pickInteractable(items, v, {});
        const bool selfBoxIgnored = segmentClear(&ws, solids, {2.0f, 1.6f, -2.0f}, {2.0f, 1.0f, 2.0f});
        const bool wallBlocks = !segmentClear(&ws, {}, {0, 1.6f, 0}, {0, 1, 1.6f});
        record("T341", "Interaction line of sight: a wall between the player and a door hides it (WalkSpace walls + solids)",
               "with the wall test: door B; without it: door A (nearer); the target's own box never hides it",
               fmt("with sight '%s', without '%s', own box ignored %d, wall blocks %d",
                   blocked.index >= 0 ? items[blocked.index].name.c_str() : "-", open.index >= 0 ? items[open.index].name.c_str() : "-",
                   selfBoxIgnored ? 1 : 0, wallBlocks ? 1 : 0),
               blocked.index == 1 && open.index == 0 && selfBoxIgnored && wallBlocks);
    }

    // ---- T342: the v9 key map has no conflicts; the detector finds the v8 overloads ----------------------------------
    {
        const std::vector<std::string> c = keyMapConflicts(keyMapV9());
        // v8 as it was: Space = jump (soldier) and pause (anywhere), Esc = release / quit, C and V both switched the soldier view
        // while also being the camera / view-mode keys everywhere.
        const std::vector<KeyBind> v8 = {
            {"Space", 32, 0, KeyCtx::Soldier, "jump", ""},   {"Space", 32, 0, KeyCtx::Any, "pause", ""},
            {"C", 'C', 0, KeyCtx::Soldier, "1st / 3rd", ""}, {"C", 'C', 0, KeyCtx::Any, "camera cycle", ""},
            {"V", 'V', 0, KeyCtx::Soldier, "1st / 3rd", ""}, {"V", 'V', 0, KeyCtx::Any, "view mode", ""},
            {"Esc", 256, 0, KeyCtx::Menu, "resume", ""},      {"Esc", 256, 0, KeyCtx::Any, "release / quit", ""},
        };
        const std::vector<std::string> old = keyMapConflicts(v8);
        record("T342", "Key map: no key does two things in contexts that can be active together (Any overlaps every gameplay "
                       "context; the menu and the POV list are modal)",
               "0 conflicts in keyMapV9(); the same check finds the 3 v8 overloads (Space, C, V) and not the modal Esc",
               fmt("%zu bindings, %zu conflicts%s%s; v8 sample: %zu conflicts", keyMapV9().size(), c.size(), c.empty() ? "" : ": ",
                   c.empty() ? "" : c[0].c_str(), old.size()),
               c.empty() && old.size() == 3 && keyMapV9().size() > 100);
    }

    // ---- T343: the M2 scheme: E / V / Tab / Esc / P / F3 where the plan puts them, every change documented -----------
    {
        const std::vector<KeyBind>& m = keyMapV9();
        auto find = [&](int code, int mods, KeyCtx ctx) -> const KeyBind* {
            for (const KeyBind& k : m)
                if (k.code == code && k.mods == mods && k.ctx == ctx) return &k;
            return nullptr;
        };
        const KeyBind* e = find('E', 0, KeyCtx::Any);
        const KeyBind* tab = find(258, 0, KeyCtx::Any);
        const KeyBind* stab = find(258, kModShift, KeyCtx::Any);
        const KeyBind* esc = find(256, 0, KeyCtx::Any);
        const KeyBind* p = find('P', 0, KeyCtx::Any);
        const KeyBind* f3 = find(290 + 2, 0, KeyCtx::Any);
        const KeyBind* vs = find('V', 0, KeyCtx::Soldier);
        const KeyBind* vv = find('V', 0, KeyCtx::Vehicle);
        const KeyBind* vr = find('V', 0, KeyCtx::Ride);
        int spaces = 0, spaceSoldier = 0;
        for (const KeyBind& k : m)
            if (k.code == 32) ++spaces, spaceSoldier += k.ctx == KeyCtx::Soldier;
        bool vAnywhere = false, cSoldier = false;
        for (const KeyBind& k : m) {
            vAnywhere = vAnywhere || (k.code == 'V' && (k.ctx == KeyCtx::Any || k.ctx == KeyCtx::Overview));
            cSoldier = cSoldier || (k.code == 'C' && (k.ctx == KeyCtx::Soldier || k.ctx == KeyCtx::Vehicle || k.ctx == KeyCtx::Ride));
        }
        const std::vector<KeyBind> changes = keyMapChanges(m);
        std::set<std::string> changed;
        for (const KeyBind& k : changes) changed.insert(k.key);
        const bool documented = changed.count("E") && changed.count("Tab") && changed.count("Esc") && changed.count("P") &&
                                changed.count("F3") && changed.count("Space") && changed.count("V") && changed.count("C");
        const std::string json = keyMapJson(m);
        record("T343", "Key map: E interact, Tab / Shift+Tab units, Esc release -> menu, P pause, F3 HUD, V the only view key, "
                       "Space jump only; every changed key names its old binding; JSON export",
               "all present in their contexts, Space only in SOLDIER, V not global, C not in SOLDIER / VEHICLE / RIDE, "
               ">= 8 documented changes, JSON lists every binding",
               fmt("E %d Tab %d Shift+Tab %d Esc %d P %d F3 %d V %d%d%d, Space %d (soldier %d), V global %d, C in control %d, "
                   "%zu changes, json %zu bytes",
                   e != nullptr, tab != nullptr, stab != nullptr, esc != nullptr, p != nullptr, f3 != nullptr, vs != nullptr, vv != nullptr,
                   vr != nullptr, spaces, spaceSoldier, vAnywhere ? 1 : 0, cSoldier ? 1 : 0, changes.size(), json.size()),
               e && tab && stab && esc && p && f3 && vs && vv && vr && spaces == 1 && spaceSoldier == 1 && !vAnywhere && !cSoldier &&
                   documented && changes.size() >= 8 && json.find("\"conflicts\": []") != std::string::npos &&
                   json.find("\"key\": \"Shift+Tab\"") != std::string::npos);
    }

    // ---- T344: settings ini round trip (defaults and a changed set), bad lines warn, values clamp ----------------------
    {
        GameSettings a;
        GameSettings b;
        b.mouseSensitivity = 1.7f;
        b.invertY = true;
        b.fovDeg = 80.0f;
        b.vsync = true;
        b.fpsLimit = 144.0f;
        b.headBob = false;
        b.quality = 2;
        GameSettings c;
        c.fpsLimit = 0.0f;   // uncapped
        const bool rtA = parseSettings(serializeSettings(a)) == a;
        const bool rtB = parseSettings(serializeSettings(b)) == b;
        const bool rtC = parseSettings(serializeSettings(c)) == c;
        std::vector<std::string> warn;
        const GameSettings d = parseSettings("# x\nmouse_sensitivity = 9\nfov=20\nquality = EPIC\nbogus = 1\nvsync = maybe\nnoequals\n"
                                             "invert_y = on\r\nfps_limit = uncapped\n",
                                             &warn);
        record("T344", "Settings: ThermalSense.ini serialize -> parse gives the same settings; bad lines warn, values clamp",
               "3 round trips equal; sensitivity 9 -> 3.00, fov 20 -> 50, quality epic = 3, invert on, uncapped = 0, 3 warnings",
               fmt("round trips %d %d %d; parsed sens %.2f fov %.0f quality %d invert %d limit %.0f, %zu warnings%s%s", rtA, rtB, rtC,
                   d.mouseSensitivity, d.fovDeg, d.quality, d.invertY ? 1 : 0, d.fpsLimit, warn.size(), warn.empty() ? "" : ": ",
                   warn.empty() ? "" : warn[0].c_str()),
               rtA && rtB && rtC && std::fabs(d.mouseSensitivity - kSensMax) < 1e-4f && d.fovDeg == kFovMin && d.quality == 3 && d.invertY &&
                   d.fpsLimit == 0.0f && warn.size() == 3 && warn[0].find("line 5") != std::string::npos);
    }

    // ---- T345: menu value steps and the mouse-look settings ----------------------------------------------------------
    {
        GameSettings s;
        for (int i = 0; i < 40; ++i) adjustSetting(s, SettingId::Sensitivity, +1);
        const float sensMax = s.mouseSensitivity;
        for (int i = 0; i < 9; ++i) adjustSetting(s, SettingId::Sensitivity, -1);
        const float sens21 = s.mouseSensitivity;
        GameSettings f;
        adjustSetting(f, SettingId::FrameLimit, +1);   // AUTO -> 30
        const float lim1 = f.fpsLimit;
        for (int i = 0; i < 20; ++i) adjustSetting(f, SettingId::FrameLimit, +1);
        const float limEnd = f.fpsLimit;   // uncapped (last choice)
        GameSettings q;
        adjustSetting(q, SettingId::Quality, -1);
        const int qLow = q.quality;   // stays AUTO (-1)
        adjustSetting(q, SettingId::InvertY, 1);
        float dx = 10.0f, dy = 4.0f;
        GameSettings look;
        look.mouseSensitivity = 0.5f;
        look.invertY = true;
        applyLookSettings(look, dx, dy);
        record("T345", "Settings rows: steps clamp at the ends, lists step through their choices; look delta = sensitivity, "
                       "invert Y flips only the vertical",
               "sensitivity max 3.00, 9 steps down 2.10; frame limit AUTO -> 30 -> UNCAPPED; quality floor AUTO; look (10, 4) "
               "at 0.5x inverted = (5, -2)",
               fmt("max %.2f, after 9 down %.2f, limit %.0f -> %.0f (%s), quality %d, invert %d, look (%.2f, %.2f)", sensMax, sens21,
                   lim1, limEnd, settingValueText(f, SettingId::FrameLimit).c_str(), qLow, q.invertY ? 1 : 0, dx, dy),
               std::fabs(sensMax - 3.0f) < 1e-4f && std::fabs(sens21 - 2.1f) < 1e-4f && lim1 == 30.0f && limEnd == 0.0f && qLow == -1 &&
                   q.invertY && std::fabs(dx - 5.0f) < 1e-5f && std::fabs(dy + 2.0f) < 1e-5f);
    }

    // ---- T346: pause menu layout: tabs above the guide panel, rows inside it, hit tests return what was drawn -------------
    {
        bool ok = true;
        std::string why;
        int checked = 0;
        for (const auto& wh : std::vector<std::pair<int, int>>{{1920, 1080}, {1280, 720}, {1600, 900}, {1024, 768}}) {
            const PauseMenuLayout l = pauseMenuLayout(wh.first, wh.second);
            const UiRect screen{0, 0, static_cast<float>(wh.first), static_cast<float>(wh.second)};
            for (int i = 0; i < kMenuTabCount; ++i) {
                const UiRect& t = l.tabs[i];
                if (!t.within(screen) || t.overlaps(l.page)) ok = false, why = "tab outside / over the panel";
                for (int j = i + 1; j < kMenuTabCount; ++j)
                    if (t.overlaps(l.tabs[j])) ok = false, why = "tabs overlap";
                if (menuTabAt(l, t.x + t.w * 0.5f, t.y + t.h * 0.5f) != i) ok = false, why = "tab hit";
                ++checked;
            }
            for (size_t i = 0; i < l.rows.size(); ++i) {
                const SettingRowRect& r = l.rows[i];
                if (!r.row.within(l.page) || !r.minus.within(r.row) || !r.plus.within(r.row) || !r.value.within(r.row) ||
                    r.minus.overlaps(r.value) || r.value.overlaps(r.plus))
                    ok = false, why = "setting row geometry";
                const SettingHit m = settingAt(l, r.minus.x + 2, r.minus.y + 2), p = settingAt(l, r.plus.x + r.plus.w - 2, r.plus.y + 2),
                                 lab = settingAt(l, r.row.x + 5, r.row.y + 5);
                if (m.row != static_cast<int>(i) || m.dir != -1 || p.dir != 1 || lab.dir != 0 || lab.row != static_cast<int>(i))
                    ok = false, why = "setting hit";
                ++checked;
            }
            if (l.rows.size() != static_cast<size_t>(kSettingCount) || !l.settingsFooter.within(l.page)) ok = false, why = "rows / footer";
            for (const GuideButton& b : l.guide.buttons)
                if (guideActionAt(l.guide, b.r.x + b.r.w * 0.5f, b.r.y + b.r.h * 0.5f) != b.action) ok = false, why = "guide hit";
        }
        const bool pages = nextMenuPage(MenuTab::Settings, 1) == MenuTab::Travel && nextMenuPage(MenuTab::Travel, -1) == MenuTab::Settings &&
                           !menuTabIsPage(MenuTab::Quit) && menuTabSection(MenuTab::Time) == GuideSection::Time;
        record("T346", "Pause menu layout at 4 screen sizes: 6 tabs above the field-guide panel, settings rows inside it, "
                       "every hit test returns the element drawn there",
               "no overlap / overflow, tab / row / -/+ / guide button hits exact, Tab cycles the 4 pages",
               fmt("%d elements checked, %s, pages %d", checked, ok ? "all consistent" : why.c_str(), pages ? 1 : 0), ok && pages);
    }

    // ---- T347: Tab order: soldiers, then UAVs, then vehicles; rovers and rounds left out; wraps both ways ---------------
    {
        std::vector<PovSource> list = {src(PovKind::Tank, 1, "TANK"),          src(PovKind::Uav, 2, "UAV-1"),
                                       src(PovKind::Helicopter, 3, "HELI"),    src(PovKind::Soldier, 4, "S1"),
                                       src(PovKind::Soldier, 5, "S2"),         src(PovKind::Uav, 6, "UAV-2", false),
                                       src(PovKind::Rover, 7, "R1"),           src(PovKind::Missile, 8, "MSL", false)};
        const std::vector<int> order = unitOrder(list);
        std::string names;
        for (int i : order) names += list[static_cast<size_t>(i)].name + " ";
        const int first = cycleUnit(list, 0, +1), last = cycleUnit(list, 0, -1);
        const int afterS2 = cycleUnit(list, 5, +1), wrap = cycleUnit(list, 3, +1), back = cycleUnit(list, 4, -1);
        const int fromRover = cycleUnit(list, 7, +1);
        record("T347", "Tab cycles the controllable units in game order (soldiers, UAVs, vehicles), skipping hostile rovers and "
                       "rounds, wrapping at both ends",
               "order S1 S2 UAV-1 UAV-2 TANK HELI; first S1, last HELI; S2 -> UAV-1; HELI -> S1; S1 back -> HELI; from a rover -> S1",
               fmt("order %s; first %s last %s, S2-> %s, HELI-> %s, S1<- %s, rover-> %s", names.c_str(), list[first].name.c_str(),
                   list[last].name.c_str(), list[afterS2].name.c_str(), list[wrap].name.c_str(), list[back].name.c_str(),
                   list[fromRover].name.c_str()),
               names == "S1 S2 UAV-1 UAV-2 TANK HELI " && first == 3 && last == 2 && afterS2 == 1 && wrap == 3 && back == 2 &&
                   fromRover == 3);
    }

    // ---- T348: prompts read like a game ------------------------------------------------------------------------------
    {
        Interactable d = item(InteractAction::OpenDoor, "blast doors", {}, 2.5f);
        Interactable h = item(InteractAction::EnterBuilding, "Barracks", {}, 2.5f);
        Interactable u = item(InteractAction::TakeControl, "UAV-1", {}, 90.0f);
        Interactable r = item(InteractAction::Release, "MBT-1 TANK", {}, 0.0f);
        Interactable w = item(InteractAction::RideAlong, "UAV-2 DRONE (WING)", {}, 90.0f);
        const std::string pd = interactPrompt(d), ph = interactPrompt(h), pu = interactPrompt(u), pr = interactPrompt(r), pw = interactPrompt(w);
        record("T348", "Interaction prompts", "\"[E] Open blast doors\", \"[E] Enter Barracks\", \"[E] Take control of UAV-1\", "
                                              "\"[E] Release\", \"[E] Ride UAV-2 DRONE (WING)\"",
               fmt("'%s' '%s' '%s' '%s' '%s'", pd.c_str(), ph.c_str(), pu.c_str(), pr.c_str(), pw.c_str()),
               pd == "[E] Open blast doors" && ph == "[E] Enter Barracks" && pu == "[E] Take control of UAV-1" && pr == "[E] Release" &&
                   pw == "[E] Ride UAV-2 DRONE (WING)");
    }

    // ---- T349: the real barracks hall: its doorway is visible from the apron in front of it, hidden from beside the hall ----
    {
        Scene s;
        const WalkSpace& ws = s.walkSpace();
        const std::vector<Box>& solids = s.staticSolids();
        namespace hl = layout::hall;
        const Vec3 door{hl::kDoorX, hl::kFloorY + 1.1f, hl::kZ1 + 0.35f};          // the "Enter Barracks" point (outside)
        const Vec3 inner{hl::kDoorX, hl::kFloorY + 1.1f, hl::kZ1 - 0.6f};          // "Exit" point inside
        const Vec3 front{hl::kDoorX + 0.3f, layout::kApronLevel + 1.55f, hl::kZ1 + 2.0f};   // on the apron, facing the door
        // outside the east wall, aimed at the solid panel between its two windows (z -12.4 .. -11.0; the windows are
        // real gaps: through them the doorway IS in sight)
        const Vec3 east{hl::kX1 + 1.5f, layout::kApronLevel + 1.55f, -12.5f};
        const Vec3 inside{hl::kDoorX - 0.5f, hl::kFloorY + 1.55f, hl::kZ1 - 2.2f};
        const bool frontClear = segmentClear(&ws, solids, front, door);
        const bool insideClear = segmentClear(&ws, solids, inside, inner);
        // from the east the straight line to the inner (exit) point runs through the east wall
        const bool eastBlocked = !segmentClear(&ws, solids, east, inner);
        // whole pick with the real geometry: standing against the east wall, 5.2 m from the exit point, nothing is offered
        InteractView v;
        v.player = {hl::kX1 + 0.3f, layout::kApronLevel, -12.0f};
        v.eye = v.player + Vec3{0, 1.55f, 0};
        v.forward = normalize(inner - v.eye);
        std::vector<Interactable> items = {item(InteractAction::ExitBuilding, "Barracks", inner, 6.0f)};
        const LineOfSight los = [&](const Vec3& a, const Vec3& b) { return segmentClear(&ws, solids, a, b); };
        const InteractPick blocked = pickInteractable(items, v, los);
        record("T349", "Barracks hall with the real scene geometry: the doorway is in sight from the apron in front of it and "
                       "from inside, hidden from beside the hall (wall solids)",
               "front clear, inside clear, east blocked, no pick through the wall",
               fmt("%zu solids, %zu walls; front %d, inside %d, east blocked %d, pick through wall %d", solids.size(), ws.walls().size(),
                   frontClear ? 1 : 0, insideClear ? 1 : 0, eastBlocked ? 1 : 0, blocked.index),
               frontClear && insideClear && eastBlocked && blocked.index == -1);
    }
}
