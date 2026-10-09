// v9 M0: fixed-shot benchmark (--bench-shots FILE). Five gameplay views (camp overview, UAV chase, soldier first /
// third person, inside the barracks hall), each by day (10:30) and by night (23:00), measured with the same frame as
// the interactive loop: simulation + UAV cameras, main frame + HUD, UAV image CV, present. Writes FILE (JSON) and
// FILE.md (table). Every milestone of v9 compares its numbers against the M0 baseline from this mode.
#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "app/App.h"
#include "app/HeadlessGL.h"
#include "scene/Garrison.h"

namespace ts {

namespace {

constexpr float kTick = 1.0f / 60.0f;

const Soldier* soldierOf(const Scene& scene, int index) {
    const std::vector<Controllable*> c = sa::controllables(scene);
    if (index < 0 || index >= static_cast<int>(c.size())) return nullptr;
    return dynamic_cast<const Soldier*>(c[index]);
}

double envNumber(const char* name, double fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::atof(v) : fallback;
}

}  // namespace

const std::vector<std::string>& App::benchShotNames() {
    static const std::vector<std::string> names = {"overview", "uav_chase", "soldier_1p", "soldier_3p", "hall_inside"};
    return names;
}

std::string App::stateLine() const {
    char line[512];
    const DayCycle* clock = sa::dayCycle(*scene_);
    const std::string when = clock ? clock->clockText() : std::string("--:--");
    const Soldier* s = soldierOf(*scene_, possessedSoldierIndex());
    if (s) {
        // v9 M3: + sim time, the motor's sprint / crouch blends, the FOV kick and the spring-arm length
        std::snprintf(line, sizeof(line),
                      "%s cam %s %s ctl %s pos %.2f %.2f %.2f yaw %.0f speed %.2f stance %d %s eye %.2f %.2f %.2f simt %.3f "
                      "sprint %.2f crouch %.2f fovkick %.2f arm %.2f",
                      when.c_str(), cameraModeName(camera_.mode), camera_.possessSight ? "FIRST" : "THIRD", s->name(),
                      s->position().x, s->position().y, s->position().z, s->yawDeg(), s->speed(), s->stance(),
                      s->airborne() ? "AIR" : "GROUND", shownPose_.eye.x, shownPose_.eye.y, shownPose_.eye.z, scene_->time(),
                      s->motor().sprintBlend(), s->motor().crouchBlend(), fovKick_, tp_.arm());
    } else {
        std::snprintf(line, sizeof(line), "%s cam %s ctl %s sel %u eye %.2f %.2f %.2f", when.c_str(), cameraModeName(camera_.mode),
                      possessed_ >= 0 ? possessedName() : "-", selKey_, shownPose_.eye.x, shownPose_.eye.y, shownPose_.eye.z);
    }
    return line;
}

bool App::setupBenchShot(const std::string& name) {
    possess(-1);
    held_ = ControlInput{};
    aiming_ = false;
    selKey_ = hoverKey_ = 0;
    playCapture_ = false;
    auto freeView = [&](const Vec3& eye, const Vec3& at, float fov) {
        camera_ = Camera{};
        camera_.mode = CameraMode::Free;
        camera_.freePos = eye;
        const Vec3 d = normalize(at - eye);
        camera_.freeYawDeg = degrees(std::atan2(d.x, d.z));
        camera_.freePitchDeg = degrees(std::asin(d.y));
        camera_.fovDeg = fov;
        rig_.cancel();
    };
    if (name == "overview") {
        freeView({-30.0f, 17.0f, 44.0f}, {2.0f, 0.5f, 4.0f}, 56.0f);
        return true;
    }
    if (name == "hall_inside") {
        freeView({27.0f, 1.9f, -9.2f}, {20.3f, 0.5f, -13.6f}, 72.0f);
        return true;
    }
    const std::vector<PovSource> list = listPovSources(*scene_, {0.0f, 2.0f, 0.0f});
    if (name == "uav_chase") {
        camera_ = Camera{};
        camera_.mode = CameraMode::Orbit;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].kind == PovKind::Uav && list[i].canControl) {
                selKey_ = list[i].key;
                controlSelected();
                rig_.cancel();
                return possessed_ >= 0;
            }
        return false;
    }
    if (name == "soldier_1p" || name == "soldier_3p") {
        int best = -1;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].kind == PovKind::Soldier && list[i].position.y > -0.5f && list[i].name.find("BARRACKS") == std::string::npos &&
                (best < 0 || list[i].distance < list[static_cast<size_t>(best)].distance))
                best = static_cast<int>(i);
        if (best < 0) return false;
        camera_ = Camera{};
        camera_.mode = CameraMode::Orbit;
        selKey_ = list[static_cast<size_t>(best)].key;
        controlSelected();
        playCapture_ = true;   // the v8 player paths (shapePlayerInput, updateOpenWorld) as in the interactive loop
        if (name == "soldier_3p") setViewThirdPerson(true);
        rig_.cancel();
        return possessedSoldierIndex() >= 0;
    }
    return false;
}

int App::benchShotsMode() {
    const int w = opt_.width, h = opt_.height;
    const double seconds = std::max(0.5, envNumber("TS_BENCH_SECONDS", 4.0));
    settings_ = RenderSettings{};
    settings_.quality = opt_.quality >= 0 ? static_cast<Quality>(opt_.quality) : autoQuality();
    settings_.view = ViewMode::Visible;
    showHud_ = true;
    showFeed_ = false;
    setSwapInterval(opt_.vsync >= 0 ? opt_.vsync : 0);
    RenderTarget offscreen;
    if (headless_ && !offscreen.create(w, h, 0, false)) return 1;
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    std::printf("bench-shots: %dx%d %s, %s, vsync %d, %.1f s per shot\n", w, h, qualityName(settings_.quality), gl ? gl : "?",
                opt_.vsync >= 0 ? opt_.vsync : 0, seconds);
    std::string onlyShots = opt_.only;   // --only overview,soldier_3p (optional)

    struct Result {
        std::string shot, time;
        bool ok = false;
        int frames = 0;
        double frameMs = 0, sim = 0, render = 0, vision = 0, present = 0;
        double draws = 0, shadowDraws = 0, tris = 0, blades = 0;
        std::vector<GpuTimer::Entry> passes;
    };
    std::vector<Result> results;
    auto frame = [&](Result* r) {
        const double t0 = nowSeconds();
        stepSim(kTick);
        updateGameCamera(kTick);
        const double t1 = nowSeconds();
        if (headless_) offscreen.bind();
        else glBindFramebuffer(GL_FRAMEBUFFER, 0);
        renderFrame(w, h, true);
        const double t2 = nowSeconds();
        stepVision();
        const double t3 = nowSeconds();
        present();
        const double t4 = nowSeconds();
        if (!r) return;
        r->sim += t1 - t0;
        r->render += t2 - t1;
        r->vision += t3 - t2;
        r->present += t4 - t3;
        r->draws += stats_.drawCalls;
        r->shadowDraws += stats_.shadowDraws;
        r->tris += static_cast<double>(stats_.triangles);
        r->blades += stats_.grassBlades;
        ++r->frames;
    };
    const struct {
        const char* name;
        float hour;
    } times[2] = {{"day", 10.5f}, {"night", 23.0f}};
    for (const auto& tod : times) {
        if (DayCycle* clock = sa::dayCycle(*scene_)) {
            clock->setAuto(false);
            clock->setHour(tod.hour);
        }
        setupBenchShot("overview");
        for (int i = 0; i < 180 && !closeRequested(); ++i) frame(nullptr);   // 3 s: lights, exposure, grass map
        for (const std::string& shot : benchShotNames()) {
            if (onlyShots.size() > 2 && onlyShots.find("," + shot + ",") == std::string::npos) continue;
            Result r;
            r.shot = shot;
            r.time = tod.name;
            r.ok = setupBenchShot(shot);
            if (!r.ok) {
                std::printf("  %-12s %-5s  setup FAILED\n", shot.c_str(), tod.name);
                results.push_back(r);
                continue;
            }
            for (int i = 0; i < 90 && !closeRequested(); ++i) frame(nullptr);   // 1.5 s: camera blend, TAA, exposure
            glFinish();
            renderer_->timer().resetTotals();
            const double start = nowSeconds();
            while (nowSeconds() - start < seconds && !closeRequested()) frame(&r);
            glFinish();
            const double wall = nowSeconds() - start;
            const int n = std::max(r.frames, 1);
            r.frameMs = 1000.0 * wall / n;
            r.sim *= 1000.0 / n;
            r.render *= 1000.0 / n;
            r.vision *= 1000.0 / n;
            r.present *= 1000.0 / n;
            r.draws /= n;
            r.shadowDraws /= n;
            r.tris /= n;
            r.blades /= n;
            for (const GpuTimer::Entry& e : renderer_->timer().entries())
                if (e.samples > 0) r.passes.push_back(e);
            std::printf("  %-12s %-5s %6.1f FPS %6.2f ms | cpu sim %.2f render %.2f vision %.2f present %.2f | %4.0f draws %5.2f M tris\n",
                        shot.c_str(), tod.name, 1000.0 / r.frameMs, r.frameMs, r.sim, r.render, r.vision, r.present, r.draws,
                        r.tris / 1e6);
            for (const GpuTimer::Entry& e : r.passes)
                std::printf("        %-18s %6.2f ms\n", e.name.c_str(), e.sumMs / e.samples);
            std::fflush(stdout);
            results.push_back(r);
        }
    }
    possess(-1);
    playCapture_ = false;

    const std::string path = opt_.outPath.empty() ? "build/bench_shots.json" : opt_.outPath;
    std::ofstream j(path);
    std::ofstream md(path + ".md");
    if (!j || !md) {
        std::fprintf(stderr, "bench-shots: cannot write %s\n", path.c_str());
        return 1;
    }
    j << "{\n  \"renderer\": \"" << (gl ? gl : "?") << "\",\n  \"quality\": \"" << qualityName(settings_.quality)
      << "\",\n  \"resolution\": \"" << w << "x" << h << "\",\n  \"vsync\": " << (opt_.vsync >= 0 ? opt_.vsync : 0)
      << ",\n  \"seconds_per_shot\": " << seconds << ",\n  \"shots\": [\n";
    md << "Renderer: " << (gl ? gl : "?") << ", " << qualityName(settings_.quality) << ", " << w << "x" << h << ", vsync "
       << (opt_.vsync >= 0 ? opt_.vsync : 0) << "\n\n"
       << "| shot | time | FPS | frame ms | CPU sim | CPU render | CPU vision | present | GPU passes ms | draws | shadow draws | triangles |\n"
       << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    bool allOk = true;
    for (size_t i = 0; i < results.size(); ++i) {
        const Result& r = results[i];
        allOk = allOk && r.ok;
        double gpu = 0.0;
        for (const GpuTimer::Entry& e : r.passes) gpu += e.sumMs / e.samples;
        j << "    {\"shot\": \"" << r.shot << "\", \"time\": \"" << r.time << "\", \"ok\": " << (r.ok ? "true" : "false")
          << ", \"frames\": " << r.frames << ", \"fps\": " << (r.frameMs > 0 ? 1000.0 / r.frameMs : 0.0)
          << ", \"frame_ms\": " << r.frameMs << ", \"cpu_sim_ms\": " << r.sim << ", \"cpu_render_ms\": " << r.render
          << ", \"cpu_vision_ms\": " << r.vision << ", \"present_ms\": " << r.present << ", \"gpu_sum_ms\": " << gpu
          << ", \"draws\": " << r.draws << ", \"shadow_draws\": " << r.shadowDraws << ", \"triangles\": " << r.tris
          << ", \"grass_blades\": " << r.blades << ", \"passes\": {";
        for (size_t k = 0; k < r.passes.size(); ++k)
            j << (k ? ", " : "") << "\"" << r.passes[k].name << "\": " << r.passes[k].sumMs / r.passes[k].samples;
        j << "}}" << (i + 1 < results.size() ? "," : "") << "\n";
        char row[400];
        std::snprintf(row, sizeof(row), "| %s | %s | %.1f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.0f | %.0f | %.2f M |\n",
                      r.shot.c_str(), r.time.c_str(), r.frameMs > 0 ? 1000.0 / r.frameMs : 0.0, r.frameMs, r.sim, r.render,
                      r.vision, r.present, gpu, r.draws, r.shadowDraws, r.tris / 1e6);
        md << row;
    }
    j << "  ]\n}\n";
    std::printf("bench-shots written to %s (+ .md)\n", path.c_str());
    return allOk ? 0 : 1;
}

}  // namespace ts
