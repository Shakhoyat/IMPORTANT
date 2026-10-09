// v9 M2: unified game controls on the App side. E = context interaction (scene/Interact.h picks the nearest usable
// thing in reach and view: doors, the gate barrier, the barracks hall entrance, soldiers, vehicles, UAVs, "release"),
// Tab / Shift+Tab = next / previous controllable unit, V = the person / camera view of what is controlled, F3 = HUD
// level, Esc = release, else the pause menu (Resume / Travel / Time / View / Settings / Quit; the Travel, Time and View
// pages are the v7.1 field guide of HudMath), player settings persisted in ThermalSense.ini (scene/GameMenu.h), and
// the decluttered per-mode gameplay HUD (soldier: crosshair, stance / rifle; the interaction prompt).
// No GLFW here (the file is built headless too): the window-only requests (quit, Frame Explorer) are flags the
// interactive loop picks up.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "app/App.h"
#include "objects/BaseLayout.h"
#include "raytrace/RtTypes.h"
#include "scene/Control.h"

namespace ts {

namespace {

const Rgba kPromptKey{1.0f, 0.82f, 0.25f, 1.0f};
const Rgba kPromptText{0.95f, 0.97f, 1.0f, 0.98f};
const Rgba kAmber{1.0f, 0.75f, 0.15f, 1.0f};
const Rgba kWhite{0.92f, 0.95f, 1.0f, 0.95f};
const Rgba kDim{0.62f, 0.68f, 0.75f, 0.85f};
const Rgba kGreen{0.35f, 1.0f, 0.55f, 1.0f};
const Rgba kRed{1.0f, 0.32f, 0.25f, 1.0f};

constexpr float kDoorReach = 2.5f, kBarrierReach = 3.0f, kSoldierReach = 2.5f, kHallReach = 3.0f;
constexpr float kHallPassOpen = 0.6f;    // v9 M4: Enter / Exit Barracks goes through once the leaf is this far open
constexpr float kHallWaitMax = 2.5f;     // s: ... or after this long (the leaf may stand against the player)
constexpr float kFarReach = 90.0f, kFarConeDeg = 9.0f, kFootConeDeg = 50.0f;

const Soldier* soldierAt(const Scene& scene, int index) {
    const std::vector<Controllable*> c = sa::controllables(scene);
    if (index < 0 || index >= static_cast<int>(c.size())) return nullptr;
    return dynamic_cast<const Soldier*>(c[index]);
}

bool insideHall(const Vec3& p) {
    namespace hl = layout::hall;
    return p.x > hl::kIX0 && p.x < hl::kIX1 && p.z > hl::kIZ0 && p.z < hl::kIZ1 && p.y > hl::kFloorY - 0.6f &&
           p.y < hl::kFloorY + hl::kCeiling;
}

// "SOLDIER 3 GUNNER" -> the name as the player reads it in a prompt (doors in lower case: "blast doors").
std::string doorName(const char* name) {
    std::string s = name;
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool isOpenStatus(const std::string& st) {
    return st.find("OPENING") != std::string::npos || st.find("RAISING") != std::string::npos ||
           (st.find("OPEN") != std::string::npos && st.find("CLOS") == std::string::npos);
}

}  // namespace

// ------------------------------------------------------------------------------------------------ interaction (E)

std::vector<Interactable> App::gatherInteractables(InteractView& view) const {
    std::vector<Interactable> items;
    const std::vector<Controllable*> ctl = sa::controllables(*scene_);
    const RigPose& cam = shownPose_;
    view.eye = cam.eye;
    view.player = cam.eye;
    view.forward = cam.forward();
    if (camera_.mode == CameraMode::Cinematic) return items;

    // Controlling a vehicle, UAV or rover / riding along: E releases (riding along a controllable: E takes it).
    if (possessedRover_ >= 0 || (camera_.mode == CameraMode::Possess && !controllingSoldier())) {
        Interactable it;
        it.action = InteractAction::Release;
        it.always = true;
        it.name = possessedRover_ >= 0 ? "rover" : possessedName();
        items.push_back(it);
        return items;
    }
    if (camera_.mode == CameraMode::Pov) {
        Interactable it;
        it.always = true;
        it.name = povSrc_.name;
        it.key = povSrc_.key;
        it.action = povHold_ < 0.0f && povSrc_.canControl ? InteractAction::TakeControl : InteractAction::Release;
        items.push_back(it);
        return items;
    }

    const std::vector<PovSource> list = listPovSources(*scene_, cam.eye);
    const int self = possessedSoldierIndex();
    const bool onFoot = self >= 0 || camera_.mode == CameraMode::Walk;
    if (!onFoot) {
        // Nothing under control: the selection first (click, then E), else whatever unit the view centres on.
        if (selKey_ != 0) {
            const int i = findPov(list, selKey_);
            if (i >= 0) {
                Interactable it;
                it.always = true;
                it.name = list[i].name;
                it.ref = i;
                it.key = list[i].key;
                it.action = list[i].canControl ? InteractAction::TakeControl : InteractAction::RideAlong;
                items.push_back(it);
                return items;
            }
        }
        view.flatCone = false;
        view.coneHalfDeg = kFarConeDeg;
        for (size_t i = 0; i < list.size(); ++i) {
            const PovSource& s = list[i];
            if (!isUnit(s) && s.kind != PovKind::Rover) continue;
            Interactable it;
            it.name = s.name;
            it.point = s.centre;
            it.reach = kFarReach;
            it.ref = static_cast<int>(i);
            it.key = s.key;
            it.action = s.canControl ? InteractAction::TakeControl : InteractAction::RideAlong;
            items.push_back(it);
        }
        return items;
    }

    // On foot: a soldier under control (first or third person) or the v4 walker.
    Vec3 feet{camera_.walkX, camera_.walkFeetY, camera_.walkZ};
    if (const Soldier* sd = soldierAt(*scene_, self)) feet = sd->position();
    view.player = feet;
    view.eye = feet + Vec3{0.0f, 1.55f, 0.0f};
    view.flatCone = true;
    view.coneHalfDeg = kFootConeDeg;
    // Doors and the gate barrier (the controllables the v4 walker toggles).
    for (size_t i = 0; i < ctl.size(); ++i) {
        if (!ctl[i]) continue;
        const char* n = ctl[i]->name();
        const bool barrier = nameHas(n, "BARRIER") || nameHas(n, "GATE");
        if (!barrier && !nameHas(n, "DOOR")) continue;
        Interactable it;
        it.name = doorName(n);
        it.point = ctl[i]->position();
        it.reach = barrier ? kBarrierReach : kDoorReach;
        it.ref = static_cast<int>(i);
        it.action = isOpenStatus(ctl[i]->status()) ? InteractAction::CloseDoor : InteractAction::OpenDoor;
        // v9 M4: a hinged door is used at its handle on the player's side ("[E] Open door" / "[E] Close door"); where the
        // leaf is going decides the verb (a swinging door can be reversed).
        if (const auto* portal = dynamic_cast<const DoorPortal*>(ctl[i])) {
            const DoorKinematics& d = portal->door();
            it.name = "door";
            it.point = d.handlePoint(feet);
            it.action = d.targetOpen() ? InteractAction::CloseDoor : InteractAction::OpenDoor;
        }
        items.push_back(it);
    }
    // The barracks hall: its doorway in the north wall (x = 23.5); outside = enter, inside = exit. v9 M4: the point lies
    // on the far side of the door, so the door's own handle (on the near side) is the nearer pick while the door is shut
    // ("[E] Open door"); once it is open, looking through the doorway offers the move through it.
    {
        namespace hl = layout::hall;
        Interactable it;
        it.name = "Barracks";
        const bool inside = insideHall(feet);
        it.action = inside ? InteractAction::ExitBuilding : InteractAction::EnterBuilding;
        it.point = {hl::kDoorX, hl::kFloorY + 1.1f, inside ? hl::kZ1 + 0.3f : hl::kIZ1 - 0.3f};
        it.reach = kHallReach;
        items.push_back(it);
    }
    // Other soldiers (take over), vehicles / UAVs within a step or two of their hull.
    for (size_t i = 0; i < list.size(); ++i) {
        const PovSource& s = list[i];
        if (!isUnit(s) || (self >= 0 && s.controllable == self)) continue;
        Interactable it;
        it.name = s.name;
        it.point = s.centre;
        it.reach = s.kind == PovKind::Soldier ? kSoldierReach : 2.0f + s.radius;
        it.ref = static_cast<int>(i);
        it.key = s.key;
        it.action = s.canControl ? InteractAction::TakeControl : InteractAction::RideAlong;
        items.push_back(it);
    }
    return items;
}

bool App::updateInteraction() {
    interactValid_ = false;
    interactPrompt_.clear();
    if (!scene_ || !shownValid_ || menuOpen_ || pickerOpen_) return false;
    InteractView view;
    const std::vector<Interactable> items = gatherInteractables(view);
    if (items.empty()) return false;
    const WalkSpace* ws = sa::walkSpace(*scene_);
    const std::vector<Box>& solids = scene_->staticSolids();
    const InteractPick pick = pickInteractable(items, view, [&](const Vec3& a, const Vec3& b) { return segmentClear(ws, solids, a, b); });
    if (pick.index < 0) return false;
    interact_ = items[static_cast<size_t>(pick.index)];
    interactValid_ = true;
    interactPrompt_ = interactPrompt(interact_);
    return true;
}

void App::performInteract() {
    if (!updateInteraction()) return;
    const Interactable it = interact_;
    std::printf("Interact: %s\n", interactPrompt_.c_str());
    switch (it.action) {
        case InteractAction::OpenDoor:
        case InteractAction::CloseDoor: {
            // v9 M4: a hinged door turns at once (the player keeps control: no tick of door possession)
            const std::vector<Controllable*> ctl = sa::controllables(*scene_);
            if (it.ref >= 0 && it.ref < static_cast<int>(ctl.size()))
                if (auto* portal = dynamic_cast<DoorPortal*>(ctl[static_cast<size_t>(it.ref)])) {
                    portal->door().toggle();
                    break;
                }
            interactIdx_ = it.ref;   // one tick of possession with actionPressed (gatherControl), as the v4 walker
            break;
        }
        case InteractAction::EnterBuilding:
        case InteractAction::ExitBuilding: {
            const bool enter = it.action == InteractAction::EnterBuilding;
            // v9 M4: through a door that is not open yet: open it first, the move follows once the leaf is out of the way
            DoorPortal* door = scene_->hallDoor();
            if (door && door->door().openFraction() < kHallPassOpen) {
                if (!door->door().targetOpen()) door->door().setTarget(true, true);
                hallMove_ = HallMove{true, enter, 0.0f};
                std::printf("Interact: the door opens first (%s)\n", door->door().stateName());
                break;
            }
            moveThroughHall(enter);
            break;
        }
        case InteractAction::TakeControl:
            if (camera_.mode == CameraMode::Pov) {
                takeControl();
                break;
            }
            if (it.key != 0) {
                const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
                const int i = findPov(list, it.key);
                if (i >= 0) {
                    selKey_ = it.key;
                    controlSource(i);
                }
            }
            break;
        case InteractAction::RideAlong:
            if (it.key != 0) jumpToPov(it.key);
            break;
        case InteractAction::Release: releaseControl(); break;
        case InteractAction::None: break;
    }
}

// The M2 Enter / Exit Barracks move: the controlled soldier (or the v4 walker) to the far side of the doorway, facing
// on, with a camera blend from where the view was.
void App::moveThroughHall(bool enter) {
    namespace hl = layout::hall;
    const float z = enter ? hl::kZ1 - 1.1f : hl::kZ1 + 1.0f, yaw = enter ? 180.0f : 0.0f;
    const float feetY = enter ? hl::kFloorY : layout::kApronLevel;
    const RigPose from = shownPose_;
    const int si = possessedSoldierIndex();
    if (si >= 0) {
        if (Garrison* g = scene_->garrison())
            for (int u = 0; u < g->count(); ++u)
                if (sa::soldierIndex(*scene_, u) == si) g->unit(u)->place(hl::kDoorX, z, yaw, feetY);
        tp_.invalidate();
        tpPoseValid_ = false;
        alignYaw_ = 0.0f;
    } else {
        camera_.walkX = hl::kDoorX;
        camera_.walkZ = z;
        camera_.walkYawDeg = yaw;
        camera_.walkFeetY = feetY;
    }
    beginJump(from);
    audio_.uiCue(audio::UiCue::PovJump);
    std::printf("Barracks: %s\n", enter ? "entered" : "left");
}

void App::stepHallMove(float dt) {
    if (!hallMove_.active) return;
    hallMove_.t += dt;
    const DoorPortal* door = scene_->hallDoor();
    const bool ready = !door || door->door().openFraction() >= kHallPassOpen || hallMove_.t > kHallWaitMax ||
                       (door->door().blocked() && hallMove_.t > 0.4f);
    if (!ready) return;
    hallMove_.active = false;
    moveThroughHall(hallMove_.enter);
}

// Eye adaptation, CPU side (scene/EyeAdapt.h): the local exposure bias follows the indoor light volume around the camera
// of the last simulation tick (interactive: the tick pose; offline modes: one frame per tick, the shown pose), so it is
// a function of the tick sequence. The renderer adapts the metered luminance itself (asymmetric rates, kAdaptFrag).
void App::stepEyeAdaptation(float dt) {
    if (!scene_) return;
    const Vec3 eye = tickPoseValid_[1] ? tickPose_[1].eye : shownPose_.eye;
    if (!shownValid_ && !tickPoseValid_[1]) return;
    eyeBias_.step(localExposureBiasTarget(sa::walkSpace(*scene_), eye), dt);
}

// ------------------------------------------------------------------------------------------------ Tab, V, F3

void App::cycleUnits(int dir) {
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    PovSource cur;
    uint32_t key = currentSource(cur) ? cur.key : 0;
    if (key == 0) key = selKey_;
    const int i = cycleUnit(list, key, dir);
    if (i < 0) return;
    selKey_ = list[static_cast<size_t>(i)].key;
    controlSource(i);
}

void App::cyclePersonView() {
    if (controllingSoldier()) {   // first <-> third person
        setViewThirdPerson(camera_.possessSight);
        return;
    }
    if (camera_.mode == CameraMode::Pov) {   // first -> chase -> alt camera
        cycleView();
        return;
    }
    if (camera_.mode == CameraMode::Possess) {   // vehicle / UAV: chase -> sight -> alt
        const RigPose from = shownPose_;
        toggleSight();
        beginJump(from);
    }
}

void App::cycleHudLevel() {
    // GAME (decluttered) -> DEBUG (the v8 corner blocks, UAV / rover status, triangle counter) -> OFF -> GAME
    if (showHud_ && !debugHud_) debugHud_ = true;
    else if (showHud_) showHud_ = false;
    else showHud_ = true, debugHud_ = false;
    std::printf("HUD %s\n", !showHud_ ? "OFF" : debugHud_ ? "DEBUG" : "GAME");
}

// ------------------------------------------------------------------------------------------------ settings

void App::loadSettings() {
    if (opt_.iniPath.empty()) return;
    std::ifstream f(opt_.iniPath, std::ios::binary);
    if (!f) {
        std::printf("Settings: %s not found, defaults\n", opt_.iniPath.c_str());
        return;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<std::string> warnings;
    gs_ = parseSettings(ss.str(), &warnings);
    for (const std::string& w : warnings) std::fprintf(stderr, "%s: %s\n", opt_.iniPath.c_str(), w.c_str());
    std::printf("Settings: %s (sensitivity %.2f, invert Y %s, fov %.0f, vsync %s, frame limit %s, head bob %s, quality %s)\n",
                opt_.iniPath.c_str(), gs_.mouseSensitivity, gs_.invertY ? "on" : "off", gs_.fovDeg, gs_.vsync ? "on" : "off",
                settingValueText(gs_, SettingId::FrameLimit).c_str(), gs_.headBob ? "on" : "off",
                settingValueText(gs_, SettingId::Quality).c_str());
}

void App::saveSettings() {
    if (opt_.iniPath.empty()) return;
    std::ofstream f(opt_.iniPath, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "Settings: cannot write %s\n", opt_.iniPath.c_str());
        return;
    }
    f << serializeSettings(gs_);
}

void App::changeSetting(SettingId id, int dir) {
    adjustSetting(gs_, id, dir);
    switch (id) {
        case SettingId::Vsync: cliVsync_ = false; displayDirty_ = true; break;
        case SettingId::FrameLimit: cliFpsLimit_ = false; displayDirty_ = true; break;
        case SettingId::Quality:
            settings_.quality = gs_.quality >= 0 ? static_cast<Quality>(gs_.quality) : autoQuality();
            qualityAuto_ = false;
            break;
        default: break;
    }
    saveSettings();
    std::printf("Setting %s = %s\n", settingLabel(id), settingValueText(gs_, id).c_str());
}

bool App::effectiveVsync() const { return cliVsync_ ? opt_.vsync > 0 : gs_.vsync; }
float App::effectiveFpsLimit() const { return cliFpsLimit_ ? opt_.fpsLimit : gs_.fpsLimit; }

// ------------------------------------------------------------------------------------------------ pause menu

void App::openMenu() {
    if (menuOpen_) return;
    menuOpen_ = true;
    menuWasPaused_ = paused_;
    paused_ = true;
    menuDrag_ = false;
    pickerOpen_ = false;
    showHelp_ = false;
    if (!menuTabIsPage(menuTab_)) menuTab_ = MenuTab::Travel;
    audio_.uiCue(audio::UiCue::PickerOpen);
    std::printf("Pause menu\n");
}

void App::closeMenu() {
    if (!menuOpen_) return;
    menuOpen_ = false;
    menuDrag_ = false;
    paused_ = menuWasPaused_;
    audio_.uiCue(audio::UiCue::PickerClose);
}

namespace {
int sourceOfKind(const std::vector<PovSource>& list, PovKind k, int uav = -1) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].kind == k && (uav < 0 || list[i].uav == uav)) return static_cast<int>(i);
    return -1;
}
}  // namespace

void App::setClockHour(float hour) {
    DayCycle* clock = sa::dayCycle(*scene_);
    if (!clock) return;
    clock->setAuto(false);
    clock->setHour(hour);
    clock->applyTo(scene_->lights);   // visible at once, also while the menu holds the simulation
}

void App::menuAction(GuideAction a) {
    if (a == GuideAction::None) return;
    DayCycle* clock = sa::dayCycle(*scene_);
    const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
    auto travel = [&](auto&& go) {   // close the menu first: the jump blends from the view on screen
        closeMenu();
        go();
    };
    menuNote_.clear();
    switch (a) {
        case GuideAction::RideUav1:
        case GuideAction::RideUav2: {
            const int i = sourceOfKind(list, PovKind::Uav, a == GuideAction::RideUav1 ? 0 : 1);
            if (i >= 0) travel([&] { jumpToPov(list[static_cast<size_t>(i)].key); });
            break;
        }
        case GuideAction::SoldierFirst:
        case GuideAction::SoldierThird:
            travel([&] {
                if (possessRandomSoldier() && a == GuideAction::SoldierThird) setViewThirdPerson(true);
            });
            break;
        case GuideAction::PickPov:
            travel([&] {
                if (!pickerOpen_) togglePicker();
            });
            break;
        case GuideAction::FreeCam: travel([&] { enterFreeCam(); }); break;
        case GuideAction::Tank:
        case GuideAction::Helicopter: {
            const int i = sourceOfKind(list, a == GuideAction::Tank ? PovKind::Tank : PovKind::Helicopter);
            if (i >= 0) travel([&] { controlSource(i); });
            break;
        }
        case GuideAction::Rover: {
            const int i = sourceOfKind(list, PovKind::Rover);
            if (i >= 0) travel([&] { controlSource(i); });
            else menuNote_ = "NO INTRUDER ON THE FIELD: THE WAVES COME AT NIGHT (TIME -> MIDNIGHT)";
            break;
        }
        case GuideAction::Dawn:
        case GuideAction::Noon:
        case GuideAction::Dusk:
        case GuideAction::Midnight: setClockHour(guidePresetHour(a)); break;
        case GuideAction::TimeSlider: break;   // handled by the click / drag
        case GuideAction::CycleAuto:
            if (clock) clock->toggleAuto();
            break;
        case GuideAction::CycleSpeed:
            if (clock) {
                if (cycleClock_ != clock) {   // first use on this world: remember its own rates
                    cycleClock_ = clock;
                    cycleBase_[0] = clock->config().daySecPerHour;
                    cycleBase_[1] = clock->config().nightSecPerHour;
                    guideSpeed_ = 0;
                }
                guideSpeed_ = (guideSpeed_ + 1) % kGuideSpeedCount;
                DayCycleConfig cfg = clock->config();
                cfg.daySecPerHour = cycleBase_[0] / guideSpeedFactor(guideSpeed_);
                cfg.nightSecPerHour = cycleBase_[1] / guideSpeedFactor(guideSpeed_);
                sa::setDayCycleConfig(*clock, cfg);
            }
            break;
        case GuideAction::PersonToggle: cyclePersonView(); break;
        case GuideAction::RenderMode: settings_.rtMode = rt::nextRtMode(settings_.rtMode); break;
        case GuideAction::FrameExplorer:
            closeMenu();
            explorerRequest_ = true;   // the interactive loop opens it (needs the window)
            break;
        case GuideAction::Thermal: settings_.view = settings_.view == ViewMode::Thermal ? ViewMode::Visible : ViewMode::Thermal; break;
        case GuideAction::HudToggle: cycleHudLevel(); break;
        case GuideAction::Feeds: showFeed_ = !showFeed_; break;
        case GuideAction::Close: closeMenu(); break;
        case GuideAction::None: break;
    }
}

void App::menuSelectTab(int tab) {
    const MenuTab t = static_cast<MenuTab>(std::clamp(tab, 0, kMenuTabCount - 1));
    if (t == MenuTab::Resume) {
        closeMenu();
    } else if (t == MenuTab::Quit) {
        closeMenu();
        saveSettings();
        quitRequest_ = true;
    } else {
        menuTab_ = t;
        audio_.uiCue(audio::UiCue::PickerMove);
    }
}

bool App::menuKey(int key, bool shift) {
    constexpr int kEsc = 256, kEnter = 257, kTab = 258, kRight = 262, kLeft = 263, kDown = 264, kUp = 265, kKpEnter = 335;
    if (key == kEsc || (key == 'G' && menuTab_ != MenuTab::Settings)) {
        closeMenu();
        return true;
    }
    if (key == kTab) {
        menuSelectTab(static_cast<int>(nextMenuPage(menuTab_, shift ? -1 : 1)));
        return true;
    }
    if (menuTab_ == MenuTab::Settings) {
        if (key == kUp || key == kDown) {
            menuRow_ = (menuRow_ + (key == kUp ? kSettingCount - 1 : 1)) % kSettingCount;
            audio_.uiCue(audio::UiCue::PickerMove);
        } else if (key == kLeft || key == kRight) {
            changeSetting(static_cast<SettingId>(menuRow_), key == kLeft ? -1 : 1);
        } else if (key == kEnter || key == kKpEnter) {
            changeSetting(static_cast<SettingId>(menuRow_), 1);
        }
        return true;
    }
    if (key == kLeft || key == kRight) {   // nudge the time-of-day slider by 15 minutes
        if (const DayCycle* clock = sa::dayCycle(*scene_)) setClockHour(clock->hour() + (key == kLeft ? -0.25f : 0.25f));
        return true;
    }
    menuAction(guideKeyAction(key));
    return true;
}

void App::menuPointer(float x, float y, int button, bool press) {
    menuMouseX_ = x;
    menuMouseY_ = y;
    if (button < 0) {   // move
        if (menuDrag_) {
            const PauseMenuLayout l = pauseMenuLayout(lastW_, lastH_);
            setClockHour(guideSliderHour(l.guide, x));
        }
        return;
    }
    if (!press) {
        menuDrag_ = false;
        return;
    }
    if (button != 0) return;
    const PauseMenuLayout l = pauseMenuLayout(lastW_, lastH_);
    const int tab = menuTabAt(l, x, y);
    if (tab >= 0) {
        menuSelectTab(tab);
        return;
    }
    if (l.guide.close.contains(x, y)) {
        closeMenu();
        return;
    }
    if (menuTab_ == MenuTab::Settings) {
        const SettingHit hit = settingAt(l, x, y);
        if (hit.row >= 0) {
            menuRow_ = hit.row;
            if (hit.dir != 0) changeSetting(static_cast<SettingId>(hit.row), hit.dir);
        }
        return;
    }
    const GuideAction a = guideActionAt(l.guide, x, y);
    if (a == GuideAction::TimeSlider) {
        menuDrag_ = true;
        setClockHour(guideSliderHour(l.guide, x));
        return;
    }
    menuAction(a);
}

// ------------------------------------------------------------------------------------------------ drawing

void App::drawInteractPrompt(int w, int h) {
    updateInteraction();
    if (!interactValid_ || interactPrompt_.empty()) return;
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f, W = static_cast<float>(w), H = static_cast<float>(h);
    // "[E] Take control of UAV-1": a key cap and the action, on the v8 prompt backing (bottom centre, above the strip).
    const std::string text = interactPrompt_.substr(4);   // without "[E] "
    const float px = 17.0f * s, cap = 30.0f * s, pad = 14.0f * s;
    const float tw = hud.textWidth(text, px);
    const bool overview = !controllingSoldier() && camera_.mode != CameraMode::Walk;
    const char* sub = interact_.always && camera_.mode != CameraMode::Possess && camera_.mode != CameraMode::Pov && possessedRover_ < 0
                          ? "ENTER / DOUBLE-CLICK also work    ESC clears the selection"
                          : nullptr;
    const float bw = pad + cap + 10.0f * s + tw + pad, bh = 42.0f * s;
    // Vehicles / UAVs: above their control panel and ammo / reload bars (drawPossessHud, bottom centre).
    const bool vehiclePanel = camera_.mode == CameraMode::Possess && !controllingSoldier();
    const float x = W * 0.5f - bw * 0.5f;
    const float y = vehiclePanel ? H - 218.0f * s : overview ? H - 140.0f * s : H * 0.5f + 64.0f * s;
    hud.roundRect(x, y, bw, bh, 7.0f * s, Rgba{0.0f, 0.0f, 0.0f, 0.62f});
    hud.roundFrame(x, y, bw, bh, 7.0f * s, 1.2f * s, Rgba{1.0f, 1.0f, 1.0f, 0.18f});
    const float cx = x + pad, cy = y + (bh - cap) * 0.5f;
    hud.roundRect(cx, cy, cap, cap, 5.0f * s, Rgba{1.0f, 0.82f, 0.25f, 0.95f});
    hud.text(cx + (cap - hud.textWidth("E", px)) * 0.5f, cy + (cap - px) * 0.5f, "E", px, Rgba{0.05f, 0.05f, 0.06f, 1.0f});
    hud.text(cx + cap + 10.0f * s, y + (bh - px) * 0.5f, text, px, interact_.action == InteractAction::Release ? kPromptKey : kPromptText);
    if (sub) hud.text(W * 0.5f - hud.textWidth(sub, 12.0f * s) * 0.5f, y + bh + 6.0f * s, sub, 12.0f * s, kDim);
}

void App::drawSoldierHud(int w, int h) {
    const int si = possessedSoldierIndex();
    const Soldier* sd = soldierAt(*scene_, si);
    if (!sd) return;
    Hud& hud = renderer_->hud();
    const float s = static_cast<float>(h) / 900.0f, W = static_cast<float>(w), H = static_cast<float>(h);
    // First-person crosshair (the third-person one comes with the spring-arm HUD); hidden while aiming down the sight.
    if (camera_.possessSight && aimBlend_ < 0.5f) {
        const float cx = W * 0.5f, cy = H * 0.5f;
        const float gap = (5.0f + 9.0f * recoil_ + 5.0f * std::min(std::fabs(sd->speed()) / 4.5f, 1.0f)) * s, arm = 8.0f * s;
        const Rgba c{0.95f, 0.97f, 1.0f, 0.85f}, sh{0.0f, 0.0f, 0.0f, 0.55f};
        for (int pass = 0; pass < 2; ++pass) {
            const Rgba cc = pass ? c : sh;
            const float o = pass ? 0.0f : 1.0f;
            hud.line(cx - gap - arm + o, cy + o, cx - gap + o, cy + o, cc);
            hud.line(cx + gap + o, cy + o, cx + gap + arm + o, cy + o, cc);
            hud.line(cx + o, cy - gap - arm + o, cx + o, cy - gap + o, cc);
            hud.line(cx + o, cy + gap + o, cx + o, cy + gap + arm + o, cc);
        }
        hud.rect(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, c);
    }
    // Status chip bottom left: who, stance, view, rifle.
    const float speed = std::fabs(sd->speed());
    const char* stance = sd->airborne() ? "AIRBORNE"
                         : sd->stance() == 1 ? "CROUCHED"
                         : held_.boost && speed > 2.6f ? "SPRINTING"
                         : speed > 0.3f ? "WALKING" : "STANDING";
    char l2[160], l3[160];
    std::snprintf(l2, sizeof(l2), "%s   %s   %.1f M/S", stance, camera_.possessSight ? "1ST PERSON" : "3RD PERSON", speed);
    std::snprintf(l3, sizeof(l3), "RIFLE  FULL AUTO   AMMO  UNLIMITED%s", aiming_ ? "   AIMING" : "");
    const float x = 22.0f * s, y = H - 92.0f * s;
    const float pw = std::max(hud.textWidth(sd->name(), 18.0f * s), std::max(hud.textWidth(l2, 13.0f * s), hud.textWidth(l3, 13.0f * s))) + 28.0f * s;
    hud.roundRect(x - 10.0f * s, y - 8.0f * s, pw, 78.0f * s, 6.0f * s, Rgba{0.0f, 0.0f, 0.0f, 0.45f});
    hud.text(x, y, sd->name(), 18.0f * s, kAmber);
    hud.text(x, y + 26.0f * s, l2, 13.0f * s, kWhite);
    hud.text(x, y + 46.0f * s, l3, 13.0f * s, fireCooldown_ > 0.0f ? kAmber : kDim);
}

void App::drawPauseMenu(int w, int h) {
    Hud& hud = renderer_->hud();
    hud.flush();   // the menu covers every marker / label queued before it
    const PauseMenuLayout l = pauseMenuLayout(w, h);
    const float s = l.s, W = static_cast<float>(w), H = static_cast<float>(h);
    const float mx = menuMouseX_, my = menuMouseY_;
    hud.rect(0.0f, 0.0f, W, H, Rgba{0.0f, 0.0f, 0.0f, 0.5f});
    // Tabs.
    for (int i = 0; i < kMenuTabCount; ++i) {
        const UiRect& r = l.tabs[i];
        const MenuTab t = static_cast<MenuTab>(i);
        const bool active = t == menuTab_, hover = r.contains(mx, my);
        const Rgba bg = active ? Rgba{1.0f, 0.75f, 0.15f, 0.92f} : hover ? Rgba{0.16f, 0.18f, 0.22f, 0.92f} : Rgba{0.04f, 0.05f, 0.06f, 0.88f};
        hud.roundRect(r.x, r.y, r.w, r.h, 6.0f * s, bg);
        if (!active) hud.roundFrame(r.x, r.y, r.w, r.h, 6.0f * s, 1.0f * s, Rgba{1.0f, 1.0f, 1.0f, 0.18f});
        const char* label = menuTabLabel(t);
        const float px = 18.0f * s;
        const Rgba tc = active ? Rgba{0.05f, 0.05f, 0.06f, 1.0f} : t == MenuTab::Quit ? kRed : t == MenuTab::Resume ? kGreen : kWhite;
        hud.text(r.x + (r.w - hud.textWidth(label, px)) * 0.5f, r.y + (r.h - px) * 0.5f, label, px, tc);
    }
    // Panel.
    const UiRect& p = l.page;
    hud.roundRect(p.x, p.y, p.w, p.h, 10.0f * s, Rgba{0.02f, 0.025f, 0.032f, 0.93f});
    hud.roundFrame(p.x, p.y, p.w, p.h, 10.0f * s, 1.5f * s, Rgba{1.0f, 0.75f, 0.15f, 0.55f});
    // Close button (X).
    {
        const UiRect& c = l.guide.close;
        const bool hover = c.contains(mx, my);
        hud.roundRect(c.x, c.y, c.w, c.h, 6.0f * s, hover ? Rgba{0.6f, 0.15f, 0.1f, 0.9f} : Rgba{0.12f, 0.13f, 0.15f, 0.9f});
        const float k = c.w * 0.3f;
        hud.thickLine(c.x + k, c.y + k, c.x + c.w - k, c.y + c.h - k, 2.0f * s, kWhite);
        hud.thickLine(c.x + c.w - k, c.y + k, c.x + k, c.y + c.h - k, 2.0f * s, kWhite);
    }
    const DayCycle* clock = sa::dayCycle(*scene_);
    char buf[200];

    if (menuTab_ == MenuTab::Settings) {
        hud.text(l.settingsTitle.x, l.settingsTitle.y, "SETTINGS", 30.0f * s, kAmber);
        hud.text(l.settingsTitle.x + hud.textWidth("SETTINGS", 30.0f * s) + 20.0f * s, l.settingsTitle.y + 12.0f * s,
                 "applied at once, saved to ThermalSense.ini", 14.0f * s, kDim);
        for (size_t i = 0; i < l.rows.size(); ++i) {
            const SettingRowRect& r = l.rows[i];
            const bool sel = static_cast<int>(i) == menuRow_;
            const bool hover = r.row.contains(mx, my);
            hud.roundRect(r.row.x, r.row.y, r.row.w, r.row.h, 6.0f * s,
                          sel ? Rgba{1.0f, 0.75f, 0.15f, 0.16f} : hover ? Rgba{1.0f, 1.0f, 1.0f, 0.06f} : Rgba{1.0f, 1.0f, 1.0f, 0.025f});
            if (sel) hud.roundFrame(r.row.x, r.row.y, r.row.w, r.row.h, 6.0f * s, 1.3f * s, Rgba{1.0f, 0.75f, 0.15f, 0.8f});
            const bool dimRow = r.id == SettingId::FrameLimit && effectiveVsync();
            hud.text(r.row.x + 16.0f * s, r.row.y + 6.0f * s, settingLabel(r.id), 17.0f * s, dimRow ? kDim : kWhite);
            hud.text(r.row.x + 16.0f * s, r.row.y + 29.0f * s, dimRow ? "off while vsync is on" : settingHint(r.id), 11.5f * s, kDim);
            for (int k = 0; k < 2; ++k) {
                const UiRect& b = k ? r.plus : r.minus;
                const bool hb = b.contains(mx, my);
                hud.roundRect(b.x, b.y, b.w, b.h, 5.0f * s, hb ? Rgba{1.0f, 0.75f, 0.15f, 0.85f} : Rgba{0.14f, 0.15f, 0.18f, 0.95f});
                const char* g = k ? "+" : "-";
                hud.text(b.x + (b.w - hud.textWidth(g, 22.0f * s)) * 0.5f, b.y + (b.h - 22.0f * s) * 0.5f, g, 22.0f * s,
                         hb ? Rgba{0.05f, 0.05f, 0.06f, 1.0f} : kWhite);
            }
            hud.roundRect(r.value.x, r.value.y, r.value.w, r.value.h, 5.0f * s, Rgba{0.0f, 0.0f, 0.0f, 0.55f});
            const std::string v = settingValueText(gs_, r.id);
            hud.text(r.value.x + (r.value.w - hud.textWidth(v, 19.0f * s)) * 0.5f, r.value.y + (r.value.h - 19.0f * s) * 0.5f, v,
                     19.0f * s, dimRow ? kDim : sel ? kAmber : kWhite);
        }
        const UiRect& f = l.settingsFooter;
        hud.text(f.x + 16.0f * s, f.y + 8.0f * s, "UP DOWN select   LEFT RIGHT / click - + change   TAB next page   ESC resume", 14.0f * s, kDim);
        std::snprintf(buf, sizeof(buf), "file: %s", opt_.iniPath.empty() ? "(not saved in this mode)" : opt_.iniPath.c_str());
        hud.text(f.x + 16.0f * s, f.y + 32.0f * s, buf, 12.0f * s, kDim);
        if (!cliVsync_ && !cliFpsLimit_) {
            std::snprintf(buf, sizeof(buf), "display %.0f Hz   now: %s", displayHz_,
                          effectiveVsync() ? "vsync" : limitHz_ > 0.0f ? "frame limiter" : "uncapped");
        } else {
            std::snprintf(buf, sizeof(buf), "display %.0f Hz   the command line sets %s%s%s for this run (a change here takes over)", displayHz_,
                          cliVsync_ ? "--vsync" : "", cliVsync_ && cliFpsLimit_ ? " and " : "", cliFpsLimit_ ? "--fps-limit" : "");
        }
        hud.text(f.x + 16.0f * s, f.y + 52.0f * s, buf, 12.0f * s, kDim);
        return;
    }

    // Field guide pages: header, the three sections (the tab's own one highlighted), help.
    const GuideLayout& g = l.guide;
    const GuideSection focus = menuTabSection(menuTab_);
    hud.text(p.x + 24.0f * s, p.y + 18.0f * s, "PAUSED", 30.0f * s, kAmber);
    hud.text(p.x + 24.0f * s + hud.textWidth("PAUSED", 30.0f * s) + 18.0f * s, p.y + 30.0f * s,
             "FIELD GUIDE   travel anywhere, set the time of day, switch views", 14.0f * s, kDim);
    auto section = [&](const UiRect& r, const char* title, GuideSection sec) {
        const bool on = sec == focus;
        hud.roundRect(r.x, r.y, r.w, r.h, 8.0f * s, on ? Rgba{1.0f, 0.75f, 0.15f, 0.07f} : Rgba{1.0f, 1.0f, 1.0f, 0.02f});
        hud.roundFrame(r.x, r.y, r.w, r.h, 8.0f * s, (on ? 1.8f : 1.0f) * s, on ? Rgba{1.0f, 0.75f, 0.15f, 0.85f} : Rgba{1.0f, 1.0f, 1.0f, 0.15f});
        hud.text(r.x + 14.0f * s, r.y + 9.0f * s, title, 16.0f * s, on ? kAmber : kWhite);
    };
    section(g.travelBox, "TRAVEL", GuideSection::Travel);
    section(g.timeBox, "TIME OF DAY", GuideSection::Time);
    section(g.viewBox, "VIEW", GuideSection::View);
    // Help box.
    hud.roundRect(g.helpBox.x, g.helpBox.y, g.helpBox.w, g.helpBox.h, 8.0f * s, Rgba{1.0f, 1.0f, 1.0f, 0.02f});
    hud.text(g.helpBox.x + 14.0f * s, g.helpBox.y + 10.0f * s, "CLICK a button or press its key    TAB next page    ESC / G resume", 13.0f * s, kWhite);
    hud.text(g.helpBox.x + 14.0f * s, g.helpBox.y + 32.0f * s,
             menuNote_.empty() ? "In the game: E interact   TAB next unit   V view   P pause   F1 all keys" : menuNote_.c_str(), 12.0f * s,
             menuNote_.empty() ? kDim : kAmber);

    // Time read-out and slider.
    if (clock) {
        const UiRect& c = g.clock;
        const std::string hhmm = formatClock(clock->hour());
        hud.text(c.x + 14.0f * s, c.y + 4.0f * s, hhmm, 38.0f * s, kWhite);
        std::snprintf(buf, sizeof(buf), "%s   %s   SPEED x%g", clock->phaseName(), clock->autoRun() ? "AUTO" : "MANUAL",
                      static_cast<double>(guideSpeedFactor(cycleClock_ == clock ? guideSpeed_ : 0)));
        hud.text(c.x + 14.0f * s + hud.textWidth(hhmm, 38.0f * s) + 16.0f * s, c.y + 18.0f * s, buf, 13.0f * s, kDim);
        const UiRect& t = g.slider;
        for (int k = 0; k < 48; ++k) {   // night - dawn - day - dusk band
            const float hh = (static_cast<float>(k) + 0.5f) * 0.5f;
            const float day = clampf((std::min(hh - 5.5f, 18.5f - hh)) / 1.5f, 0.0f, 1.0f);
            const Rgba col{0.10f + 0.75f * day, 0.14f + 0.65f * day, 0.30f + 0.45f * day, 0.95f};
            hud.rect(t.x + t.w * static_cast<float>(k) / 48.0f, t.y, t.w / 48.0f + 0.5f, t.h, col);
        }
        hud.frame(t.x, t.y, t.w, t.h, Rgba{1.0f, 1.0f, 1.0f, 0.35f});
        for (int hh = 0; hh <= 24; hh += 6) {
            const float x = t.x + t.w * static_cast<float>(hh) / 24.0f;
            hud.line(x, t.y + t.h, x, t.y + t.h + 5.0f * s, kDim);
            std::snprintf(buf, sizeof(buf), "%02d", hh);
            hud.text(x - hud.textWidth(buf, 10.0f * s) * 0.5f, t.y + t.h + 6.0f * s, buf, 10.0f * s, kDim);
        }
        const float kx = guideSliderX(g, clock->hour()), ky = t.y + t.h * 0.5f;
        hud.disc(kx, ky, 10.0f * s, Rgba{0.0f, 0.0f, 0.0f, 0.6f});
        hud.disc(kx, ky, 8.0f * s, menuDrag_ ? kAmber : kWhite);
    }

    // Buttons.
    for (const GuideButton& b : g.buttons) {
        const UiRect& r = b.r;
        const bool hover = r.contains(mx, my);
        bool on = false, disabled = false;
        std::string state;
        switch (b.action) {
            case GuideAction::CycleAuto: on = clock && clock->autoRun(); state = on ? "ON" : "OFF"; break;
            case GuideAction::CycleSpeed:
                std::snprintf(buf, sizeof(buf), "x%g", static_cast<double>(guideSpeedFactor(cycleClock_ == clock ? guideSpeed_ : 0)));
                state = buf;
                break;
            case GuideAction::PersonToggle:
                state = controllingSoldier() ? (camera_.possessSight ? "1ST" : "3RD")
                        : camera_.mode == CameraMode::Pov ? povViewName(povView_) : camera_.mode == CameraMode::Possess ? (camera_.possessSight ? "SIGHT" : "CHASE") : "-";
                disabled = camera_.mode != CameraMode::Possess && camera_.mode != CameraMode::Pov;
                break;
            case GuideAction::RenderMode: state = rt::rtModeName(settings_.rtMode); break;
            case GuideAction::Thermal: on = settings_.view == ViewMode::Thermal; state = on ? "ON" : "OFF"; break;
            case GuideAction::HudToggle: state = !showHud_ ? "OFF" : debugHud_ ? "DEBUG" : "GAME"; break;
            case GuideAction::Feeds: on = showFeed_; state = on ? "ON" : "OFF"; break;
            case GuideAction::Rover: disabled = sourceOfKind(listPovSources(*scene_, shownPose_.eye), PovKind::Rover) < 0; break;
            default: break;
        }
        if (b.section == GuideSection::Time && guidePresetHour(b.action) >= 0.0f && clock)
            on = std::fabs(std::fmod(clock->hour() - guidePresetHour(b.action) + 36.0f, 24.0f) - 12.0f) > 11.95f && !clock->autoRun();
        const bool focusSec = b.section == focus;
        Rgba bg = hover ? Rgba{0.22f, 0.24f, 0.28f, 0.95f} : Rgba{0.10f, 0.11f, 0.13f, 0.95f};
        if (on) bg = Rgba{0.12f, 0.32f, 0.18f, 0.95f};
        hud.roundRect(r.x, r.y, r.w, r.h, 6.0f * s, bg);
        hud.roundFrame(r.x, r.y, r.w, r.h, 6.0f * s, 1.0f * s,
                       hover ? Rgba{1.0f, 0.75f, 0.15f, 0.9f} : focusSec ? Rgba{1.0f, 0.75f, 0.15f, 0.35f} : Rgba{1.0f, 1.0f, 1.0f, 0.12f});
        const char* key = guideActionKey(b.action);
        const float kc = std::min(26.0f * s, r.h - 10.0f * s);
        const float kx = r.x + 8.0f * s, ky = b.section == GuideSection::Travel ? r.y + 10.0f * s : r.y + (r.h - kc) * 0.5f;
        if (*key) {
            hud.roundRect(kx, ky, kc, kc, 4.0f * s, Rgba{1.0f, 0.82f, 0.25f, disabled ? 0.35f : 0.92f});
            hud.text(kx + (kc - hud.textWidth(key, 15.0f * s)) * 0.5f, ky + (kc - 15.0f * s) * 0.5f, key, 15.0f * s, Rgba{0.05f, 0.05f, 0.06f, 1.0f});
        }
        const Rgba lc = disabled ? kDim : kWhite;
        const float tx = kx + kc + 10.0f * s;
        if (b.section == GuideSection::Travel) {
            hud.text(tx, r.y + 13.0f * s, b.label, 16.0f * s, lc);
            hud.text(r.x + 10.0f * s, r.y + 50.0f * s, b.sub, 11.5f * s, kDim);
            if (disabled) hud.text(r.x + 10.0f * s, r.y + 72.0f * s, "NONE ON THE FIELD", 11.0f * s, kAmber);
        } else if (*b.sub) {   // time presets: name + hour
            hud.text(tx, r.y + 6.0f * s, b.label, 13.0f * s, lc);
            hud.text(tx, r.y + 24.0f * s, b.sub, 12.0f * s, kDim);
        } else {
            hud.text(tx, r.y + (r.h - 14.0f * s) * 0.5f, b.label, 14.0f * s, lc);
            if (!state.empty())
                hud.text(r.right() - 10.0f * s - hud.textWidth(state, 13.0f * s), r.y + (r.h - 13.0f * s) * 0.5f, state, 13.0f * s,
                         on ? kGreen : kAmber);
        }
    }
}

}  // namespace ts
