// Non-interactive modes: scripted screenshot capture (for the report) and the performance benchmark.
// Both run the real simulation (UAV camera, tracker, rover planner) with a fixed 60 Hz time step.
#include <glad/gl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <utility>

#include "app/App.h"
#include "app/CaptureLab.h"
#include "app/CaptureRT.h"
#include "app/HeadlessGL.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "objects/BaseLayout.h"
#include "scene/Director.h"
#include "scene/HudMath.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/Sphere.h"
#include "rendering/ImageWriter.h"

namespace ts {

namespace {

constexpr float kStep = 1.0f / 60.0f;

enum class Follow { None, Uav, Rover, Bunker, Action, Soldier };
// Event-driven shots (v5): the game is emergent, so these shots wait for a game moment instead of a fixed time.
// v6: Withdraw (a rover leaves at dawn), Shift (shift-change event), Persons (UAV 0's CV sees a person blob).
// Status: soldier(s) whose status() contains Shot::waitStatus (the mechanic at the tank, a crate carried, soldiers seated).
enum class Trig { None, Launch, Inbound, Combined, Hit, Kill, Wave, Tracks2, FpvTracks, Withdraw, Shift, Persons, Status };

const char* eventName(GameEventType t) {
    static const char* names[] = {"WaveStart", "EnemySpotted", "LockOn", "RoundLaunched", "CombinedStrike", "Explosion", "EnemyHit",
                                  "EnemyDestroyed", "Breach", "BaseLost", "CannonFire", "GatlingBurst", "DoorMoving", "Dusk",
                                  "NightFalls", "Dawn", "DayBreak", "ShiftChange", "Withdraw"};
    const int k = static_cast<int>(t);
    return k >= 0 && k < static_cast<int>(sizeof(names) / sizeof(names[0])) ? names[k] : "other";
}

struct Shot {
    const char* file;
    float time;                 // simulation time of the shot
    ViewMode view = ViewMode::Thermal;
    EdgeMode edges = EdgeMode::Solid;
    ShadingOverride shading = ShadingOverride::PerObject;
    bool depthTest = true, exploded = false, plan = false, axes = false, hud = true, feed = true;
    bool library = false;       // the three primitives alone (report figure 1)
    const char* isolate = "";   // draw a single composite object
    float director = -1.0f;     // >= 0: cinematic director camera at this time in the sequence
    bool cinematic = true;      // false: the v1 renderer (before/after figure)
    int palette = 0;            // thermal colour map
    bool rain = false;          // full rain, ground soaked
    const char* without = "";   // feature switched off (shadows, ssao, fog, bloom) for the comparison figures
    CameraMode cam = CameraMode::Orbit;
    bool fpv = false;           // thermal view through the UAV gimbal camera, full screen (first person)
    float fov = 40.0f;          // first-person thermal display field of view
    float walkX = 0, walkZ = 12, walkYaw = 180, walkPitch = 0;   // CameraMode::Walk
    Follow follow = Follow::Action;
    float yaw = 100, pitch = 30, dist = 24;
    int width = 1600, height = 900;
    // v4: free camera at fixed world positions (or relative to the strike point), and scripted events.
    bool custom = false;        // camera at eye -> aim (CameraMode::Free), fov camFov
    bool rel = false;           // eye / aim are offsets from the strike point (position of the first kill)
    Vec3 eye{0, 0, 0}, aim{0, 0, 0};
    float camFov = 50.0f;
    float walkFeet = -1e9f;     // walk camera: floor level to start from (default: terrain)
    const char* possessName = "";  // take control of the controllable whose name contains this (SAM-1, MLRS-1)
    int possess = -1;           // Slot to take control of (HUD drawn, chase or sight view)
    bool sight = false;
    Trig trigger = Trig::None;  // wait (up to 'timeout' s) for this game moment, then 'delay' more seconds
    float delay = 0.0f, timeout = 60.0f;
    float afterImpact = -1.0f;  // >= 0: taken this many seconds after the missile of the scripted strike detonated
    const char* lab = "";       // v5 lab figure (app/CaptureLab.cpp): cv, materials, shading, relief, reflect, buffers
    // v6
    float hour = -1.0f;         // >= 0: set the clock to this hour (manual) before the shot, then 'settle' s of simulation
    float settle = 2.0f;        // s simulated after a clock change (lighting, soldiers walking to their posts)
    bool autoClock = false;     // let the clock run (dawn / dusk shots that wait for the phase events)
    int soldier = -1;           // Follow::Soldier target / soldier to possess (roster index, Garrison order)
    bool v6 = false;            // v6 figure (an --only list of v6 shots skips the v5 sequence)
    const char* waitStatus = "";  // Trig::Status: text in Soldier::status() ('soldier' only when >= 0, else any soldier)
    int waitCount = 1;          // Trig::Status: how many soldiers must match
    bool aimSun = false;        // custom camera: eye -> eye + sun direction (+ aim as an offset): the sunset figure
    // v7 hero close-ups: 'isolate' alone, orbit camera at 'yaw' degrees from the object's front (+Z of its root), the
    // bounding sphere filling 'heroFill' of the frame height (computed from the live leaves: no hand-placed camera).
    bool hero = false;
    float heroFill = 0.7f;
    float exposure = 1.0f;      // RenderSettings::exposure (the hero objects alone against the bright day sky: +1 EV)
};

// The scripted sequence (directorScript) starts on the simulation clock at kScriptStart: shots with
// time = kScriptStart + t see the world exactly as the video does t seconds into the director sequence.
constexpr float kScriptStart = 60.0f;

Shot isolated(const char* file, const char* object, Follow f, float dist, float yaw, float pitch, bool exploded) {
    Shot s{file, 30.0f};
    s.view = ViewMode::Diagnostic;
    s.isolate = object;
    s.follow = f;
    s.dist = dist;
    s.yaw = yaw;
    s.pitch = pitch;
    s.exploded = exploded;
    s.hud = false;
    s.width = 1200;
    s.height = 900;
    return s;
}

std::vector<Shot> shotList() {
    std::vector<Shot> s;
    auto add = [&](Shot shot) { s.push_back(shot); };
    Shot lib{"primitive_library", 44.0f}; lib.library = true; lib.width = 2000; lib.height = 560; add(lib);
    Shot a{"scene_thermal", 44.0f};
    add(a);
    Shot aw = a; aw.file = "scene_thermal_whitehot"; aw.palette = 1; add(aw);
    Shot ar = a; ar.file = "scene_thermal_rainbow"; ar.palette = 2; add(ar);
    Shot fp{"thermal_footprints", 44.0f}; fp.follow = Follow::Rover; fp.dist = 9.0f; fp.pitch = 58; fp.yaw = 150;
    fp.feed = false; add(fp);
    Shot b = a; b.file = "scene_visible"; b.view = ViewMode::Visible; add(b);
    Shot c{"uav_closeup_visible", 44.0f}; c.view = ViewMode::Visible; c.follow = Follow::Uav; c.dist = 2.4f; c.pitch = 12; c.yaw = 140; c.hud = false; add(c);
    Shot d = c; d.file = "uav_closeup_thermal"; d.view = ViewMode::Thermal; add(d);
    Shot e{"scene_primitives_exploded", 44.0f}; e.view = ViewMode::Diagnostic; e.exploded = true; e.feed = false; add(e);
    add(isolated("iso_uav_assembled", "Autonomous UAV", Follow::Uav, 0.95f, 150, 22, false));
    add(isolated("iso_uav_exploded", "Autonomous UAV", Follow::Uav, 1.2f, 150, 22, true));
    add(isolated("iso_rover_assembled", "Target Rover", Follow::Rover, 1.7f, 35, 28, false));
    add(isolated("iso_rover_exploded", "Target Rover", Follow::Rover, 2.1f, 35, 28, true));
    add(isolated("iso_bunker_assembled", "Command Bunker", Follow::Bunker, 8.5f, 40, 25, false));
    add(isolated("iso_bunker_exploded", "Command Bunker", Follow::Bunker, 12.0f, 40, 22, true));
    Shot i{"scene_xray", 44.0f}; i.edges = EdgeMode::XRay; i.view = ViewMode::Visible; i.feed = false; add(i);
    Shot j{"scene_depth", 44.0f}; j.view = ViewMode::Depth; j.feed = false; add(j);
    Shot k{"scene_hiddenline", 44.0f}; k.edges = EdgeMode::HiddenLine; k.view = ViewMode::Visible; k.feed = false; add(k);
    Shot m{"uav_camera_view", 44.0f}; m.cam = CameraMode::UavCamera; add(m);
    Shot n{"bunker_flat_visible", 44.0f}; n.view = ViewMode::Visible; n.follow = Follow::Bunker; n.dist = 8.0f; n.pitch = 22; n.yaw = 50; n.hud = false; add(n);
    Shot o = n; o.file = "bunker_all_gouraud"; o.shading = ShadingOverride::Gouraud; add(o);
    Shot p = n; p.file = "bunker_all_phong"; p.shading = ShadingOverride::Phong; add(p);
    Shot q{"hierarchy_axes", 44.0f}; q.view = ViewMode::Visible; q.axes = true; q.follow = Follow::Uav; q.dist = 2.2f; q.pitch = 15; q.yaw = 120; q.hud = false; add(q);
    for (const auto& [name, dt] : std::initializer_list<std::pair<const char*, float>>{
             {"cine_establishing", 6.0f}, {"cine_gate", 2.5f}, {"cine_chase", 34.0f}, {"cine_ground", 41.0f},
             {"cine_watchtower", 70.0f}, {"cine_thermal_wide", 103.0f}, {"cine_low_angle", 95.0f}}) {  // v4 director times
        Shot c{name, 44.0f};
        c.director = dt;
        c.width = 1920;
        c.height = 1080;
        add(c);
    }
    // Rain: the same cinematic shots and the thermal view in a downpour.
    for (const auto& [name, dt] : std::initializer_list<std::pair<const char*, float>>{
             {"rain_gate", 2.5f}, {"rain_chase", 34.0f}, {"rain_low_angle", 95.0f}}) {
        Shot c{name, 44.0f};
        c.director = dt;
        c.width = 1920;
        c.height = 1080;
        c.rain = true;
        add(c);
    }
    Shot rt{"rain_thermal", 44.0f}; rt.rain = true; add(rt);
    // First-person thermal (FLIR) view: the full screen is the UAV's gimbal camera.
    Shot tf{"thermal_fpv", 44.0f}; tf.fpv = true; tf.width = 1920; tf.height = 1080; add(tf);
    Shot tw = tf; tw.file = "thermal_fpv_whitehot"; tw.palette = 1; add(tw);
    Shot tz = tf; tz.file = "thermal_fpv_zoom"; tz.fov = 14.0f; tz.palette = 3; add(tz);
    // First-person walk (eye height 1.7 m): the scene from where a player would stand.
    auto walk = [&](const char* file, float x, float z, float yaw, float pitch, bool rain) {
        Shot w{file, 44.0f};
        w.view = ViewMode::Visible;
        w.cam = CameraMode::Walk;
        w.walkX = x;
        w.walkZ = z;
        w.walkYaw = yaw;
        w.walkPitch = pitch;
        w.hud = false;
        w.feed = false;
        w.rain = rain;
        w.width = 1920;
        w.height = 1080;
        add(w);
    };
    walk("walk_bunker", -3.5f, 11.0f, 165.0f, 2.0f, false);
    walk("walk_forest", 9.0f, -6.0f, 250.0f, 4.0f, false);
    walk("walk_meadow", -12.0f, 4.0f, 110.0f, -2.0f, false);
    walk("walk_rain_puddles", 1.5f, 14.0f, 175.0f, -8.0f, true);
    // Before/after and one-feature-off comparisons, all from the same camera.
    const std::pair<const char*, const char*> variants[] = {{"compare_v1", "v1"},       {"compare_v2", ""},
                                                            {"ablate_shadows", "shadows"}, {"ablate_ssao", "ssao"},
                                                            {"ablate_fog", "fog"},         {"ablate_bloom", "bloom"}};
    for (const auto& [name, off] : variants) {
        Shot v{name, 44.0f};
        v.view = ViewMode::Visible;
        v.follow = Follow::Bunker;
        v.dist = 10.0f;
        v.yaw = 35;
        v.pitch = 18;
        v.hud = false;
        v.feed = false;
        if (std::string(off) == "v1") v.cinematic = false;
        else v.without = off;
        add(v);
    }
    // t = 52 s: the rover has slipped out of sight (trace: hidden, REACQUIRE) and the UAV is circling back.
    Shot r{"hide_thermal", 52.0f}; r.follow = Follow::Rover; r.yaw = 200; r.pitch = 30; r.dist = 11; add(r);
    Shot t = r; t.file = "hide_ztest_off"; t.depthTest = false; t.feed = false; add(t);
    Shot th{"thermal_fpv_hidden", 52.0f}; th.fpv = true; th.width = 1920; th.height = 1080; add(th);
    Shot u{"plan_topdown", 52.0f}; u.plan = true; u.view = ViewMode::Visible; u.pitch = 85; u.yaw = 0; u.dist = 44; u.feed = false; u.follow = Follow::None; add(u);

    // ---- v4: the military base (1920x1080, visible, no HUD unless noted) --------------------------------------------
    using namespace layout;
    auto v4 = [&](const char* file, float time) {
        Shot x{file, time};
        x.view = ViewMode::Visible;
        x.hud = false;
        x.feed = false;
        x.width = 1920;
        x.height = 1080;
        return x;
    };
    auto cam = [&](Shot x, Vec3 eye, Vec3 aim, float fov) {
        x.custom = true;
        x.eye = eye;
        x.aim = aim;
        x.camFov = fov;
        return x;
    };
    auto walkAt = [&](Shot x, float px, float pz, float yaw, float pitch, float feet = -1e9f) {
        x.cam = CameraMode::Walk;
        x.walkX = px;
        x.walkZ = pz;
        x.walkYaw = yaw;
        x.walkPitch = pitch;
        x.walkFeet = feet;
        return x;
    };
    // Static views before the script starts (t < kScriptStart).
    add(cam(v4("base_overview", 55.0f), {-30.0f, 17.0f, 44.0f}, {2.0f, 0.5f, 4.0f}, 56.0f));
    add(cam(v4("base_gate", 56.0f), {7.0f, 1.9f, 46.5f}, {0.0f, 1.6f, 34.0f}, 52.0f));
    add(cam(v4("helipad", 57.0f), {12.0f, 3.2f, 25.0f}, {20.3f, 1.2f, 17.0f}, 50.0f));  // B2 proposal
    add(cam(v4("radar", 58.0f), {21.0f, 6.5f, -12.5f}, {29.0f, 5.0f, -3.0f}, 50.0f));  // B2 proposal
    // The old camera (-12, 2.2, -4) looked into the west wood (two pines filled the frame); this one looks down on the park.
    // (Tried and rejected: (-32.5, 2.4, -10.5) is inside the HESCO wall, (-13, 2.2, -6) is behind pines.)
add(cam(v4("motor_pool", 58.5f), {-14.0f, 7.5f, -6.0f}, kMotorPool + Vec3{0.0f, 0.8f, 0.0f}, 50.0f));  // raised: the background is the park, not the west wood
    add(cam(v4("tank_closeup", 59.0f), {-14.2f, 1.3f, -13.9f}, {-22.0f, 1.2f, -16.0f}, 44.0f));  // B1 proposal
    // C5 hidden camp (static, before the script, HUD off).
    add(cam(v4("camp_sam", 56.5f), {16.5f, 2.0f, 1.0f}, {25.1f, 2.2f, 6.7f}, 55.0f));
    add(cam(v4("camp_mlrs", 56.6f), {-4.5f, 2.5f, 17.0f}, {-11.5f, 2.0f, 22.5f}, 55.0f));
    add(cam(v4("camp_under_nets", 56.7f), {-2.0f, 4.5f, -17.0f}, {9.5f, 1.5f, -23.8f}, 60.0f));
    // Inside the bunker (walk camera, 1.7 m eye height on the WalkSpace floors). Doors open at script time 10.5 s.
    add(walkAt(v4("bunker_ramp", kScriptStart + 8.0f), 0.0f, 11.5f, 180.0f, -10.0f));
    add(walkAt(v4("bunker_door", kScriptStart + 10.0f), 0.0f, 7.0f, 180.0f, 0.0f));
    add(walkAt(v4("bunker_tunnel", kScriptStart + 20.0f), 0.0f, 2.5f, 180.0f, 0.0f, kBunkerFloorY));
    add(walkAt(v4("bunker_cic", kScriptStart + 21.0f), 0.0f, -2.0f, 160.0f, -3.0f, kBunkerFloorY));
    Shot possess_doors = v4("possess_doors", kScriptStart + 21.5f);
    possess_doors.possess = static_cast<int>(Slot::Doors);
    possess_doors.sight = false;
    possess_doors.hud = true;
    add(possess_doors);
    // Scripted controllables: the director's inputs drive the helicopter (15-28 s), tank (28-40 s), AA gun (40-46 s).
    Shot ht = v4("heli_takeoff", kScriptStart + 22.0f);
    ht.director = 22.0f;
    add(ht);
    Shot possess_heli_cockpit = v4("possess_heli_cockpit", kScriptStart + 24.0f);
    possess_heli_cockpit.possess = static_cast<int>(Slot::Helicopter);
    possess_heli_cockpit.sight = true;
    possess_heli_cockpit.hud = true;
    add(possess_heli_cockpit);
    Shot possess_tank_sight = v4("possess_tank_sight", kScriptStart + 33.0f);
    possess_tank_sight.possess = static_cast<int>(Slot::Tank);
    possess_tank_sight.sight = true;
    possess_tank_sight.hud = true;
    add(possess_tank_sight);
    Shot tf2 = v4("tank_fire", kScriptStart + 36.7f);
    tf2.director = 36.7f;
    add(tf2);
    Shot ag = v4("aa_gun_burst", kScriptStart + 42.5f);
    ag.director = 42.5f;
    add(ag);
    Shot possess_aa_sight = v4("possess_aa_sight", kScriptStart + 43.0f);
    possess_aa_sight.possess = static_cast<int>(Slot::AaGun);
    possess_aa_sight.sight = true;
    possess_aa_sight.hud = true;
    add(possess_aa_sight);
    Shot ags = v4("aa_gun_sight", kScriptStart + 44.5f);
    ags.director = 44.5f;
    add(ags);
    Shot possess_uav_manual = v4("possess_uav_manual", kScriptStart + 50.0f);
    possess_uav_manual.possess = static_cast<int>(Slot::Uav);
    possess_uav_manual.sight = true;
    possess_uav_manual.hud = true;
    add(possess_uav_manual);
    // C5 hidden camp (static time before the script, HUD off) and the launchers under player control.
    Shot ps = v4("possess_sam", kScriptStart + 12.0f);
    ps.possessName = "SAM-1";
    ps.hud = true;
    add(ps);
    Shot pm = v4("possess_mlrs", kScriptStart + 12.1f);
    pm.possessName = "MLRS-1";
    pm.hud = true;
    add(pm);
    // v5 game moments (event-driven, all with the game HUD): the squad plays the strikes, the shots wait for them.
    auto game = [&](const char* file, Trig trig, float delay, bool wide = true) {
        Shot x = v4(file, 0.0f);
        x.hud = true;
        x.feed = false;
        x.trigger = trig;
        x.delay = delay;
        x.view = ViewMode::Visible;
        if (wide) x.follow = Follow::Action;
        return x;
    };
    Shot ml = game("uav_missile_launch", Trig::Launch, 0.25f, false);
    ml.follow = Follow::Uav;
    ml.dist = 7.0f;
    ml.yaw = 140;
    ml.pitch = 12;
    add(ml);
    Shot go = cam(game("game_overview", Trig::Inbound, 0.4f), {-13.0f, 9.0f, 16.0f}, {0.0f, 1.2f, 0.0f}, 56.0f);  // wave running, both UAVs engaged, rounds in flight
    go.rel = true;
    add(go);
    Shot cs = game("combined_strike", Trig::Inbound, 0.9f);  // wide cinematic angle: both drones and both rounds converge
    cs.director = 84.0f;
    add(cs);
    Shot hm = cam(game("hit_marker", Trig::Hit, 0.08f), {5.0f, 2.8f, 6.5f}, {0.0f, 0.6f, 0.0f}, 50.0f);
    hm.rel = true;
    add(hm);
    Shot sf = game("thermal_fpv_strike", Trig::Kill, -0.0f);
    sf.director = 106.0f;
    sf.afterImpact = 0.0f;
    sf.width = 1920;
    add(sf);
    Shot e1 = cam(game("explosion_early", Trig::None, 0.0f), {5.0f, 1.6f, 6.5f}, {0.0f, 1.2f, 0.0f}, 46.0f);
    e1.rel = true;
    e1.hud = false;
    e1.afterImpact = 0.15f;
    add(e1);
    Shot e2 = cam(game("explosion_mid", Trig::None, 0.0f), {7.0f, 2.2f, 8.5f}, {0.0f, 1.8f, 0.0f}, 48.0f);
    e2.rel = true;
    e2.hud = false;
    e2.afterImpact = 1.0f;
    add(e2);
    Shot e3 = cam(game("explosion_smoke", Trig::None, 0.0f), {10.0f, 3.0f, 12.0f}, {0.0f, 4.0f, 0.0f}, 52.0f);
    e3.rel = true;
    e3.hud = false;
    e3.afterImpact = 5.0f;
    add(e3);
    Shot e4 = cam(game("wreck_burning", Trig::None, 0.0f), {2.4f, 0.9f, 2.8f}, {0.0f, 0.4f, 0.0f}, 46.0f);
    e4.rel = true;
    e4.hud = false;
    e4.afterImpact = 9.0f;
    add(e4);
    Shot kb = cam(game("kill_banner", Trig::Kill, 0.5f), {6.0f, 3.0f, 8.0f}, {0.0f, 0.8f, 0.0f}, 52.0f);
    kb.rel = true;
    add(kb);
    Shot tm = game("tactical_map", Trig::Tracks2, 1.0f);  // top-down-ish with the mini-map
    tm.cam = CameraMode::Orbit;
    tm.follow = Follow::None;
    tm.pitch = 78;
    tm.yaw = 0;
    tm.dist = 58;
    tm.view = ViewMode::Thermal;
    add(tm);
    Shot tt = game("thermal_fpv_tracks", Trig::FpvTracks, 0.0f);  // FPV with several track boxes
    tt.fpv = true;
    tt.view = ViewMode::Thermal;
    tt.fov = 40.0f;
    add(tt);
    Shot wb = game("wave_banner", Trig::Wave, 0.8f);  // the next wave announces itself
    wb.timeout = 70.0f;
    wb.follow = Follow::None;
    wb.custom = true;
    wb.eye = {-26.0f, 17.0f, 40.0f};
    wb.aim = {-2.0f, 0.5f, 6.0f};
    wb.camFov = 56.0f;
    add(wb);
    // ---- v6: day / night, soldiers, sensor noise (after the v5 sequence; the clock is set per shot) ---------------------
    auto v6 = [&](Shot x, float hour, float settle = 2.0f) {
        x.v6 = true;
        x.hour = hour;
        x.settle = settle;
        return x;
    };
    auto soldierView = [&](Shot x, int soldier, float dist, float yaw, float pitch) {
        x.follow = Follow::Soldier;
        x.soldier = soldier;
        x.dist = dist;
        x.yaw = yaw;
        x.pitch = pitch;
        return x;
    };
    {   // UAV 1 close up with its searchlight cone down to the ground (the overview hid the beams: UAVs too small)
        Shot ns = v6(v4("night_searchlights", 0.0f), 23.0f, 10.0f);
        ns.follow = Follow::Uav;
        ns.dist = 10.0f;
        ns.yaw = 200;
        ns.pitch = 32;  // from above and behind: the UAV, its cone and the lit pool on the ground
        add(ns);
    }
    {
        Shot pr = v6(v4("plan_routes", 0.0f), 23.0f);  // top-down: rovers on their via-point routes, flood field
        pr.plan = true;
        pr.view = ViewMode::Thermal;
        pr.hud = true;
        pr.follow = Follow::None;
        pr.pitch = 85;
        pr.yaw = 0;
        pr.dist = 50;
        pr.trigger = Trig::Tracks2;
        pr.timeout = 40.0f;
        add(pr);
    }
    {
        Shot ln{"lab_cv_noise", 0.0f};
        ln.lab = "cvnoise";
        ln.v6 = true;
        ln.hour = 23.0f;
        add(ln);
    }
    {
        Shot dw = v6(cam(v4("dawn_withdraw", 0.0f), {-40.0f, 15.0f, 12.0f}, {8.0f, 3.0f, 2.0f}, 56.0f), 5.4f, 0.5f);
        dw.autoClock = true;  // the clock runs into dawn; the shot waits for the first rover to withdraw
        dw.trigger = Trig::Withdraw;
        dw.timeout = 90.0f;
        dw.delay = 1.0f;
        dw.hud = true;
        add(dw);
    }
    // 70 s of day before the first day shot: the day shift walks from the bunker / night posts to its work places.
    add(v6(cam(v4("day_overview", 0.0f), {-30.0f, 17.0f, 44.0f}, {2.0f, 0.5f, 4.0f}, 56.0f), 9.5f, 70.0f));
    add(v6(cam(v4("sky_noon", 0.0f), {-9.0f, 1.8f, 30.0f}, {-1.0f, 14.0f, -10.0f}, 70.0f), 12.0f));  // left of the lamp pole
    {
        Shot ps = v6(v4("day_patrol_spline", 0.0f), 10.0f, 6.0f);  // the patrol on its spline route, plan overlay on
        ps.plan = true;
        add(soldierView(ps, 6, 26.0f, 150, 55));  // high: the whole curved route around the soldier
    }
    {   // the shots wait for the work itself (the mechanic walks ~45 s between the tank and the helicopter)
        Shot me = soldierView(v6(v4("day_mechanic", 0.0f), 10.5f, 1.0f), 3, 5.5f, 45, 30);  // from the open (camp) side of the bay
        me.trigger = Trig::Status;
        me.waitStatus = "WORKING AT MBT-2";
        me.timeout = 150.0f;
        me.delay = 1.0f;
        add(me);
        Shot lo = soldierView(v6(v4("day_logistics", 0.0f), 11.0f, 1.0f), 4, 5.5f, 270, 18);
        lo.trigger = Trig::Status;
        lo.waitStatus = "CARRYING CRATE";
        lo.timeout = 120.0f;
        lo.delay = 2.0f;
        add(lo);
    }
    add(soldierView(v6(v4("soldier_closeup", 0.0f), 11.0f, 0.5f), 0, 2.4f, 20, 8));  // front (the sentry faces the gate road), right of the lamp pole
    {
        Shot sa2 = soldierView(v6(v4("soldier_axes", 0.0f), 11.0f, 0.5f), 0, 2.4f, 310, 10);
        sa2.axes = true;
        add(sa2);
        Shot se = v6(isolated("soldier_exploded", "Soldier 1", Follow::Soldier, 3.0f, 330, 12, true), 11.0f, 0.5f);
        se.soldier = 0;
        se.hero = true;  // fitted to the exploded parts (bounds after the explode offsets), front three-quarter
        se.yaw = 25;
        se.pitch = 12;
        se.heroFill = 0.8f;
        se.width = 1920;
        se.height = 1080;
        add(se);
    }
    {
        Shot tp = v6(v4("soldier_possess_tp", 0.0f), 11.5f, 0.5f);
        tp.soldier = 6;
        tp.hud = true;
        add(tp);
        Shot fp2 = tp;
        fp2.file = "soldier_possess_fp";
        fp2.sight = true;
        add(fp2);
    }
    {
        Shot dt = v6(v4("day_thermal_fpv_persons", 0.0f), 13.0f, 2.0f);  // UAV 0's gimbal: soldiers boxed PERS, no target
        dt.fpv = true;
        dt.view = ViewMode::Thermal;
        dt.hud = true;
        dt.trigger = Trig::Persons;
        dt.timeout = 60.0f;
        add(dt);
    }
    {   // high on the east side, looking straight into the setting sun over the camp (aim follows DayCycle::sunDirection)
        Shot ss = v6(cam(v4("sky_sunset", 0.0f), {30.0f, 10.0f, 6.0f}, {0.0f, -6.0f, 0.0f}, 64.0f), 17.75f);
        ss.aimSun = true;
        add(ss);
    }
    {
        Shot sc = v6(cam(v4("dusk_shift_change", 0.0f), {36.0f, 10.0f, 14.0f}, {-6.0f, 2.0f, 6.0f}, 54.0f), 17.3f, 0.5f);
        sc.autoClock = true;  // runs into dusk: the shot waits for the shift-change event
        sc.trigger = Trig::Shift;
        sc.timeout = 60.0f;
        sc.delay = 1.5f;  // the DUSK / SHIFT CHANGE banner (3.5 s) is still up, the soldiers have started walking
        sc.hud = true;
        add(sc);
    }
    {
        // Off-duty soldiers seated in the bunk room (mess bench, bunks), seen from its east wall; waits until they sit.
        Shot bo = cam(v6(v4("bunker_offduty", 0.0f), 21.0f, 1.0f), {-1.95f, kBunkerFloorY + 1.7f, 0.7f},
                      {-4.6f, kBunkerFloorY + 0.5f, 1.3f}, 74.0f);
        bo.trigger = Trig::Status;
        bo.waitStatus = "OFF DUTY";
        bo.waitCount = 3;
        bo.timeout = 150.0f;
        bo.delay = 1.0f;
        add(bo);
    }
    // ---- v7 hero close-ups (WP-M detail; the camp nets hide it in the scene shots): each object alone, 10:30 daylight,
    // front three-quarter view filling ~70 % of the frame. soldier_front is the last shot (M's soldier pass comes later).
    for (const auto& [name, object] : std::initializer_list<std::pair<const char*, const char*>>{
             {"iso_tank", "Main Battle Tank"}, {"iso_heli", "Scout Helicopter"}, {"iso_aa", "AA Gatling Gun"},
             {"iso_sam", "SAM launcher"}, {"iso_mlrs", "MLRS rocket truck"}, {"iso_uav", "Autonomous UAV"},
             {"soldier_front", "Soldier 1"}}) {
        Shot h = v6(v4(name, 0.0f), 10.5f, 2.0f);
        h.isolate = object;
        h.hero = true;
        h.exposure = 2.0f;
        h.follow = Follow::None;
        h.yaw = 35;
        h.pitch = 16;
        if (std::string(name) == "soldier_front") {
            h.yaw = 20;
            h.pitch = 8;
        }
        add(h);
    }
    // ---- v5 lab figures (technique explanations rendered by the program). ./ts --capture DIR --only lab_cv,lab_materials,...
    // (an --only list made only of lab_* shots runs on a fresh simulation, independent of the other shots).
    for (const auto& [name, kind] : std::initializer_list<std::pair<const char*, const char*>>{
             {"lab_reflect", "reflect"}, {"lab_buffers", "buffers"}, {"lab_materials", "materials"}, {"lab_shading", "shading"},
             {"lab_relief", "relief"}, {"lab_cv", "cv"}, {"explorer", "explorer"}}) {  // explorer: v7 Frame Explorer (WP-I)
        Shot l{name, 0.0f};
        l.lab = kind;
        add(l);
    }
    // v7 WP-H ray-tracing figures (app/CaptureRT.cpp): the day set at 16:30 (long sun shadows) and a night set at 23:00
    // (moon, searchlights, lamps) written to DIR/rt_night/ under the same file names.
    for (const char* name : {"raytrace_modes", "raytrace_tree", "raytrace_ssr", "raytrace_photons", "raytrace_perf",
                             "raytrace_modes_night", "raytrace_tree_night", "raytrace_ssr_night", "raytrace_photons_night",
                             "raytrace_perf_night"}) {
        Shot l{name, 0.0f};
        l.lab = "rt";
        l.hour = std::string(name).find("_night") != std::string::npos ? 23.0f : 16.5f;
        add(l);
    }
    return s;
}

// Bounds of one instance of a composite object (leaves under the topmost ancestor of the first visible leaf named
// 'object'; the two UAVs share a name) and the forward direction (+Z) of that root.
bool objectBounds(const Scene& scene, const std::string& object, Vec3& centre, float& radius, Vec3& forward,
                  std::vector<Vec3>* corners = nullptr) {
    const SceneNode* root = nullptr;
    auto rootOf = [&](const SceneNode* n) {
        while (n->parent && n->parent->object == object) n = n->parent;
        return n;
    };
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (const SceneNode* n : scene.leaves()) {
        if (n->object != object || !n->visible) continue;
        if (n->world.m[13] < -6.0f) continue;  // parts parked below the ground (spent casings, idle rounds), not drawn in view
        if (!root) root = rootOf(n);
        if (rootOf(n) != root) continue;
        for (int c = 0; c < 8; ++c) {
            const Vec3 p = transformPoint(n->world, {(c & 1) ? 0.5f : -0.5f, (c & 2) ? 0.5f : -0.5f, (c & 4) ? 0.5f : -0.5f});
            if (corners) corners->push_back(p);
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
    }
    if (!root) return false;
    centre = (lo + hi) * 0.5f;
    radius = 0.5f * length(hi - lo);
    const Vec3 f = transformDirection(root->world, {0, 0, 1});
    forward = length(Vec3{f.x, 0, f.z}) > 1e-4f ? normalize(Vec3{f.x, 0, f.z}) : Vec3{0, 0, 1};
    return true;
}

void drawLibraryLabels(Hud& hud, int w, int h) {
    hud.begin(w, h);
    const char* names[Renderer::kLegendCount] = {"CUBE", "SPHERE", "CYLINDER", "CONE (r_top = 0)"};
    const MeshData meshes[Renderer::kLegendCount] = {createCube(), createSphere(36, 18), createCylinder(36),
                                                     createCylinder(36, 0.5f, 0.0f, true)};
    const PrimitiveType types[Renderer::kLegendCount] = {PrimitiveType::Cube, PrimitiveType::Sphere,
                                                         PrimitiveType::Cylinder, PrimitiveType::Cone};
    const float cell = static_cast<float>(w) / static_cast<float>(Renderer::kLegendCount);
    for (int i = 0; i < Renderer::kLegendCount; ++i) {
        const Vec3 c = primitiveColor(types[i]);
        const float x = cell * static_cast<float>(i) + 30.0f;
        char counts[64];  // measured from the generators, not typed in
        std::snprintf(counts, sizeof(counts), "%zu vertices / %zu triangles", meshes[i].vertexCount(),
                      meshes[i].triangleCount());
        hud.text(x, 24.0f, names[i], 34.0f, Rgba{c.x, c.y, c.z, 1.0f});
        hud.text(x, 66.0f, counts, 22.0f, Rgba{0.85f, 0.88f, 0.92f, 1.0f});
    }
    hud.end();
}

}  // namespace

int App::captureMode() {
    const std::string dir = opt_.outPath.empty() ? "captures" : opt_.outPath;
    std::vector<Shot> shots = shotList();
    {   // an --only list that names only lab_* shots (or only v6 shots): run just those, on a fresh simulation
        std::vector<std::string> names;
        size_t pos = 1;
        while (!opt_.only.empty() && pos < opt_.only.size()) {
            const size_t e = opt_.only.find(',', pos);
            const std::string name = opt_.only.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            if (!name.empty()) names.push_back(name);
            if (e == std::string::npos) break;
            pos = e + 1;
        }
        const auto find = [&](const std::string& n) -> const Shot* {
            for (const Shot& x : shots)
                if (n == x.file) return &x;
            return nullptr;
        };
        bool allLab = !names.empty(), allV6 = !names.empty(), allHero = !names.empty();
        for (const std::string& n : names) {
            const Shot* x = find(n);
            allLab = allLab && x && x->lab[0] && !x->v6;
            allV6 = allV6 && x && x->v6;
            allHero = allHero && x && x->hero;
        }
        if (allHero) shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& x) { return !x.hero; }), shots.end());
        if (allLab) shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& x) { return !x.lab[0] || x.v6; }), shots.end());
        if (allV6) shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& x) { return !x.v6; }), shots.end());
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream log(dir + "/capture_log.txt");
    if (!log) {
        std::fprintf(stderr, "Cannot write to %s\n", dir.c_str());
        return 1;
    }
    RenderTarget target;
    scriptCut_ = DirectorCut::V5;  // the scripted controllables of the v5 shots (helicopter, tank, AA gun, doors)
    float t = 0.0f;
    float nextLog = 0.0f;
    float impactT = -1.0f;
    bool labWarm = false;
    Vec3 strikePoint{0, 0, 0};
    // One simulation step with the capture log (rover / UAV trace once per simulated second).
    auto stepLogged = [&]() {
        if (t >= kScriptStart && scriptClock_ < 0.0f) scriptClock_ = 0.0f;  // the director script starts here
        step(kStep);
        t += kStep;
        for (const GameEvent& e : scene_->events()) {
            if (e.type == GameEventType::Explosion || e.type == GameEventType::DoorMoving || e.type == GameEventType::CannonFire ||
                e.type == GameEventType::GatlingBurst)
                continue;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "EVENT t=%6.2f %-14s uav=%d track=%d value=%d combined=%d pos(%.1f,%.1f)\n", t,
                          eventName(e.type), e.uav, e.track, e.value, e.combined, e.position.x, e.position.z);
            log << buf;
        }
        if (t >= nextLog) {
            const DroneState& d = scene_->drone;
            const Vec3 r = sa::primaryRover(*scene_);
            char buf[256];
            const DayCycle* clock = sa::dayCycle(*scene_);
            std::snprintf(buf, sizeof(buf), "t=%5.1f clock=%s uav=%-9s pos(%5.2f,%5.2f,%5.2f) blobs=%d found=%d rover=%s (%5.2f,%5.2f)\n",
                          t, clock ? formatClock(clock->hour()).c_str() : "--:--", droneModeName(d.mode), d.position.x, d.position.y,
                          d.position.z, detection_.blobCount, detection_.found, sa::roverHidden(*scene_) ? "HIDDEN " : "EXPOSED", r.x, r.z);
            log << buf;
            nextLog += 1.0f;
        }
    };
    // Game moments: the squad plays the strikes by itself, so the shots wait for them (events of the latest step).
    auto stepEvents = [&]() -> const std::vector<GameEvent>& { return scene_->events(); };
    auto hasEvent = [&](GameEventType type, Vec3* pos = nullptr) {
        for (const GameEvent& e : stepEvents())
            if (e.type == type) {
                if (pos) *pos = e.position;
                return true;
            }
        return false;
    };
    // Confirmed tracks that fall inside UAV 0's image (FPV shot).
    auto tracksInUavView = [&]() {
        const CameraView cv = uavCameraView(*scene_, 16.0f / 9.0f, 40.0f);
        const Mat4 vp = cv.proj * cv.view;
        int n = 0;
        for (const Track& tr : scene_->squad().tracker().tracks()) {
            if (tr.state == TrackState::Tentative) continue;
            if (projectMarker(vp, cv.position, tr.position(), 1920, 1080).onScreen) ++n;
        }
        return n;
    };
    auto confirmedTracks = [&]() {
        int n = 0;
        for (const Track& tr : scene_->squad().tracker().tracks()) n += tr.state != TrackState::Tentative;
        return n;
    };
    // Trig::Status: soldiers whose status line contains the text (one soldier when waitSoldier >= 0).
    const char* waitText = "";
    int waitSoldier = -1, waitCount = 1;
    auto soldiersMatching = [&]() {
        const Garrison* g = sa::garrison(*scene_);
        int n = 0;
        for (int i = 0; g && i < g->count(); ++i)
            if (const Controllable* c = g->soldier(i))
                if ((waitSoldier < 0 || waitSoldier == i) && c->status().find(waitText) != std::string::npos) ++n;
        return n;
    };
    Vec3 eventPos{0, 0, 0};
    // Steps until the moment happens (or the timeout); returns whether it did. eventPos = where.
    auto waitFor = [&](Trig trig, float timeout) {
        for (float waited = 0.0f; waited < timeout; waited += kStep) {
            stepLogged();
            const Squad& sq = scene_->squad();
            bool hit = false;
            switch (trig) {
                case Trig::Launch: hit = hasEvent(GameEventType::RoundLaunched, &eventPos); break;
                case Trig::Combined: hit = hasEvent(GameEventType::CombinedStrike, &eventPos); break;
                case Trig::Hit: hit = hasEvent(GameEventType::EnemyHit, &eventPos); break;
                case Trig::Kill: hit = hasEvent(GameEventType::EnemyDestroyed, &eventPos); break;
                case Trig::Wave: hit = hasEvent(GameEventType::WaveStart, &eventPos); break;
                case Trig::Inbound:
                    hit = sq.strike().phase == StrikePhase::Inbound && sq.strike().launched[0] && sq.strike().launched[1];
                    if (hit) if (const Track* tr = sq.tracker().find(sq.strike().track)) eventPos = tr->position();
                    break;
                case Trig::Tracks2:
                    hit = confirmedTracks() >= 2;
                    break;
                case Trig::FpvTracks: hit = tracksInUavView() >= 2; break;
                case Trig::Withdraw: hit = hasEvent(GameEventType::Withdraw, &eventPos); break;
                case Trig::Shift: hit = hasEvent(GameEventType::ShiftChange, &eventPos) || hasEvent(GameEventType::Dusk, &eventPos); break;
                case Trig::Persons:
                    for (const Measurement2D& m : uavBlobs_[0]) hit = hit || m.cls == BlobClass::Person;
                    break;
                case Trig::Status: hit = soldiersMatching() >= waitCount; break;
                case Trig::None: hit = true; break;
            }
            if (hit) return true;
        }
        return false;
    };
    // A kill ends the wait for the explosion shots: they are timed from its detonation. v6: only real kills count (no
    // scripted blast, no synthetic HUD event): without one the explosion shots are logged and skipped.
    bool impactFound = false;
    auto findImpact = [&]() {
        impactFound = waitFor(Trig::Kill, 60.0f);
        if (!impactFound) log << "no kill within 60 s at t=" << t << ": explosion shots skipped\n";
        strikePoint = eventPos;
        impactT = t;
    };
    // The last shot an --only list asks for: the run ends there (no simulation of the shots after it).
    size_t lastWanted = shots.size();
    for (size_t k = 0; k < shots.size(); ++k)
        if (opt_.only.empty() || opt_.only.find("," + std::string(shots[k].file) + ",") != std::string::npos) lastWanted = k;
    for (size_t shotIndex = 0; shotIndex < shots.size() && shotIndex <= lastWanted; ++shotIndex) {
        const Shot& shot = shots[shotIndex];
        float shotTime = shot.time;
        bool skip = false;
        if (shot.hour >= 0.0f) {  // v6: the clock for this shot (manual unless the shot waits for a phase event)
            if (DayCycle* clock = sa::dayCycle(*scene_)) {
                clock->setAuto(false);
                clock->setHour(shot.hour);
                clock->setAuto(shot.autoClock);
            }
            for (float k = 0.0f; k < shot.settle; k += kStep) stepLogged();
            shotTime = std::max(shotTime, t);
        }
        if (shot.afterImpact >= 0.0f) {
            if (impactT < 0.0f || shot.trigger == Trig::Kill) findImpact();
            shotTime = impactT + shot.afterImpact;
            skip = !impactFound;
        } else if (shot.trigger != Trig::None) {
            waitText = shot.waitStatus;
            waitSoldier = shot.soldier;
            waitCount = shot.waitCount;
            const bool fired = waitFor(shot.trigger, shot.timeout);
            if (!fired) {
                log << "trigger for " << shot.file << " timed out at t=" << t << (shot.trigger == Trig::Hit || shot.trigger == Trig::Kill ? ": shot skipped" : "") << "\n";
                skip = shot.trigger == Trig::Hit || shot.trigger == Trig::Kill;  // the HUD element must come from a real hit
            }
            strikePoint = eventPos;
            shotTime = t + std::max(shot.delay, 0.0f);
        }
        if (shot.autoClock)  // freeze the clock again once the moment has come
            if (DayCycle* clock = sa::dayCycle(*scene_)) clock->setAuto(false);
        while (t + 0.5f * kStep < shotTime) stepLogged();
        if (!opt_.only.empty() && opt_.only.find("," + std::string(shot.file) + ",") == std::string::npos) continue;
        if (skip) {
            std::printf("skipped %s (no real game moment)\n", shot.file);
            continue;
        }
        if (shot.lab[0]) {  // v5 lab figure: its own renderer path (app/CaptureLab.cpp)
            if (!labWarm) {  // one real step first: the renderer builds its terrain maps from the unmodified scene
                stepLogged();
                labWarm = true;
            }
            scene_->updateWorld();
            lab::Ctx ctx;
            ctx.scene = scene_.get();
            ctx.renderer = renderer_.get();
            ctx.dir = dir;
            ctx.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : Quality::Ultra;
            ctx.qualityForced = opt_.quality >= 0;
            ctx.log = &log;
            const std::string kind = shot.lab;
            bool ok = true;
            if (kind == "materials") ok = lab::materials(ctx);
            else if (kind == "shading") ok = lab::shading(ctx);
            else if (kind == "relief") ok = lab::relief(ctx);
            else if (kind == "reflect") ok = lab::reflections(ctx);
            else if (kind == "buffers") ok = lab::buffers(ctx);
            else if (kind == "explorer") ok = lab::explorer(ctx);
            else if (kind == "rt") {
                std::string name = shot.file, out = dir;
                const size_t night = name.find("_night");
                if (night != std::string::npos) {
                    name = name.substr(0, night);
                    out = dir + "/rt_night";
                    std::filesystem::create_directories(out, ec);
                }
                rtcap::Ctx rc{scene_.get(), renderer_.get(), out, 1280, 720, Quality::Epic, shot.hour, &log};
                ok = rtcap::figure(rc, name);
                log << shot.file << (ok ? " written" : " FAILED") << "\n";
            }  // explorer_stage_01..16, montage, extras, explorer_stages.json
            else if (kind == "cvnoise") {
                // A frame where UAV 0's CV holds a target (fallback: whatever it sees after 60 s): the clean render
                // of its camera, then a fresh sensor model on a copy (the run's own sensors keep their state).
                bool found = false;
                for (float waited = 0.0f; waited < 60.0f && !found; waited += kStep) {
                    stepLogged();
                    found = cvFrame_[0].targets > 0;
                }
                lab::NoiseFrame nf;
                nf.uav = 0;
                nf.time = t;
                nf.seed = deriveSeed(opt_.seed == 0 ? 1u : opt_.seed, 0x5e10u);
                FrameStats st;
                renderer_->captureUavCamera(*scene_, nf.clean, st, 0);
                SensorModel sensor(nf.seed);
                nf.cfg = sensor.config();
                nf.noisy = nf.clean;
                sensor.apply(nf.noisy, kCvWidth, kCvHeight);
                nf.cleanBlobs = cv::detectAll(nf.clean, kCvWidth, kCvHeight);
                nf.noisyBlobs = cv::detectAll(nf.noisy, kCvWidth, kCvHeight);
                log << "lab_cv_noise: t=" << t << " target frame " << found << " clean blobs " << nf.cleanBlobs.size()
                    << " noisy blobs " << nf.noisyBlobs.size() << "\n";
                ok = lab::cvNoise(ctx, nf);
            }
            else if (kind == "cv") {
                // The moment: the first frame where both UAVs have a target blob and the shared tracker holds a
                // triangulated track (fallback: a frame where any UAV sees one).
                auto triangulated = [&]() {
                    for (const Track& tr : scene_->squad().tracker().tracks())
                        if (tr.state != TrackState::Tentative && tr.triangulated && tr.seenNow == 3u) return true;
                    return false;
                };
                bool found = false;
                for (float waited = 0.0f; waited < 240.0f && !found; waited += kStep) {
                    stepLogged();
                    found = cvFrame_[0].targets > 0 && cvFrame_[1].targets > 0 && triangulated();
                }
                if (!found) {
                    log << "lab_cv: no two-view moment, using a single-view frame\n";
                    for (float waited = 0.0f; waited < 240.0f && !found; waited += kStep) {
                        stepLogged();
                        found = cvFrame_[0].targets > 0 || cvFrame_[1].targets > 0;
                    }
                }
                log << "lab_cv: moment t=" << t << " found=" << found << "\n";
                lab::CvView views[2] = {lab::makeCvView(*scene_, 0, uavImage_, t), lab::makeCvView(*scene_, 1, uavImage2_, t)};
                int mainUav = 0, area = -1;
                for (int u = 0; u < 2; ++u)
                    for (const Measurement2D& m : views[u].dbg.targets)
                        if (m.area > area) { area = m.area; mainUav = u; }
                ok = lab::cvFrame(ctx, views);
                std::vector<lab::CvView> strip{views[mainUav]};
                for (int k = 1; k < 6; ++k) {
                    for (int i = 0; i < 9; ++i) stepLogged();
                    strip.push_back(lab::makeCvView(*scene_, mainUav, mainUav == 0 ? uavImage_ : uavImage2_, t));
                }
                ok = lab::cvStrip(ctx, strip, mainUav) && ok;
            }
            lab::writeCaptions(ctx);
            if (!ok) return 1;
            continue;
        }
        settings_ = RenderSettings{};
        settings_.view = shot.view;
        settings_.exposure = shot.exposure;
        settings_.edges = shot.edges;
        settings_.shading = shot.shading;
        settings_.depthTest = shot.depthTest;
        settings_.showPlan = shot.plan;
        settings_.showAxes = shot.axes;
        settings_.isolate = shot.isolate;
        settings_.cinematic = shot.cinematic;
        settings_.palette = shot.palette;
        settings_.rain = shot.rain ? 1.0f : 0.0f;
        scene_->wetness = shot.rain ? 1.0f : 0.0f;
        // Report figures: best quality (frame rate is irrelevant offline) unless --quality says otherwise.
        settings_.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : Quality::Ultra;
        const std::string off = shot.without;
        settings_.shadowsOn = off != "shadows";
        settings_.ssaoOn = off != "ssao";
        settings_.fogOn = off != "fog";
        settings_.bloomOn = off != "bloom";
        settings_.showLegend = std::string(shot.isolate).empty();
        showHud_ = shot.hud;
        showFeed_ = shot.feed;
        scene_->explode = shot.exploded ? 1.0f : 0.0f;
        exploded_ = shot.exploded;
        scene_->updateWorld();

        camera_ = Camera{};
        possess(-1);
        camera_.mode = shot.cam;
        syncWalkWorld();  // WalkSpace floors / walls and the closed doors for the walk camera
        thermalFpv_ = shot.fpv;
        thermalFovDeg_ = shot.fov;
        if (shot.cam == CameraMode::Walk) {
            camera_.placeWalk(shot.walkX, shot.walkZ, shot.walkFeet);
            camera_.walkYawDeg = shot.walkYaw;
            camera_.walkPitchDeg = shot.walkPitch;
        }
        camera_.yawDeg = shot.yaw;
        camera_.pitchDeg = shot.pitch;
        camera_.distance = shot.dist;
        if (shot.follow == Follow::Uav) camera_.target = transformPoint(scene_->uav().root->world, {0, 0, 0});
        if (shot.follow == Follow::Rover) camera_.target = sa::primaryRover(*scene_) + Vec3{0, 0.2f, 0};
        if (shot.follow == Follow::Bunker) camera_.target = {0, 1.3f, 0};
        if (shot.follow == Follow::Soldier) camera_.target = sa::soldierPosition(*scene_, shot.soldier, {0, 0, 12.0f}) + Vec3{0, 1.0f, 0};
        axesControllable_ = shot.axes && shot.soldier >= 0 ? sa::soldierIndex(*scene_, shot.soldier) : -1;
        planRoutesOnly_ = shot.plan && shot.v6;  // v6 route figures: splines and via chains only, no flood field
        if (shot.follow == Follow::Action)  // between the bunker and the rover, so both agents are in frame
            camera_.target = lerp(Vec3{0, 0.8f, 0}, sa::primaryRover(*scene_), 0.45f) + Vec3{0, 0.6f, 0};

        if (shot.possess >= 0) {
            possessSlot(static_cast<Slot>(shot.possess));
            camera_.possessSight = shot.sight;
            if (shot.sight && shot.possess == static_cast<int>(Slot::Uav)) settings_.view = ViewMode::Thermal;
        }
        if (shot.soldier >= 0 && shot.follow != Follow::Soldier) {  // v6: take control of a soldier (chase or first person)
            const int index = sa::soldierIndex(*scene_, shot.soldier);
            if (index >= 0) {
                possess(index);
                camera_.possessSight = shot.sight;
            } else {
                log << "no soldier " << shot.soldier << " for " << shot.file << "\n";
            }
        }
        if (shot.possessName[0]) {
            const std::vector<Controllable*> list = sa::controllables(*scene_);
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i] && nameHas(list[i]->name(), shot.possessName)) {
                    possess(static_cast<int>(i));
                    camera_.possessSight = shot.sight;
                    break;
                }
        }
        if (shot.custom) {  // fixed camera: eye -> aim (offsets from the strike point when 'rel')
            const Vec3 base = shot.rel ? strikePoint : Vec3{0, 0, 0};
            Vec3 aim = base + shot.aim;
            if (shot.aimSun)  // v6 sunset: look straight at the sun of the current clock (aim = offset from that line)
                if (const DayCycle* clock = sa::dayCycle(*scene_))
                    aim = base + shot.eye + DayCycle::sunDirection(clock->hour(), clock->config()) * 60.0f + shot.aim;
            const Vec3 eye = base + shot.eye, d = aim - eye;
            camera_.mode = CameraMode::Free;
            camera_.freePos = eye;
            camera_.freeYawDeg = degrees(std::atan2(d.x, d.z));
            camera_.freePitchDeg = degrees(std::asin(clampf(d.y / std::max(length(d), 1e-4f), -1.0f, 1.0f)));
            camera_.fovDeg = shot.camFov;
        }
        if (shot.hero) {
            Vec3 c, f;
            float r = 1.0f;
            std::vector<Vec3> corners;
            if (objectBounds(*scene_, shot.isolate, c, r, f, &corners)) {
                // Front three-quarter on the sunny side (front-lit), then the distance at which the projected box of
                // the parts spans heroFill of the frame (larger of width / height, bisection) and the box centred.
                const float front = degrees(std::atan2(f.x, f.z));
                float yaw = front + shot.yaw;
                if (const DayCycle* clock = sa::dayCycle(*scene_)) {
                    const Vec3 sun = DayCycle::sunDirection(clock->hour(), clock->config());
                    auto facing = [&](float y) { return std::sin(radians(y)) * sun.x + std::cos(radians(y)) * sun.z; };
                    if (facing(front - shot.yaw) > facing(yaw)) yaw = front - shot.yaw;
                }
                const float aspect = static_cast<float>(opt_.sizeGiven ? opt_.width : shot.width) /
                                     static_cast<float>(opt_.sizeGiven ? opt_.height : shot.height);
                const float th = std::tan(radians(camera_.fovDeg) * 0.5f);
                const Vec3 dir{std::cos(radians(shot.pitch)) * std::sin(radians(yaw)), std::sin(radians(shot.pitch)),
                               std::cos(radians(shot.pitch)) * std::cos(radians(yaw))};
                const Vec3 fwd = dir * -1.0f, right = normalize(cross(fwd, Vec3{0, 1, 0})), up = cross(right, fwd);
                Vec3 target = c;
                float ndcCx = 0, ndcCy = 0;
                auto span = [&](float d) {  // max half-extent of the projected parts in NDC units (1 = frame edge)
                    const Vec3 eye = target + dir * d;
                    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                    for (const Vec3& p : corners) {
                        const Vec3 v = p - eye;
                        const float z = std::max(dot(v, fwd), 0.05f);
                        const float x = dot(v, right) / (z * th * aspect), y = dot(v, up) / (z * th);
                        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
                    }
                    ndcCx = 0.5f * (x0 + x1);
                    ndcCy = 0.5f * (y0 + y1);
                    return std::max(0.5f * (x1 - x0), 0.5f * (y1 - y0));
                };
                float lo = 0.5f * r, hi = 20.0f * r;
                for (int it = 0; it < 40; ++it) {
                    const float mid = 0.5f * (lo + hi);
                    (span(mid) > shot.heroFill ? lo : hi) = mid;
                }
                span(hi);  // centre the projected box: shift the target in the image plane at the object's depth
                target = target + right * (ndcCx * hi * th * aspect) + up * (ndcCy * hi * th);
                camera_.mode = CameraMode::Orbit;
                camera_.target = target;
                camera_.yawDeg = yaw;
                camera_.pitchDeg = shot.pitch;
                camera_.distance = hi;
                log << "hero " << shot.file << ": centre (" << c.x << ", " << c.y << ", " << c.z << ") radius " << r << " dist "
                    << camera_.distance << "\n";
            } else {
                log << "hero " << shot.file << ": no object '" << shot.isolate << "'\n";
            }
        }
        if (shot.director >= 0.0f) {
            camera_.mode = CameraMode::Cinematic;
            camera_.directorCut = DirectorCut::V5;  // the report shots were authored against the v5 sequence
            camera_.directorTime = shot.director;
            applyCinematic();
        }
        const int width = opt_.sizeGiven ? opt_.width : shot.width, height = opt_.sizeGiven ? opt_.height : shot.height;
        if (!target.create(width, height, 0, false)) return 1;
        target.bind();
        if (shot.library) {
            stats_ = {};
            glClearColor(0.075f, 0.08f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const int size = width / Renderer::kLegendCount;
            renderer_->drawPrimitiveLegend(0, 0, size, 0.55f, stats_, true);
            glViewport(0, 0, width, height);
            drawLibraryLabels(renderer_->hud(), width, height);
        } else {
            // Temporal effects (TAA, exposure / sensor gain) start fresh for every shot; the frozen frame is
            // rendered several times so the jittered TAA samples accumulate into a supersampled image.
            renderer_->resetHistory();
            const int frames = qualityProfile(settings_.quality).taa ? 8 : 1;
            for (int f = 0; f < frames; ++f) {
                target.bind();
                renderFrame(width, height, true);
            }
        }
        std::vector<uint8_t> rgb;
        target.read(rgb);
        const std::string path = dir + "/" + shot.file + ".png";
        const bool ok = writePng(path, width, height, rgb);
        std::printf("%s %s (t = %.1f s, %d draw calls, %lld triangles)\n", ok ? "wrote" : "FAILED", path.c_str(), t,
                    stats_.drawCalls + cvStats_.drawCalls, stats_.triangles + cvStats_.triangles);
        float stats[4] = {0, 0, 0, 0};
        renderer_->adaptedStats(stats);
        std::printf("    frame statistics: min %.3f max %.3f mean %.3f (log2 luminance or heat)\n", stats[0], stats[1], stats[2]);
        if (const Garrison* g = sa::garrison(*scene_))  // where the soldiers are (camera framing of the v6 shots)
            for (int i = 0; shot.v6 && i < g->count(); ++i)
                if (const Controllable* c = g->soldier(i))
                    log << "  soldier " << i << " (" << c->position().x << ", " << c->position().y << ", " << c->position().z << ") "
                        << c->status() << "\n";
        log << "shot " << shot.file << " at t=" << t << " stat_mean=" << stats[2] << " draws=" << stats_.drawCalls + cvStats_.drawCalls
            << " tris=" << stats_.triangles + cvStats_.triangles << "\n";
        if (!ok) return 1;
    }
    return 0;
}

int App::traceMode() {
    const std::string path = opt_.outPath.empty() ? "build/trace.csv" : opt_.outPath;
    std::ofstream f(path);
    if (!f) return 1;
    f << "t,uav_mode,detected,blobs,rover_hidden,uav_x,uav_y,uav_z,rover_x,rover_z,est_x,est_z,est_error,"
         "gimbal_yaw,gimbal_pitch,rover_speed,rover_in_view,rover_u,rover_v,range,image_peak,"
         "hour,phase,seed,uav0_targets,uav0_persons,frames_dropped\n";  // v6 columns (appended: old readers keep working)
    std::printf("Trace: seed %u\n", opt_.seed);
    // Event log next to the trace (FILE.events.csv): every game event of every step plus synthetic 'Spawn' rows, with
    // the clock, so the day / night rules can be checked offline (spawns only at night, Withdraw only at dawn, every
    // kill from a UAV round whose aim came from a vision track).
    std::ofstream ev(path + ".events.csv");
    ev << "t,hour,phase,waves_allowed,event,uav,track,value,combined,x,z,note\n";
    // v9 M5: the UAV duty timeline (FILE.duty.csv): a row on every duty change and once a second.
    std::ofstream duty(path + ".duty.csv");
    duty << "t,hour,phase,day_ops,waves_allowed,alive,uav1_duty,uav1_height,uav1_spool,uav1_vision,uav2_duty,uav2_height,"
            "uav2_spool,uav2_vision,change\n";
    std::string dutyPrev[kUavCount];
    std::vector<int> lifePrev;
    for (const auto& in : scene_->intruders()) lifePrev.push_back(static_cast<int>(in->life));
    int spawns = 0, daySpawns = 0, badWithdraw = 0, kills = 0, killsNoRound = 0, launches = 0, launchesNoTrack = 0, otherFire = 0;
    GameEventType lastPhase = GameEventType::NightFalls;  // the trace starts in the night (or after the start hour's phase)
    if (const DayCycle* c0 = sa::dayCycle(*scene_)) lastPhase = c0->wavesAllowed() ? GameEventType::NightFalls : GameEventType::DayBreak;
    const int steps = static_cast<int>(opt_.seconds / kStep);
    int hits = 0, blasts = 0, strikes = 0, spotted = 0, withdrawn = 0, phases = 0;
    for (int i = 1; i <= steps; ++i) {
        step(kStep);
        const DayCycle* clk = sa::dayCycle(*scene_);
        const float hourNow = clk ? clk->hour() : 23.0f;
        const char* phaseNow = clk ? clk->phaseName() : "NIGHT";
        const bool waves = scene_->dayRules().wavesAllowed;   // the clock's rule (v9 M5: or day ops)
        int stepBlast = 0;  // strongest blast of this step (x100): UAV rounds are >= 90
        for (const GameEvent& e : scene_->events())
            if (e.type == GameEventType::Explosion) stepBlast = std::max(stepBlast, e.value);
        auto row = [&](const char* name, const GameEvent& e, const std::string& note) {
            char buf[320];
            std::snprintf(buf, sizeof(buf), "%.4f,%.4f,%s,%d,%s,%d,%d,%d,%d,%.3f,%.3f,%s\n", i * kStep, hourNow, phaseNow, waves ? 1 : 0, name,
                          e.uav, e.track, e.value, e.combined ? 1 : 0, e.position.x, e.position.z, note.c_str());
            ev << buf;
        };
        for (const GameEvent& e : scene_->events()) {
            std::string note;
            switch (e.type) {
                case GameEventType::Dusk: case GameEventType::NightFalls: case GameEventType::Dawn: case GameEventType::DayBreak:
                    lastPhase = e.type;
                    break;
                case GameEventType::Withdraw:  // allowed after Dawn (or DayBreak) until the next NightFalls
                    if (lastPhase != GameEventType::Dawn && lastPhase != GameEventType::DayBreak) {
                        ++badWithdraw;
                        note = "VIOLATION:withdraw_outside_dawn";
                    }
                    break;
                case GameEventType::RoundLaunched: {  // the round's aim: a confirmed vision track of the shared tracker
                    ++launches;
                    const Track* tr = scene_->squad().tracker().find(e.track);
                    char b[96];
                    if (tr) std::snprintf(b, sizeof(b), "track_hits=%d state=%d tri=%d", tr->hits, static_cast<int>(tr->state), tr->triangulated ? 1 : 0);
                    else std::snprintf(b, sizeof(b), "no_track");
                    note = b;
                    if (!tr || tr->state == TrackState::Tentative) {
                        ++launchesNoTrack;
                        note += " VIOLATION:round_without_confirmed_track";
                    }
                    break;
                }
                case GameEventType::EnemyDestroyed:
                    ++kills;
                    note = "blast=" + std::to_string(stepBlast);
                    if (stepBlast < 90) {
                        ++killsNoRound;
                        note += " VIOLATION:kill_without_round_blast";
                    }
                    break;
                case GameEventType::CannonFire: case GameEventType::GatlingBurst: ++otherFire; break;
                default: break;
            }
            if (e.type != GameEventType::DoorMoving) row(eventName(e.type), e, note);
        }
        const auto& ins = scene_->intruders();
        for (size_t k = 0; k < ins.size() && k < lifePrev.size(); ++k) {
            const int life = static_cast<int>(ins[k]->life);
            if (life == static_cast<int>(Intruder::Life::Alive) && lifePrev[k] != life) {
                ++spawns;
                GameEvent s;
                s.track = static_cast<int>(k);
                s.position = ins[k]->state.position;
                std::string note = "slot=" + std::to_string(k);
                if (!waves) {
                    ++daySpawns;
                    note += " VIOLATION:spawn_by_day";
                }
                row("Spawn", s, note);
            }
            lifePrev[k] = life;
        }
        for (const GameEvent& e : scene_->events()) {
            hits += e.type == GameEventType::EnemyHit ? 1 : 0;
            blasts += (e.type == GameEventType::Explosion && e.value >= 90) ? 1 : 0;
            strikes += e.type == GameEventType::CombinedStrike ? 1 : 0;
            spotted += e.type == GameEventType::EnemySpotted ? 1 : 0;
            withdrawn += e.type == GameEventType::Withdraw ? 1 : 0;
            phases += (e.type == GameEventType::Dusk || e.type == GameEventType::NightFalls || e.type == GameEventType::Dawn ||
                       e.type == GameEventType::DayBreak) ? 1 : 0;
        }
        {   // v9 M5 duty timeline
            std::string change;
            for (int u = 0; u < kUavCount; ++u) {
                const std::string now = scene_->uavDutyState(u);
                const std::string was = dutyPrev[u].empty() ? std::string("START") : dutyPrev[u];
                if (now != was) change += (change.empty() ? "" : " ") + ("UAV-" + std::to_string(u + 1)) + ":" + was + "->" + now;
                dutyPrev[u] = now;
            }
            if (!change.empty() || i % 60 == 0) {
                char b[400];
                const Squad& sq = scene_->squad();
                std::snprintf(b, sizeof(b), "%.4f,%.4f,%s,%d,%d,%d,%s,%.3f,%.3f,%d,%s,%.3f,%.3f,%d,%s\n", i * kStep, hourNow, phaseNow,
                              scene_->dayOps() ? 1 : 0, waves ? 1 : 0, scene_->aliveIntruders(), scene_->uavDutyState(0),
                              sq.heightAbovePad(0), sq.rotorSpool(0), scene_->uavVisionActive(0) ? 1 : 0, scene_->uavDutyState(1),
                              sq.heightAbovePad(1), sq.rotorSpool(1), scene_->uavVisionActive(1) ? 1 : 0, change.c_str());
                duty << b;
            }
        }
        if (i % 15 != 0) continue;  // 4 samples per second
        const DroneState& d = scene_->drone;
        const Vec3 rpos = sa::primaryRover(*scene_);
        const bool rhidden = sa::roverHidden(*scene_);
        const Vec3 diff{d.targetEstimate.x - rpos.x, 0.0f, d.targetEstimate.z - rpos.z};
        const float err = detection_.found && d.hasEstimate ? length(diff) : -1.0f;
        // Diagnostics: where the rover's motor box projects in the UAV image and the hottest pixel seen.
        const CameraView cv = uavCameraView(*scene_, static_cast<float>(kCvWidth) / kCvHeight, kCvFovYDeg);
        const Vec3 motor = rpos + Vec3{0, 0.33f, 0};
        const Vec4 c = (cv.proj * cv.view) * Vec4{motor.x, motor.y, motor.z, 1.0f};
        const float u = c.w > 0 ? c.x / c.w : 9.0f, v = c.w > 0 ? c.y / c.w : 9.0f;
        const bool inView = c.w > 0 && std::fabs(u) <= 1.0f && std::fabs(v) <= 1.0f;
        int peak = 0;
        for (uint8_t px : uavImage_) peak = std::max(peak, static_cast<int>(px));
        const DayCycle* clock = sa::dayCycle(*scene_);
        const int targets0 = cvFrame_[0].targets, persons0 = cvFrame_[0].persons;
        char buf[480];
        std::snprintf(buf, sizeof(buf),
                      "%.2f,%s,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f,%d,%.3f,%.3f,%.2f,%d,%.3f,%s,%u,%d,%d,%d\n",
                      i * kStep, droneModeName(d.mode), detection_.found, detection_.blobCount, rhidden, d.position.x,
                      d.position.y, d.position.z, rpos.x, rpos.z, d.targetEstimate.x, d.targetEstimate.z,
                      err, d.gimbalYawDeg, d.gimbalPitchDeg, sa::roverSpeed(*scene_), inView, u, v,
                      length(motor - cv.position), peak, clock ? clock->hour() : 23.0f, clock ? clock->phaseName() : "NIGHT",
                      opt_.seed, targets0, persons0, framesDropped_[0] + framesDropped_[1]);
        f << buf;
    }
    std::printf("Trace of %.0f s written to %s\n", opt_.seconds, path.c_str());
    const MissionState& ms = scene_->mission().state();
    std::printf("Game: wave %d, kills %d (combined %d), rounds launched %d, breaches %d, integrity %.0f %%, score %d, "
                "fused track updates %d\n      round blasts %d, hits %d, combined strikes %d, tracks confirmed %d\n", ms.wave, ms.kills, ms.combinedKills, scene_->roundsLaunched(), ms.breaches,
                ms.integrity, ms.score, scene_->squad().tracker().fusedUpdates(), blasts, hits, strikes, spotted);
    std::printf("v6: seed %u, phase events %d, rovers withdrawn %d, frames %d / %d dropped (UAV 1 / 2: %d / %d)\n", opt_.seed,
                phases, withdrawn, framesDropped_[0] + framesDropped_[1], framesTotal_[0] + framesTotal_[1], framesDropped_[0],
                framesDropped_[1]);
    std::printf("rules: spawns %d (by day %d), withdrawals outside dawn %d, kills %d (without a UAV round blast %d), "
                "rounds %d (without a confirmed vision track %d), cannon / gatling fire %d -> %s\n",
                spawns, daySpawns, badWithdraw, kills, killsNoRound, launches, launchesNoTrack, otherFire,
                daySpawns + badWithdraw + killsNoRound + launchesNoTrack == 0 ? "OK" : "VIOLATIONS");
    std::printf("events written to %s.events.csv, UAV duty to %s.duty.csv\n", path.c_str(), path.c_str());
    return 0;
}

int App::benchmarkMode() {
    struct Phase {
        const char* name = "";
        Quality quality = Quality::High;
        bool cinematic = true;
        bool indexed = true;
        const char* without = "";  // ablation: feature switched off
        bool rain = false;
        double fps = 0, frameMs = 0;
        std::vector<double> roundMs;  // frame time of each round
        double sumDraws = 0, sumShadowDraws = 0, sumCvDraws = 0, sumTris = 0, sumVerts = 0;  // per-frame averages
        FrameStats main, cv;
        int frames = 0;
        double stepMs = 0, renderMs = 0, presentMs = 0, renderCpuMs = 0, visionMs = 0;  // v7 WP-Q: CPU wall time per frame (sum over frames)
        bool skip = false;                               // TS_BENCH_PHASES filter
        std::vector<GpuTimer::Entry> passes;
    };
    std::vector<Phase> phases(17);
    phases[0].name = "v1 look (no post), indexed";
    phases[0].cinematic = false;
    phases[1].name = "LOW, indexed";
    phases[1].quality = Quality::Low;
    phases[2].name = "HIGH, indexed (glDrawElements)";
    phases[3].name = "HIGH, non-indexed (glDrawArrays)";
    phases[3].indexed = false;
    phases[4].name = "ULTRA, indexed";
    phases[4].quality = Quality::Ultra;
    const char* ablations[6][2] = {{"HIGH without shadow maps", "shadows"}, {"HIGH without SSAO", "ssao"},
                                   {"HIGH without volumetric fog", "fog"}, {"HIGH without bloom", "bloom"},
                                   {"HIGH without MSAA", "msaa"}, {"HIGH without searchlight shadow", "spotshadow"}};
    for (int i = 0; i < 6; ++i) {
        phases[5 + i].name = ablations[i][0];
        phases[5 + i].without = ablations[i][1];
    }
    phases[11].name = "HIGH, raining (soaked ground)";
    phases[11].rain = true;
    // EPIC (discrete GPU, e.g. RTX 4070) and the cost of its v3 features
    phases[12].name = "EPIC, indexed";
    phases[12].quality = Quality::Epic;
    const char* epicAblations[4][2] = {{"EPIC without TAA", "taa"}, {"EPIC without SSR", "ssr"},
                                       {"EPIC without foliage cards", "foliage"}, {"EPIC, raining", "rain"}};
    for (int i = 0; i < 4; ++i) {
        phases[13 + i].name = epicAblations[i][0];
        phases[13 + i].quality = Quality::Epic;
        if (std::string(epicAblations[i][1]) == "rain") phases[13 + i].rain = true;
        else phases[13 + i].without = epicAblations[i][1];
    }
    settings_.view = ViewMode::Visible;
    setSwapInterval(0);  // measure the real frame rate, not the monitor refresh
#if defined(_WIN32)
    // Background applications (editor, browser) compete for the CPU and the shared GPU; high priority keeps the
    // rounds comparable. Results are the median of 5 interleaved rounds; the spread is reported as the IQR.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif
    // Headless (EGL, remote GPU): no default framebuffer, so frames go to an off-screen target of the same size.
    RenderTarget offscreen;
    if (headless_ && !offscreen.create(opt_.width, opt_.height, 0, false)) return 1;
    // Same order as the interactive loop (v7 WP-Q): simulation + UAV cameras, main frame, then the UAV images' CV.
    auto frame = [&]() {
        stepSim(kStep);
        if (headless_) offscreen.bind();
        else glBindFramebuffer(GL_FRAMEBUFFER, 0);
        renderFrame(opt_.width, opt_.height, true);
        stepVision();
        present();
    };
    // v7 WP-Q: CPU breakdown of a frame (simulation + UAV cameras / render submission / present). A stall on the GPU
    // (readback, query wait, swap throttling) shows up in the step / that phase rather than as GPU pass time.
    double tStep = 0, tRender = 0, tPresent = 0, tRenderCpu = 0, tVision = 0;
    auto threadCpu = []() {
#if defined(__linux__)
        timespec ts{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return static_cast<double>(ts.tv_sec) + 1e-9 * static_cast<double>(ts.tv_nsec);
#elif defined(_WIN32)
        FILETIME created, exited, kernel, user;
        if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0.0;
        auto ticks = [](const FILETIME& f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
        return 1e-7 * static_cast<double>(ticks(kernel) + ticks(user));   // 100 ns units (scheduler-tick resolution)
#else
        return 0.0;
#endif
    };
    auto timedFrame = [&]() {
        const double t0 = nowSeconds();
        stepSim(kStep);
        const double t1 = nowSeconds();
        if (headless_) offscreen.bind();
        else glBindFramebuffer(GL_FRAMEBUFFER, 0);
        const double c1 = threadCpu();
        renderFrame(opt_.width, opt_.height, true);
        tRenderCpu += threadCpu() - c1;
        const double tv = nowSeconds();
        stepVision();
        tVision += nowSeconds() - tv;
        const double t2 = nowSeconds();
        present();
        const double t3 = nowSeconds();
        tStep += t1 - t0;
        tRender += t2 - t1;
        tPresent += t3 - t2;
    };
    for (int i = 0; i < 120; ++i) frame();  // warm-up: 2 s of simulation
    // Five interleaved rounds over all phases; the median frame time per phase is reported, which is robust
    // against slow drift (thermal throttling, background load) on this integrated GPU.
    // v7 WP-Q: quick runs: TS_BENCH_ROUNDS, TS_BENCH_SECONDS, TS_BENCH_PHASES (comma list of phase indices).
    auto envNumber = [](const char* name, double fallback) {
        const char* v = std::getenv(name);
        return v && *v ? std::atof(v) : fallback;
    };
    const int kRounds = std::max(1, static_cast<int>(envNumber("TS_BENCH_ROUNDS", 5)));
    const double kSeconds = std::max(0.25, envNumber("TS_BENCH_SECONDS", 4.0));
    if (const char* only = std::getenv("TS_BENCH_PHASES")) {
        const std::string list = std::string(",") + only + ",";
        for (size_t i = 0; i < phases.size(); ++i)
            phases[i].skip = list.find("," + std::to_string(i) + ",") == std::string::npos;
    }
    for (int round = 0; round < kRounds; ++round) {
        for (Phase& p : phases) {
            if (p.skip) continue;
            settings_.indexed = p.indexed;
            settings_.quality = p.quality;
            settings_.cinematic = p.cinematic;
            const std::string off = p.without;
            settings_.shadowsOn = off != "shadows";
            settings_.spotShadowOn = off != "spotshadow";
            settings_.ssaoOn = off != "ssao";
            settings_.fogOn = off != "fog";
            settings_.bloomOn = off != "bloom";
            settings_.msaaOn = off != "msaa";
            settings_.taaOn = off != "taa";
            settings_.ssrOn = off != "ssr";
            settings_.foliageOn = off != "foliage";
            settings_.rain = p.rain ? 1.0f : 0.0f;
            scene_->wetness = p.rain ? 1.0f : 0.0f;
            for (int i = 0; i < 20; ++i) frame();  // settle: new targets, query latency
            renderer_->timer().resetTotals();
            int frames = 0;
            tStep = tRender = tPresent = tRenderCpu = tVision = 0;
            const double start = nowSeconds();
            while (nowSeconds() - start < kSeconds && !closeRequested()) {
                timedFrame();
                ++frames;
                p.sumDraws += stats_.drawCalls;
                p.sumShadowDraws += stats_.shadowDraws;
                p.sumCvDraws += cvStats_.drawCalls;
                p.sumTris += static_cast<double>(stats_.triangles);
                p.sumVerts += static_cast<double>(stats_.vertices);
            }
            glFinish();
            p.roundMs.push_back(1000.0 * (nowSeconds() - start) / std::max(frames, 1));
            p.stepMs += 1000.0 * tStep;
            p.renderMs += 1000.0 * tRender;
            p.presentMs += 1000.0 * tPresent;
            p.renderCpuMs += 1000.0 * tRenderCpu;
            p.visionMs += 1000.0 * tVision;
            p.frames += frames;
            p.main = stats_;
            p.cv = cvStats_;
            p.passes.clear();
            for (const GpuTimer::Entry& e : renderer_->timer().entries())
                if (e.samples > 0) p.passes.push_back(e);
        }
    }
    phases.erase(std::remove_if(phases.begin(), phases.end(), [](const Phase& p) { return p.skip; }), phases.end());
    for (Phase& p : phases) {
        std::vector<double> sorted = p.roundMs;
        std::sort(sorted.begin(), sorted.end());
        p.frameMs = sorted[sorted.size() / 2];
        p.fps = 1000.0 / p.frameMs;
        std::printf("%-34s %6.1f FPS  %.3f ms/frame  (median of %d rounds, %d frames)\n", p.name, p.fps, p.frameMs,
                    kRounds, p.frames);
        const double nf = std::max(p.frames, 1);
        std::printf("      cpu: simulation + UAV cameras %.2f ms, render %.2f ms (incl. UAV image CV %.2f ms; main thread busy "
                    "%.2f ms), present %.2f ms\n", p.stepMs / nf, p.renderMs / nf, p.visionMs / nf, p.renderCpuMs / nf,
                    p.presentMs / nf);
        for (const GpuTimer::Entry& e : p.passes) std::printf("      %-18s %6.2f ms\n", e.name.c_str(), e.sumMs / e.samples);
    }

    const std::string path = opt_.outPath.empty() ? "build/benchmark.json" : opt_.outPath;
    std::ofstream f(path);
    if (!f) return 1;
    f << "{\n  \"renderer\": \"" << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << "\",\n";
    f << "  \"gl_version\": \"" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << "\",\n";
    f << "  \"resolution\": \"" << opt_.width << "x" << opt_.height << "\",\n";
    f << "  \"uav_camera\": \"" << kCvWidth << "x" << kCvHeight << " per frame\",\n  \"phases\": [\n";
    for (size_t i = 0; i < phases.size(); ++i) {
        const Phase& p = phases[i];
        f << "    {\"name\": \"" << p.name << "\", \"quality\": \"" << (p.cinematic ? qualityName(p.quality) : "V1")
          << "\", \"without\": \"" << p.without << "\", \"rain\": " << (p.rain ? "true" : "false") << ", \"indexed\": " << (p.indexed ? "true" : "false") << ", \"frames\": " << p.frames
          << ", \"fps\": " << p.fps << ", \"frame_ms\": " << p.frameMs
          << ", \"cpu_step_ms\": " << p.stepMs / std::max(p.frames, 1) << ", \"cpu_render_ms\": " << p.renderMs / std::max(p.frames, 1)
          << ", \"cpu_present_ms\": " << p.presentMs / std::max(p.frames, 1)
          << ", \"cpu_uav_vision_ms\": " << p.visionMs / std::max(p.frames, 1) << ", \"frame_ms_rounds\": [";
        for (size_t k = 0; k < p.roundMs.size(); ++k) f << (k ? ", " : "") << p.roundMs[k];
        f << "]" << ", \"avg_draw_calls_main\": " << p.sumDraws / std::max(p.frames, 1)
          << ", \"avg_draw_calls_shadow\": " << p.sumShadowDraws / std::max(p.frames, 1)
          << ", \"avg_draw_calls_uav_camera\": " << p.sumCvDraws / std::max(p.frames, 1)
          << ", \"avg_triangles_main\": " << p.sumTris / std::max(p.frames, 1)
          << ", \"avg_vertices_main\": " << p.sumVerts / std::max(p.frames, 1)
          << ", \"draw_calls_main\": " << p.main.drawCalls
          << ", \"draw_calls_shadow\": " << p.main.shadowDraws << ", \"draw_calls_prepass\": " << p.main.prepassDraws
          << ", \"draw_calls_uav_camera\": " << p.cv.drawCalls << ", \"triangles_main\": " << p.main.triangles
          << ", \"triangles_uav_camera\": " << p.cv.triangles << ", \"vertices_main\": " << p.main.vertices
          << ", \"primitive_instances\": " << p.main.primitives << ", \"scene_parts\": " << scene_->leaves().size()
          << ", \"grass_blades\": " << p.main.grassBlades << ", \"rain_drops\": " << p.main.rainDrops << ", \"passes\": [";
        for (size_t k = 0; k < p.passes.size(); ++k)
            f << (k ? ", " : "") << "{\"pass\": \"" << p.passes[k].name << "\", \"gpu_ms\": "
              << p.passes[k].sumMs / p.passes[k].samples << "}";
        f << "]}" << (i + 1 < phases.size() ? "," : "") << "\n";
    }
    f << "  ]\n}\n";
    std::printf("Benchmark written to %s\n", path.c_str());
    return 0;
}

}  // namespace ts
