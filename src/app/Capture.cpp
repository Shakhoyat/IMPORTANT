// Non-interactive modes: scripted screenshot capture (for the report) and the performance benchmark.
// Both run the real simulation (UAV camera, tracker, rover planner) with a fixed 60 Hz time step.
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <utility>

#include "app/App.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
    Follow follow = Follow::Action;
    float yaw = 100, pitch = 30, dist = 24;
    int width = 1600, height = 900;
};

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
             {"cine_establishing", 6.0f}, {"cine_gate", 14.0f}, {"cine_chase", 22.0f}, {"cine_ground", 30.0f},
             {"cine_watchtower", 43.0f}, {"cine_thermal_wide", 50.0f}, {"cine_low_angle", 58.0f}}) {
        Shot c{name, 44.0f};
        c.director = dt;
        c.width = 1920;
        c.height = 1080;
        add(c);
    }
    // Rain: the same cinematic shots and the thermal view in a downpour.
    for (const auto& [name, dt] : std::initializer_list<std::pair<const char*, float>>{
             {"rain_gate", 14.0f}, {"rain_chase", 22.0f}, {"rain_low_angle", 58.0f}}) {
        Shot c{name, 44.0f};
        c.director = dt;
        c.width = 1920;
        c.height = 1080;
        c.rain = true;
        add(c);
    }
    Shot rt{"rain_thermal", 44.0f}; rt.rain = true; add(rt);
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
    Shot u{"plan_topdown", 52.0f}; u.plan = true; u.view = ViewMode::Visible; u.pitch = 85; u.yaw = 0; u.dist = 44; u.feed = false; u.follow = Follow::None; add(u);
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
    for (const Shot& shot : shots) {
        while (t + 0.5f * kStep < shot.time) {  // advance the real simulation
            step(kStep);
            t += kStep;
            if (t >= nextLog) {
                const DroneState& d = scene_->drone;
                const RoverState& r = scene_->rover;
                char buf[256];
                std::snprintf(buf, sizeof(buf), "t=%5.1f uav=%-9s pos(%5.2f,%5.2f,%5.2f) blobs=%d found=%d rover=%s (%5.2f,%5.2f)\n",
                              t, droneModeName(d.mode), d.position.x, d.position.y, d.position.z, detection_.blobCount,
                              detection_.found, r.hidden ? "HIDDEN " : "EXPOSED", r.position.x, r.position.z);
                log << buf;
                nextLog += 1.0f;
            }
        }
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
        settings_.quality = Quality::Ultra;  // report figures: best quality (frame rate is irrelevant offline)
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
        camera_.mode = shot.cam;
        camera_.yawDeg = shot.yaw;
        camera_.pitchDeg = shot.pitch;
        camera_.distance = shot.dist;
        if (shot.follow == Follow::Uav) camera_.target = transformPoint(scene_->uav().root->world, {0, 0, 0});
        if (shot.follow == Follow::Rover) camera_.target = scene_->rover.position + Vec3{0, 0.2f, 0};
        if (shot.follow == Follow::Bunker) camera_.target = {0, 1.3f, 0};
        if (shot.follow == Follow::Action)  // between the bunker and the rover, so both agents are in frame
            camera_.target = lerp(Vec3{0, 0.8f, 0}, scene_->rover.position, 0.45f) + Vec3{0, 0.6f, 0};

        if (shot.director >= 0.0f) {
            camera_.mode = CameraMode::Cinematic;
            camera_.directorTime = shot.director;
            applyCinematic();
        }
        if (!target.create(shot.width, shot.height, 4, false)) return 1;
        target.bind();
        if (shot.library) {
            stats_ = {};
            glClearColor(0.075f, 0.08f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const int size = shot.width / Renderer::kLegendCount;
            renderer_->drawPrimitiveLegend(0, 0, size, 0.55f, stats_, true);
            glViewport(0, 0, shot.width, shot.height);
            drawLibraryLabels(renderer_->hud(), shot.width, shot.height);
        } else {
            renderFrame(shot.width, shot.height, true);
        }
        std::vector<uint8_t> rgb;
        target.read(rgb);
        const std::string path = dir + "/" + shot.file + ".png";
        const bool ok = writePng(path, shot.width, shot.height, rgb);
        std::printf("%s %s (t = %.1f s, %d draw calls, %lld triangles)\n", ok ? "wrote" : "FAILED", path.c_str(), t,
                    stats_.drawCalls + cvStats_.drawCalls, stats_.triangles + cvStats_.triangles);
        log << "shot " << shot.file << " at t=" << t << " draws=" << stats_.drawCalls + cvStats_.drawCalls
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
        const RoverState& r = scene_->rover;
        const Vec3 diff{d.targetEstimate.x - r.position.x, 0.0f, d.targetEstimate.z - r.position.z};
        const float err = detection_.found && d.hasEstimate ? length(diff) : -1.0f;
        // Diagnostics: where the rover's motor box projects in the UAV image and the hottest pixel seen.
        const CameraView cv = uavCameraView(*scene_, static_cast<float>(kCvWidth) / kCvHeight, kCvFovYDeg);
        const Vec3 motor = r.position + Vec3{0, 0.33f, 0};
        const Vec4 c = (cv.proj * cv.view) * Vec4{motor.x, motor.y, motor.z, 1.0f};
        const float u = c.w > 0 ? c.x / c.w : 9.0f, v = c.w > 0 ? c.y / c.w : 9.0f;
        const bool inView = c.w > 0 && std::fabs(u) <= 1.0f && std::fabs(v) <= 1.0f;
        int peak = 0;
        for (uint8_t px : uavImage_) peak = std::max(peak, static_cast<int>(px));
        char buf[400];
        std::snprintf(buf, sizeof(buf),
                      "%.2f,%s,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%.3f,%d,%.3f,%.3f,%.2f,%d\n",
                      i * kStep, droneModeName(d.mode), detection_.found, detection_.blobCount, r.hidden, d.position.x,
                      d.position.y, d.position.z, r.position.x, r.position.z, d.targetEstimate.x, d.targetEstimate.z,
                      err, d.gimbalYawDeg, d.gimbalPitchDeg, 0.5f * (r.leftSpeed + r.rightSpeed), inView, u, v,
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
    std::vector<Phase> phases(12);
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
    settings_.view = ViewMode::Visible;
    glfwSwapInterval(0);  // measure the real frame rate, not the monitor refresh
    // Background applications (editor, browser) compete for the CPU and the shared GPU; high priority keeps the
    // rounds comparable. Results are the median of 5 interleaved rounds; the spread is reported as the IQR.
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    for (int i = 0; i < 120; ++i) {  // warm-up: 2 s of simulation
        step(kStep);
        renderFrame(opt_.width, opt_.height, true);
        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
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
            settings_.rain = p.rain ? 1.0f : 0.0f;
            scene_->wetness = p.rain ? 1.0f : 0.0f;
            for (int i = 0; i < 20; ++i) {  // settle: new targets, query latency
                step(kStep);
                renderFrame(opt_.width, opt_.height, true);
                glfwSwapBuffers(window_);
                glfwPollEvents();
            }
            renderer_->timer().resetTotals();
            int frames = 0;
            const double start = glfwGetTime();
            while (glfwGetTime() - start < kSeconds && !glfwWindowShouldClose(window_)) {
                step(kStep);
                renderFrame(opt_.width, opt_.height, true);
                glfwSwapBuffers(window_);
                glfwPollEvents();
                ++frames;
                p.sumDraws += stats_.drawCalls;
                p.sumShadowDraws += stats_.shadowDraws;
                p.sumCvDraws += cvStats_.drawCalls;
                p.sumTris += static_cast<double>(stats_.triangles);
                p.sumVerts += static_cast<double>(stats_.vertices);
            }
            glFinish();
            p.roundMs.push_back(1000.0 * (glfwGetTime() - start) / std::max(frames, 1));
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
