// Non-interactive modes: scripted screenshot capture (for the report) and the performance benchmark.
// Both run the real simulation (UAV camera, tracker, rover planner) with a fixed 60 Hz time step.
#include <glad/gl.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <utility>

#include "app/App.h"
#include "app/HeadlessGL.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "objects/BaseLayout.h"
#include "primitives/Cube.h"
#include "primitives/Cylinder.h"
#include "primitives/Sphere.h"
#include "rendering/ImageWriter.h"

namespace ts {

namespace {

constexpr float kStep = 1.0f / 60.0f;

enum class Follow { None, Uav, Rover, Bunker, Action };

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
    int possess = -1;           // Slot to take control of (HUD drawn, chase or sight view)
    bool sight = false;
    float afterImpact = -1.0f;  // >= 0: taken this many seconds after the missile of the scripted strike detonated
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
    add(cam(v4("motor_pool", 58.5f), {-12.0f, 2.2f, -4.0f}, {-21.0f, 1.2f, -15.0f}, 50.0f));  // A2: sodium floodlight on the tank park
    add(cam(v4("tank_closeup", 59.0f), {-14.2f, 1.3f, -13.9f}, {-22.0f, 1.2f, -16.0f}, 44.0f));  // B1 proposal
    // Inside the bunker (walk camera, 1.7 m eye height on the WalkSpace floors). Doors open at script time 12.5 s.
    add(walkAt(v4("bunker_ramp", kScriptStart + 8.0f), 0.0f, 11.5f, 180.0f, -10.0f));
    add(walkAt(v4("bunker_door", kScriptStart + 10.0f), 0.0f, 7.0f, 180.0f, 0.0f));
    add(walkAt(v4("bunker_tunnel", kScriptStart + 20.0f), 0.0f, 2.5f, 180.0f, 0.0f, kBunkerFloorY));
    add(walkAt(v4("bunker_cic", kScriptStart + 21.0f), 0.0f, -2.0f, 160.0f, -3.0f, kBunkerFloorY));
    Shot possess_doors = v4("possess_doors", kScriptStart + 21.5f);
    possess_doors.possess = static_cast<int>(Slot::Doors);
    possess_doors.sight = false;
    possess_doors.hud = true;
    add(possess_doors);
    // Scripted controllables: the director's inputs drive the helicopter (18-38 s), tank (32-46 s), AA gun (46-54 s).
    Shot ht = v4("heli_takeoff", kScriptStart + 28.0f);
    ht.director = 28.0f;
    add(ht);
    Shot possess_heli_cockpit = v4("possess_heli_cockpit", kScriptStart + 29.0f);
    possess_heli_cockpit.possess = static_cast<int>(Slot::Helicopter);
    possess_heli_cockpit.sight = true;
    possess_heli_cockpit.hud = true;
    add(possess_heli_cockpit);
    Shot possess_tank_sight = v4("possess_tank_sight", kScriptStart + 38.0f);
    possess_tank_sight.possess = static_cast<int>(Slot::Tank);
    possess_tank_sight.sight = true;
    possess_tank_sight.hud = true;
    add(possess_tank_sight);
    Shot tf2 = v4("tank_fire", kScriptStart + 41.7f);
    tf2.director = 41.7f;
    add(tf2);
    Shot ag = v4("aa_gun_burst", kScriptStart + 48.5f);
    ag.director = 48.5f;
    add(ag);
    Shot possess_aa_sight = v4("possess_aa_sight", kScriptStart + 50.0f);
    possess_aa_sight.possess = static_cast<int>(Slot::AaGun);
    possess_aa_sight.sight = true;
    possess_aa_sight.hud = true;
    add(possess_aa_sight);
    Shot ags = v4("aa_gun_sight", kScriptStart + 51.5f);
    ags.director = 51.5f;
    add(ags);
    Shot possess_uav_manual = v4("possess_uav_manual", kScriptStart + 55.0f);
    possess_uav_manual.possess = static_cast<int>(Slot::Uav);
    possess_uav_manual.sight = true;
    possess_uav_manual.hud = true;
    add(possess_uav_manual);
    // The UAV strike (script time 81 s): missile launch, impact in thermal FPV, then the explosion at fixed delays.
    Shot ml = v4("uav_missile_launch", kScriptStart + 81.3f);
    ml.follow = Follow::Uav;
    ml.dist = 7.0f;
    ml.yaw = 140;
    ml.pitch = 12;
    add(ml);
    Shot sf = v4("thermal_fpv_strike", 0.0f);
    sf.director = 84.0f;
    sf.afterImpact = 0.0f;
    sf.width = 1920;
    add(sf);
    Shot e1 = cam(v4("explosion_early", 0.0f), {5.0f, 1.6f, 6.5f}, {0.0f, 1.2f, 0.0f}, 46.0f);
    e1.rel = true;
    e1.afterImpact = 0.15f;
    add(e1);
    Shot e2 = cam(v4("explosion_mid", 0.0f), {7.0f, 2.2f, 8.5f}, {0.0f, 1.8f, 0.0f}, 48.0f);
    e2.rel = true;
    e2.afterImpact = 1.0f;
    add(e2);
    Shot e3 = cam(v4("explosion_smoke", 0.0f), {10.0f, 3.0f, 12.0f}, {0.0f, 4.0f, 0.0f}, 52.0f);
    e3.rel = true;
    e3.afterImpact = 5.0f;
    add(e3);
    Shot e4 = cam(v4("wreck_burning", 0.0f), {2.4f, 0.9f, 2.8f}, {0.0f, 0.4f, 0.0f}, 46.0f);
    e4.rel = true;
    e4.afterImpact = 9.0f;
    add(e4);
    return s;
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
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream log(dir + "/capture_log.txt");
    if (!log) {
        std::fprintf(stderr, "Cannot write to %s\n", dir.c_str());
        return 1;
    }
    RenderTarget target;
    float t = 0.0f;
    float nextLog = 0.0f;
    float impactT = -1.0f;
    Vec3 strikePoint{0, 0, 0};
    // One simulation step with the capture log (rover / UAV trace once per simulated second).
    auto stepLogged = [&]() {
        if (t >= kScriptStart && scriptClock_ < 0.0f) scriptClock_ = 0.0f;  // the director script starts here
        step(kStep);
        t += kStep;
        if (t >= nextLog) {
            const DroneState& d = scene_->drone;
            const Vec3 r = sa::primaryRover(*scene_);
            char buf[256];
            std::snprintf(buf, sizeof(buf), "t=%5.1f uav=%-9s pos(%5.2f,%5.2f,%5.2f) blobs=%d found=%d rover=%s (%5.2f,%5.2f)\n",
                          t, droneModeName(d.mode), d.position.x, d.position.y, d.position.z, detection_.blobCount,
                          detection_.found, sa::roverHidden(*scene_) ? "HIDDEN " : "EXPOSED", r.x, r.z);
            log << buf;
            nextLog += 1.0f;
        }
    };
    // The scripted strike detonates some seconds after script time 81 s: run until the missile is gone.
    auto findImpact = [&]() {
        while (t < kScriptStart + 81.5f) stepLogged();
        bool seen = false;
        float waited = 0.0f;
        while (waited < 8.0f) {
            const Combat* combat = sa::combat(*scene_);
            bool flying = false;
            if (combat)
                for (const Projectile& p : combat->projectiles())
                    if (p.alive && p.kind == ProjectileKind::Missile) flying = true;
            if (flying) seen = true;
            if (seen && !flying) break;
            stepLogged();
            waited += kStep;
        }
        strikePoint = sa::primaryRover(*scene_);
        if (!seen || waited >= 8.0f) {  // no missile flew (no combat wired yet / timeout): detonate on the rover
            if (Combat* combat = sa::combat(*scene_)) combat->explode(strikePoint, 1.0f, 0);
        }
        impactT = t;
    };
    for (const Shot& shot : shots) {
        float shotTime = shot.time;
        if (shot.afterImpact >= 0.0f) {
            if (impactT < 0.0f) findImpact();
            shotTime = impactT + shot.afterImpact;
        }
        while (t + 0.5f * kStep < shotTime) stepLogged();
        if (!opt_.only.empty() && opt_.only.find("," + std::string(shot.file) + ",") == std::string::npos) continue;
        settings_ = RenderSettings{};
        settings_.view = shot.view;
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
        if (shot.follow == Follow::Action)  // between the bunker and the rover, so both agents are in frame
            camera_.target = lerp(Vec3{0, 0.8f, 0}, sa::primaryRover(*scene_), 0.45f) + Vec3{0, 0.6f, 0};

        if (shot.possess >= 0) {
            possessSlot(static_cast<Slot>(shot.possess));
            camera_.possessSight = shot.sight;
            if (shot.sight && shot.possess == static_cast<int>(Slot::Uav)) settings_.view = ViewMode::Thermal;
        }
        if (shot.custom) {  // fixed camera: eye -> aim (offsets from the strike point when 'rel')
            const Vec3 base = shot.rel ? strikePoint : Vec3{0, 0, 0};
            const Vec3 eye = base + shot.eye, aim = base + shot.aim, d = aim - eye;
            camera_.mode = CameraMode::Free;
            camera_.freePos = eye;
            camera_.freeYawDeg = degrees(std::atan2(d.x, d.z));
            camera_.freePitchDeg = degrees(std::asin(clampf(d.y / std::max(length(d), 1e-4f), -1.0f, 1.0f)));
            camera_.fovDeg = shot.camFov;
        }
        if (shot.director >= 0.0f) {
            camera_.mode = CameraMode::Cinematic;
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
         "gimbal_yaw,gimbal_pitch,rover_speed,rover_in_view,rover_u,rover_v,range,image_peak\n";
    const int steps = static_cast<int>(opt_.seconds / kStep);
    for (int i = 1; i <= steps; ++i) {
        step(kStep);
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
        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "%.2f,%s,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f,%d,%.3f,%.3f,%.2f,%d\n",
                      i * kStep, droneModeName(d.mode), detection_.found, detection_.blobCount, rhidden, d.position.x,
                      d.position.y, d.position.z, rpos.x, rpos.z, d.targetEstimate.x, d.targetEstimate.z,
                      err, d.gimbalYawDeg, d.gimbalPitchDeg, sa::roverSpeed(*scene_), inView, u, v,
                      length(motor - cv.position), peak);
        f << buf;
    }
    std::printf("Trace of %.0f s written to %s\n", opt_.seconds, path.c_str());
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
    auto frame = [&]() {
        step(kStep);
        if (headless_) offscreen.bind();
        else glBindFramebuffer(GL_FRAMEBUFFER, 0);
        renderFrame(opt_.width, opt_.height, true);
        present();
    };
    for (int i = 0; i < 120; ++i) frame();  // warm-up: 2 s of simulation
    // Three interleaved rounds over all phases; the median frame time per phase is reported, which is robust
    // against slow drift (thermal throttling, background load) on this integrated GPU.
    constexpr int kRounds = 5;
    constexpr double kSeconds = 4.0;
    for (int round = 0; round < kRounds; ++round) {
        for (Phase& p : phases) {
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
            const double start = nowSeconds();
            while (nowSeconds() - start < kSeconds && !closeRequested()) {
                frame();
                ++frames;
                p.sumDraws += stats_.drawCalls;
                p.sumShadowDraws += stats_.shadowDraws;
                p.sumCvDraws += cvStats_.drawCalls;
                p.sumTris += static_cast<double>(stats_.triangles);
                p.sumVerts += static_cast<double>(stats_.vertices);
            }
            glFinish();
            p.roundMs.push_back(1000.0 * (nowSeconds() - start) / std::max(frames, 1));
            p.frames += frames;
            p.main = stats_;
            p.cv = cvStats_;
            p.passes.clear();
            for (const GpuTimer::Entry& e : renderer_->timer().entries())
                if (e.samples > 0) p.passes.push_back(e);
        }
    }
    for (Phase& p : phases) {
        std::vector<double> sorted = p.roundMs;
        std::sort(sorted.begin(), sorted.end());
        p.frameMs = sorted[sorted.size() / 2];
        p.fps = 1000.0 / p.frameMs;
        std::printf("%-34s %6.1f FPS  %.3f ms/frame  (median of %d rounds, %d frames)\n", p.name, p.fps, p.frameMs,
                    kRounds, p.frames);
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
          << ", \"fps\": " << p.fps << ", \"frame_ms\": " << p.frameMs << ", \"frame_ms_rounds\": [";
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
