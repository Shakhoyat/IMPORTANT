// Video recorder: runs the real simulation at 60 Hz, films it with the cinematic director camera and
// pipes 30 fps RGB frames into ffmpeg (H.264). Without ffmpeg the frames are written as PNG files.
#include <glad/gl.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

#include "app/App.h"
#include "app/HeadlessGL.h"
#include "audio/AudioEngine.h"
#include "audio/Wav.h"
#include "scene/Camera.h"
#include "scene/Director.h"
#include "scene/Garrison.h"
#include "objects/BaseLayout.h"
#include "scene/Pov.h"
#include "objects/Tank.h"
#include "objects/Terrain.h"
#include "rendering/FrameExplorer.h"
#include "rendering/ImageWriter.h"

namespace ts {

namespace {

// The paths go into a shell command line (cmd.exe or sh): refuse characters that could break the quoting or
// chain commands.
bool safeForShell(const std::string& s) { return !s.empty() && s.find_first_of("\"&|^<>%!$`;") == std::string::npos; }

FILE* openPipe(const std::string& cmd) {
#if defined(_WIN32)
    return _popen(("\"" + cmd + "\"").c_str(), "wb");  // cmd.exe strips one outer pair of quotes
#else
    return popen(cmd.c_str(), "w");
#endif
}

int closePipe(FILE* pipe) {
#if defined(_WIN32)
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
}

int runCommand(const std::string& cmd) {
#if defined(_WIN32)
    return std::system(("\"" + cmd + "\"").c_str());  // cmd.exe strips one outer pair of quotes
#else
    return std::system(cmd.c_str());
#endif
}

constexpr float kSimStep = 1.0f / 60.0f;

float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}

// v7 cut, ray-tracing segments: the showcase props of the raytrace_* figures (app/CaptureRT.cpp, WP-H), added to the
// scene's leaves (raster and ray tracer both see them) for the RT and Frame Explorer segments, then removed.
// Mirror of CaptureRT's Props: keep the two in step (or move both to a shared helper).
struct ShowcaseProps {
    std::vector<std::unique_ptr<SceneNode>> nodes;
    Scene* scene = nullptr;
    void add(PrimitiveType p, const Material& m, const Vec3& pos, const Vec3& size, const Vec3& rot, const char* name) {
        auto n = std::make_unique<SceneNode>();
        n->id = "RT-" + std::to_string(nodes.size());
        n->name = name;
        n->object = "RT showcase";
        n->primitive = p;
        n->shading = ShadingModel::Phong;
        n->material = m;
        n->world = translate(pos) * rotateEulerYXZ(rot) * scale(size);
        n->obstacle = false;
        nodes.push_back(std::move(n));
    }
    void install(Scene& s) {
        if (scene) return;
        scene = &s;
        const float x = kV7PropsX, z = kV7PropsZ, g = terrainHeight(x, z);
        Material glassM{{0.86f, 0.93f, 0.96f}, 0.2f, 0.0f, 0.9f, 128.0f};
        glassM.surface = Surface::Glass;
        glassM.roughness = 0.05f;
        Material mirrorM{{0.92f, 0.92f, 0.94f}, 0.2f, 0.0f, 0.9f, 128.0f};
        mirrorM.metalness = 1.0f;
        mirrorM.roughness = 0.04f;
        Material red{{0.75f, 0.10f, 0.08f}, 0.2f, 0.0f, 0.1f, 8.0f};
        red.roughness = 0.8f;
        add(PrimitiveType::Sphere, glassM, {x, g + 0.62f, z}, {1.2f, 1.2f, 1.2f}, {0, 0, 0}, "Glass ball");
        add(PrimitiveType::Sphere, mirrorM, {x + 1.5f, g + 0.62f, z - 0.6f}, {1.2f, 1.2f, 1.2f}, {0, 0, 0}, "Mirror ball");
        add(PrimitiveType::Cube, glassM, {x - 1.4f, g + 0.45f, z - 0.4f}, {0.9f, 0.9f, 0.9f}, {0, 30, 0}, "Glass block");
        add(PrimitiveType::Cylinder, mirrorM, {x + 0.4f, g + 0.9f, z - 1.6f}, {0.25f, 0.25f, 1.8f}, {90, 0, 0}, "Chrome post");
        add(PrimitiveType::Cube, red, {x + 0.5f, g + 0.35f, z + 1.1f}, {0.7f, 0.7f, 0.7f}, {0, 20, 0}, "Red block");
        ensure();
    }
    void ensure() {  // (re)append the props if the scene rebuilt its leaf list
        if (!scene) return;
        auto& lv = const_cast<std::vector<SceneNode*>&>(scene->leaves());
        for (auto& n : nodes)
            if (std::find(lv.begin(), lv.end(), n.get()) == lv.end()) lv.push_back(n.get());
    }
    void remove() {
        if (!scene) return;
        auto& lv = const_cast<std::vector<SceneNode*>&>(scene->leaves());
        for (auto& n : nodes) lv.erase(std::remove(lv.begin(), lv.end(), n.get()), lv.end());
        nodes.clear();
        scene = nullptr;
    }
    ~ShowcaseProps() { remove(); }
};

}  // namespace

int App::recordMode() {
    const int w = opt_.width, h = opt_.height;
    const std::string out = opt_.outPath.empty() ? "report/demo.mp4" : opt_.outPath;
    const int fps = opt_.fps >= 60 ? 60 : 30;
    const int stepsPerFrame = 60 / fps;  // 60 Hz simulation
    const int frames = static_cast<int>(opt_.seconds * static_cast<float>(fps));

    if (!safeForShell(out) || !safeForShell(opt_.ffmpeg) || !safeForShell(opt_.encoder)) {
        std::fprintf(stderr, "--record: output path, --ffmpeg and --encoder must not contain \" & | ^ < > %% ! $ ` ;\n");
        return 1;
    }
    // Warm-up (8 s at 23:00, no inputs): the first wave and the UAV patrol develop before the first frame; scripts
    // (and the clock of the v6 cut) are keyed to the director clock, which starts after it.
    restartWorld(8.0f, true);
    // Cut: TS_DIRECTOR_CUT = v7 (default) | v6 | v5. TS_DIRECTOR_START = director time of the first frame (s), and
    // TS_DIRECTOR_SPEED = director seconds per video second (previews / smoke tests of a part of the cut).
    const char* cutEnv = std::getenv("TS_DIRECTOR_CUT");
    const std::string cutName = cutEnv && *cutEnv ? cutEnv : "v7";
    const DirectorCut cut = cutName == "v5" ? DirectorCut::V5 : cutName == "v6" ? DirectorCut::V6 : DirectorCut::V7;
    const float dirStart = std::max(0.0f, envFloat("TS_DIRECTOR_START", 0.0f));
    const float dirSpeed = std::max(0.05f, envFloat("TS_DIRECTOR_SPEED", 1.0f));
    std::printf("Recording seed %u, director cut %s (%.0f s), from %.1f s at x%.2f\n", opt_.seed, cutName.c_str(),
                directorLength(cut), dirStart, dirSpeed);

    // Procedural sound track: rendered offline, one video frame of audio per rendered frame, muxed at the end.
    // TS_NO_AUDIO=1 disables it (silent video, as before).
    const bool wantAudio = std::getenv("TS_NO_AUDIO") == nullptr;
    const std::string stem = out.size() > 4 && out.compare(out.size() - 4, 4, ".mp4") == 0 ? out.substr(0, out.size() - 4) : out;
    const std::string wavPath = stem + "_audio.wav";
    const std::string silentPath = stem + "_noaudio.mp4";
    audio::AudioEngine audioEngine;
    audio::WavWriter wav;
    std::vector<float> audioBlock;
    bool audioOn = wantAudio && wav.open(wavPath);
    const std::string videoTarget = audioOn ? silentPath : out;

    // x264 at constant quality, or NVENC (--encoder h264_nvenc / hevc_nvenc) on an NVIDIA GPU.
    const bool nvenc = opt_.encoder.find("nvenc") != std::string::npos;
    const std::string codec = nvenc ? "-c:v " + opt_.encoder + " -preset p7 -tune hq -rc vbr -cq 18 -b:v 0 "
                                    : "-c:v " + opt_.encoder + " -preset slow -crf 17 ";
    const std::string cmd = "\"" + opt_.ffmpeg + "\" -y -loglevel error -f rawvideo -pix_fmt rgb24 -s " +
                            std::to_string(w) + "x" + std::to_string(h) + " -r " + std::to_string(fps) + " -i - " +
                            codec + "-pix_fmt yuv420p -movflags +faststart \"" + videoTarget + "\"";
    FILE* pipe = openPipe(cmd);
    std::string pngDir;
    if (!pipe) {
        pngDir = out + "_frames";
        std::filesystem::create_directories(pngDir);
        std::fprintf(stderr, "ffmpeg not available, writing PNG frames to %s\n", pngDir.c_str());
    }

    const Quality quality = settings_.quality;
    settings_ = RenderSettings{};
    settings_.quality = opt_.quality >= 0 ? quality : std::max(quality, Quality::Ultra);
    camera_ = Camera{};
    camera_.mode = CameraMode::Cinematic;
    camera_.directorCut = scriptCut_ = cut;
    camera_.directorTime = dirStart;
    // v5 / v6: the director script drives the clock, the helicopter and the doors (the squad fights by itself).
    // v7: the recorder sets the clock itself and performs the segment actions; the script stays off, otherwise its
    // (empty) inputs would override a possessed soldier's.
    scriptClock_ = cut == DirectorCut::V7 ? -1.0f : dirStart;
    // TS_FILM_CAM = "hour  ex0 ey0 ez0 ax0 ay0 az0  ex1 ey1 ez1 ax1 ay1 az1  fov": a free camera that glides from the first
    // pose to the second over the recording at a fixed clock, the director script off (the world runs by itself).
    // Report film shots of places the cuts do not visit (the barracks hall, the posts). Use it with TS_DIRECTOR_CUT=v6.
    float film[14] = {};
    bool filmCam = false;
    if (const char* fc = std::getenv("TS_FILM_CAM")) {
        filmCam = std::sscanf(fc, "%f %f %f %f %f %f %f %f %f %f %f %f %f %f", &film[0], &film[1], &film[2], &film[3], &film[4],
                              &film[5], &film[6], &film[7], &film[8], &film[9], &film[10], &film[11], &film[12], &film[13]) == 14 &&
                  cut != DirectorCut::V7;
        if (filmCam) {
            showHud_ = false;     // a clean cinema frame: no game HUD, banners or camera feeds
            showFeed_ = false;
            scriptClock_ = -1.0f;
            // a running clock from a daytime hour: start from a day world (UAVs docked, no wave alive), so the dusk
            // launch happens on screen; the warm-up above started the night
            if (std::getenv("TS_FILM_AUTO") && film[0] > 6.5f && film[0] < 19.0f) restartWorld(2.0f, false);
            if (DayCycle* clock = sa::dayCycle(*scene_)) {
                clock->setAuto(false);
                clock->setHour(film[0]);
                clock->setAuto(std::getenv("TS_FILM_AUTO") != nullptr);   // let the day run: dusk launches, dawn landings
            }
            for (int k = 0; k < 120; ++k) step(kSimStep);   // 2 s: lighting and the garrison settle at the new hour
        }
    }
    // TS_TOUR = route file: a soldier explores in third person, the recorder steering him like a player would (camera-
    // relative walk, the arm easing behind). Lines: "hour H", "soldier ROSTER", "doors MASK" (1 blast, 2 hall, 4 generator),
    // "start X Z YAW [FEETY]", then "p X Z [run]" waypoints. The garrison's own soldiers keep their duties around him.
    // The script is played like a player would: besides the waypoints, "face X Z" (turn to a point), "use" (E: open the
    // door in front / get into the vehicle in front), "wait S", "view first|third", and inside a vehicle "drive X Z"
    // (waypoint), "aim X Y Z" (lay the gun / launcher on a world point and hold it; "aim off"), "sight" / "chase" (gunner's
    // sight or chase camera), "fire S" (trigger held S seconds), "action" (the vehicle's secondary action) and "exit".
    // "turn YAW" pivots the tank to a heading; "cam BACK UP SIDE" films the vehicle from a low follow camera (under the
    // camp nets, where the game's own chase camera sits above the net), "cam off" gives the view back to the game.
    // "camfix X Y Z" = a camera standing at that point, turning with the vehicle.
    enum class TourOp { Walk, Face, Use, Wait, View, Drive, Aim, AimOff, Sight, Chase, Fire, Action, Exit, Turn, Cam, CamOff, Hud, CamFix };
    struct TourPt {
        float x, z;
        bool run;
        TourOp op = TourOp::Walk;
        float y = 0.0f, t = 0.0f;   // third parameter; time spent in the step
    };
    bool aimOn = false, followOn = false, followValid = false, followFixed = false;
    Vec3 aimAt{}, followArm{}, followEye{}, followAim{};
    std::vector<TourPt> tour;
    std::vector<size_t> routed;   // indices of "g" points (expanded after the planner is built)
    size_t tourK = 0;
    float tourStuck = 0.0f, tourLastD = 1e9f;
    bool tourOn = false;
    if (const char* tp = std::getenv("TS_TOUR")) {
        std::ifstream tin(tp);
        std::string line;
        float hour = 10.5f, sx = 0, sz = 0, syaw = 0, sfeet = -1e9f;
        int roster = 0, doors = 0;
        while (std::getline(tin, line)) {
            std::istringstream ls(line);
            std::string k;
            ls >> k;
            if (k == "hour") ls >> hour;
            else if (k == "soldier") ls >> roster;
            else if (k == "doors") ls >> doors;
            else if (k == "start") { ls >> sx >> sz >> syaw; if (!(ls >> sfeet)) sfeet = -1e9f; }
            else if (k == "p" || k == "g") {   // g: routed by the soldiers' A* planner (clear of every obstacle)
                TourPt q{0, 0, false};
                std::string r;
                ls >> q.x >> q.z >> r;
                q.run = r == "run";
                if (k == "g") q.run = q.run, routed.push_back(tour.size());
                tour.push_back(q);
            } else if (k == "face" || k == "drive") {
                TourPt q{0, 0, false};
                q.op = k == "face" ? TourOp::Face : TourOp::Drive;
                ls >> q.x >> q.z;
                tour.push_back(q);
            } else if (k == "aim") {
                TourPt q{0, 0, false};
                std::string first;
                ls >> first;
                if (first == "off") q.op = TourOp::AimOff;
                else {
                    q.op = TourOp::Aim;
                    q.x = static_cast<float>(std::atof(first.c_str()));
                    ls >> q.y >> q.z;
                }
                tour.push_back(q);
            } else if (k == "wait" || k == "fire" || k == "turn" || k == "hud") {
                TourPt q{0, 0, false};
                q.op = k == "wait" ? TourOp::Wait : k == "fire" ? TourOp::Fire : k == "turn" ? TourOp::Turn : TourOp::Hud;
                ls >> q.x;
                tour.push_back(q);
            } else if (k == "cam" || k == "camfix") {
                TourPt q{0, 0, false};
                std::string first;
                ls >> first;
                if (first == "off") q.op = TourOp::CamOff;
                else {
                    q.op = k == "camfix" ? TourOp::CamFix : TourOp::Cam;
                    q.x = static_cast<float>(std::atof(first.c_str()));
                    ls >> q.y >> q.z;
                }
                tour.push_back(q);
            } else if (k == "view") {
                TourPt q{0, 0, false};
                std::string r;
                ls >> r;
                q.op = TourOp::View;
                q.run = r == "third";
                tour.push_back(q);
            } else if (k == "use" || k == "sight" || k == "chase" || k == "action" || k == "exit") {
                TourPt q{0, 0, false};
                q.op = k == "use" ? TourOp::Use : k == "sight" ? TourOp::Sight : k == "chase" ? TourOp::Chase :
                       k == "action" ? TourOp::Action : TourOp::Exit;
                tour.push_back(q);
            }
        }
        tourOn = !tour.empty() && cut != DirectorCut::V7;
        if (tourOn) {
            scriptClock_ = -1.0f;
            showHud_ = false;
            showFeed_ = false;
            playCapture_ = true;   // the player's camera-relative walk (shapePlayerInput)
            if (DayCycle* clock = sa::dayCycle(*scene_)) {
                clock->setAuto(false);
                clock->setHour(hour);
            }
            if (doors & 1) {
                const std::vector<Controllable*> list = sa::controllables(*scene_);
                const int i = resolveSlot(list, Slot::Doors);
                if (i >= 0 && list[i]->status().find("OPEN") == std::string::npos) {
                    ControlInput din;
                    din.actionPressed = true;
                    WorldContext wc;
                    list[i]->update(wc, &din);
                }
            }
            for (const auto& [bit, door] : {std::pair<int, DoorPortal*>{2, scene_->hallDoor()}, {4, scene_->generatorDoor()}})
                if ((doors & bit) && door) {
                    door->door().setTarget(true, true);
                    door->door().snap(door->door().params().openDeg);
                }
            for (int k = 0; k < 300; ++k) step(kSimStep);   // 5 s: lighting, doors and the garrison settle
            if (const Garrison* g = scene_->garrison(); g && !routed.empty()) {
                std::vector<TourPt> out;
                Vec3 from{sx, 0.0f, sz};
                for (size_t i = 0; i < tour.size(); ++i) {
                    const Vec3 to{tour[i].x, 0.0f, tour[i].z};
                    std::vector<Vec3> cells;
                    if (tour[i].op != TourOp::Walk) {   // not a waypoint: kept in place
                        out.push_back(tour[i]);
                        continue;
                    }
                    if (std::find(routed.begin(), routed.end(), i) != routed.end() && g->planner().gridPath(from, to, {}, cells)) {
                        const std::vector<int> keep = g->planner().stringPull(cells, {});
                        for (size_t k = 1; k + 1 < keep.size(); ++k)
                            out.push_back({cells[static_cast<size_t>(keep[k])].x, cells[static_cast<size_t>(keep[k])].z, tour[i].run});
                        std::printf("tour: routed to (%.1f, %.1f) through %zu turns\n", to.x, to.z, keep.size() > 2 ? keep.size() - 2 : 0);
                    }
                    out.push_back(tour[i]);
                    from = to;
                }
                tour = out;
            }
            const int ci = sa::soldierIndex(*scene_, roster);
            if (Garrison* g = scene_->garrison())
                for (int u = 0; u < g->count(); ++u)
                    if (sa::soldierIndex(*scene_, u) == ci) g->unit(u)->place(sx, sz, syaw, sfeet);
            possess(ci);
            camera_.possessSight = false;
            setViewThirdPerson(true);
            tp_.invalidate();
            for (int k = 0; k < 60; ++k) { step(kSimStep); updateGameCamera(kSimStep); }
            std::printf("tour: soldier %d at (%.1f, %.1f), %zu waypoints, %s\n", roster, sx, sz, tour.size(), possessedName());
        }
    }
    if (const char* dump = std::getenv("TS_DUMP_SOLIDS")) {   // route planning for the film takes: the static solid boxes
        std::ofstream o(dump);
        for (const Box& b : scene_->staticSolids())
            o << b.min.x << ' ' << b.min.y << ' ' << b.min.z << ' ' << b.max.x << ' ' << b.max.y << ' ' << b.max.z << '\n';
    }
    // TS_FILM_VIEW (with TS_FILM_CAM, which gives the hour and the fallback camera): "follow" = a slow orbit round the
    // squad's target (the glide while there is none); "uav0" / "uav1" = that UAV's thermal gimbal view with the game HUD.
    const char* fvEnv = std::getenv("TS_FILM_VIEW");
    const std::string filmView = filmCam && fvEnv ? fvEnv : "";
    const int filmUav = filmView == "uav0" ? 0 : filmView == "uav1" ? 1 : -1;
    Vec3 followEye2{}, followAim2{};
    float followHold = -1e9f;
    bool followLive = false;
    const bool filmClean = std::getenv("TS_FILM_CLEAN") != nullptr;   // v7 cut without its segment titles and letterbox
    RenderTarget target;
    if (!target.create(w, h, 0, false)) return 1;  // the renderer anti-aliases (MSAA + TAA) before this target
    std::vector<uint8_t> rgb;
    const float aspect = static_cast<float>(w) / static_cast<float>(h);

    // ---- v7 segment actions ---------------------------------------------------------------------------------------
    ShowcaseProps props;
    std::unique_ptr<FrameExplorer> explorer;
    int segment = -1;
    bool treePicked = false;
    float pickerT = 0.0f;
    auto softReset = [&]() {  // between POV segments: keep the shown pose (the next jump blends from it)
        held_ = ControlInput{};
        releaseRover();
        possess(-1);
        pickerOpen_ = false;
        flyCam_ = false;
    };
    auto fullReset = [&]() {
        softReset();
        camera_ = Camera{};
        camera_.directorCut = cut;
        povHold_ = -1.0f;
        rig_.cancel();
        fovKick_ = letterbox_ = 0.0f;
        gameMode_ = false;
        showHud_ = true;
        settings_.rtMode = rt::RtMode::Raster;
        renderer_->setRayDebug(false);
    };
    auto setFree = [&](const Vec3& eye, const Vec3& target, float fov) {
        const Vec3 d = target - eye;
        camera_.mode = CameraMode::Free;
        camera_.freePos = eye;
        camera_.freeYawDeg = degrees(std::atan2(d.x, d.z));
        camera_.freePitchDeg = degrees(std::asin(clampf(d.y / std::max(length(d), 1e-4f), -1.0f, 1.0f)));
        camera_.fovDeg = fov;
    };
    auto findPov = [&](PovKind kind, const char* nameHas = "", const Vec3* near = nullptr) -> uint32_t {
        uint32_t best = 0;
        float bestD = 1e9f;
        for (const PovSource& p : listPovSources(*scene_, shownPose_.eye)) {
            if (p.kind != kind || (nameHas[0] && p.name.find(nameHas) == std::string::npos)) continue;
            const float d = near ? length(p.position - *near) : p.distance;
            if (d < bestD) {
                bestD = d;
                best = p.key;
            }
        }
        return best;
    };
    auto isPov = [](V7Act a) {
        return a == V7Act::Picker || a == V7Act::PovUav || a == V7Act::PovMissile || a == V7Act::PovRover || a == V7Act::SoldierFire;
    };
    auto rtBlock = [](int seg) { return seg >= kV7RtRaster && seg <= kV7ExFinal; };
    auto enter = [&](const DirectorShot& sh, int prev) {
        const V7Act prevAct = prev >= 0 ? directorShot(*scene_, directorShotStart(prev, cut), cut).act : V7Act::Camera;
        if (isPov(sh.act) && isPov(prevAct)) softReset();
        else fullReset();
        if (!rtBlock(sh.script)) props.remove();
        if (sh.act != V7Act::Explorer && explorer) {
            explorer->close();
            explorer.reset();
        }
        switch (sh.act) {
            case V7Act::Picker:
                gameMode_ = true;
                setFree(sh.eye, sh.target, sh.fovDeg);
                togglePicker();
                pickerT = 0.0f;
                break;
            case V7Act::PovUav: {
                gameMode_ = true;
                uint32_t wing = 0;
                for (const PovSource& p : listPovSources(*scene_, shownPose_.eye))
                    if (p.kind == PovKind::Uav && p.uav == 1) wing = p.key;
                if (!wing) wing = findPov(PovKind::Uav);
                if (wing) jumpToPov(wing, static_cast<int>(PovView::First), true);
                break;
            }
            case V7Act::PovMissile:
                gameMode_ = true;  // jumps when a round is in the air (the per-frame update below), or keeps riding
                break;
            case V7Act::PovRover: {
                gameMode_ = true;
                const Squad& sq = scene_->squad();
                Vec3 aim = shownPose_.eye;
                if (const Track* t = sq.tracker().find(sq.targetTrack())) aim = t->position();
                // mast camera (the rover hides in cover, so foliage often frames the view; the chase view is no better)
                if (const uint32_t r = findPov(PovKind::Rover, "", &aim)) jumpToPov(r, static_cast<int>(PovView::Alt), true);
                break;
            }
            case V7Act::SoldierFire:
                gameMode_ = true;
                if (uint32_t sol = findPov(PovKind::Soldier, "SENTRY") ? findPov(PovKind::Soldier, "SENTRY") : findPov(PovKind::Soldier))
                    jumpToPov(sol, static_cast<int>(PovView::First), true);
                break;
            case V7Act::RtMode: case V7Act::RtTree: case V7Act::Explorer:
                props.install(*scene_);
                showHud_ = false;
                settings_.rtMode = sh.act == V7Act::Explorer ? rt::RtMode::Raster : static_cast<rt::RtMode>(sh.param);
                treePicked = false;
                if (sh.act == V7Act::Explorer) {
                    if (!explorer) {  // freeze the showcase frame once; the four Explorer segments step through it
                        explorer = std::make_unique<FrameExplorer>();
                        CameraView cv;
                        cv.view = lookAt(sh.eye, sh.target, {0, 1, 0});
                        cv.proj = perspective(radians(sh.fovDeg), aspect, 0.05f, 600.0f);
                        cv.position = sh.eye;
                        explorer->setConvergence(8, false);
                        if (!explorer->open(*renderer_, *scene_, cv, settings_, w, h)) {
                            std::fprintf(stderr, "v7 cut: the Frame Explorer could not freeze the frame\n");
                            explorer.reset();
                        }
                    }
                    if (explorer) {
                        explorer->setStage(sh.param);
                        explorer->setTransition(0.0f);
                    }
                }
                break;
            case V7Act::Camera: break;
        }
    };

    // TS_FILM_SCRIPT = file (with TS_FILM_CAM, which gives the hour and the opening camera): timed steps, one per line,
    // "T step args" (T = video seconds; "+D" = D seconds after the previous step; '#' starts a comment). A step does what
    // a key or the mouse does in the program, through the same functions, so a take shows the program as a player uses it:
    //   set NAME VALUE                         a switch of the key map (view, edges, shading, depth, axes, plan, explode,
    //                                          quality, pbr, cinematic, rt, lightterms, projection, taa, ssr, hud, rain ...)
    //   cam EX EY EZ AX AY AZ FOV [S]          free camera glides to this pose in S seconds (0 = cut)
    //   orbit CX CY CZ R H RATE DEG0 FOV       orbit a point; track KIND NAME R H RATE DEG0 FOV orbits a moving object
    //   pov KIND NAME [first|chase|alt]        jump into an object's view (F4 list / middle click); next / prev (Insert / Delete)
    //   control KIND NAME, view, release       take control (Enter), V, Backspace
    //   picker open|close|down N|up N|enter    the POV list (F4)
    //   cursor show|hide | cursor X Y [S] | cursorto KIND NAME [S] | cursorrow N [S]     the pointer, drawn in the frame
    //   click left|double|middle               select / take control / jump into the object (or list row) under the pointer
    //   hold AXIS VALUE S (forward turn lift aimyaw aimpitch strafe boost), fire S, action, use, look DX DY S
    //   menu open|close|tab N, explorer STAGE [S]|off, raypick X Y, hour H, scrub H0 H1 S, clock auto|manual, dayops 0|1
    // Every step is written with its video time to <video>_steps.txt (titles and key badges of the film are cut to it).
    struct FilmStep {
        float t = 0.0f;
        std::string op, line;
        std::vector<std::string> a;
    };
    struct FilmPose {
        Vec3 eye, aim;
        float fov = 50.0f;
    };
    std::vector<FilmStep> fsteps;
    size_t fsK = 0;
    bool fsOn = false;
    std::ofstream fsLog;
    FilmPose fsFrom{Vec3{film[1], film[2], film[3]}, Vec3{film[4], film[5], film[6]}, film[13]}, fsTo = fsFrom, fsShown = fsFrom;
    float fsCamT0 = 0.0f, fsCamDur = 0.0f;
    int fsCamMode = 0;   // 0 keyed poses, 1 orbit round a point, 2 orbit round a moving object
    bool fsCamCut = true;
    Vec3 fsOrbC{};
    float fsOrbR = 10.0f, fsOrbH = 4.0f, fsOrbRate = 6.0f, fsOrbDeg0 = 0.0f, fsOrbT0 = 0.0f;
    uint32_t fsOrbKey = 0;
    float fsHoldV[6] = {}, fsHoldUntil[6] = {-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f}, fsBoostUntil = -1.0f, fsFireUntil = -1.0f;
    float fsNucT0 = -1.0f;   // the thermal sensor's shutter closed at this time (step "nuc", the Q key)
    float fsLookDx = 0.0f, fsLookDy = 0.0f, fsLookUntil = -1.0f;
    bool fsCurShow = false;
    float fsCurX = 0.5f, fsCurY = 0.5f, fsCurX0 = 0.5f, fsCurY0 = 0.5f, fsCurX1 = 0.5f, fsCurY1 = 0.5f, fsCurT0 = 0.0f, fsCurDur = 0.0f;
    uint32_t fsCurKey = 0;
    int fsCurRow = -1;
    float fsClickT = -10.0f;
    float fsScrubH0 = 0.0f, fsScrubH1 = 0.0f, fsScrubT0 = 0.0f, fsScrubDur = 0.0f;
    std::unique_ptr<FrameExplorer> fsExplorer;
    int fsExStage = 0;
    float fsExT0 = 0.0f, fsExLen = 6.0f;
    bool fsRayPick = false;
    FilmStep fsWait;
    float fsWaitUntil = -1.0f;
    float fsRayX = 0.5f, fsRayY = 0.5f;
    if (const char* fp = std::getenv("TS_FILM_SCRIPT"); fp && filmCam) {
        std::ifstream in(fp);
        std::string line;
        float prev = 0.0f;
        while (std::getline(in, line)) {
            const size_t hash = line.find('#');
            if (hash != std::string::npos) line.erase(hash);
            std::istringstream ls(line);
            std::string ts;
            FilmStep st;
            if (!(ls >> ts >> st.op)) continue;
            st.t = ts[0] == '+' ? prev + static_cast<float>(std::atof(ts.c_str() + 1)) : static_cast<float>(std::atof(ts.c_str()));
            prev = st.t;
            for (std::string v; ls >> v;) st.a.push_back(v);
            st.line = line;
            fsteps.push_back(st);
        }
        std::stable_sort(fsteps.begin(), fsteps.end(), [](const FilmStep& x, const FilmStep& y) { return x.t < y.t; });
        fsOn = !fsteps.empty();
        if (fsOn) {
            fsLog.open(stem + "_steps.txt");
            std::printf("film script %s: %zu steps\n", fp, fsteps.size());
            setFree(fsFrom.eye, fsFrom.aim, fsFrom.fov);   // the script's camera from the first frame (not the director's)
        }
    }
    auto fsNum = [](const FilmStep& st, size_t i, float def) { return i < st.a.size() ? static_cast<float>(std::atof(st.a[i].c_str())) : def; };
    auto fsStr = [](const FilmStep& st, size_t i) { return i < st.a.size() ? st.a[i] : std::string(); };
    auto fsKind = [](const std::string& k, PovKind& out) {
        static const std::pair<const char*, PovKind> names[] = {
            {"soldier", PovKind::Soldier}, {"rover", PovKind::Rover}, {"uav", PovKind::Uav}, {"helicopter", PovKind::Helicopter},
            {"tank", PovKind::Tank}, {"aagun", PovKind::AaGun}, {"launcher", PovKind::Launcher}, {"radar", PovKind::Radar},
            {"missile", PovKind::Missile}, {"shell", PovKind::Shell}};
        for (const auto& [n, v] : names)
            if (k == n) {
                out = v;
                return true;
            }
        return false;
    };
    // the object a step names: KIND and a part of its name ("-" = the nearest of that kind)
    auto fsSource = [&](const FilmStep& st, size_t i, PovSource& out) {
        PovKind kind;
        if (!fsKind(fsStr(st, i), kind)) return false;
        const std::string name = fsStr(st, i + 1) == "-" ? std::string() : fsStr(st, i + 1);
        const uint32_t key = findPov(kind, name.c_str());
        for (const PovSource& p : listPovSources(*scene_, shownPose_.eye))
            if (key != 0 && p.key == key) {
                out = p;
                return true;
            }
        return false;
    };
    auto fsProject = [&](const Vec3& p, float& nx, float& ny) {
        const Mat4 vp = perspective(radians(shownPose_.fovDeg + fovKick_), aspect, shownPose_.nearZ, 600.0f) * shownPose_.view();
        const Vec4 c = vp * Vec4{p.x, p.y, p.z, 1.0f};
        if (c.w <= 1e-4f) return false;
        nx = 0.5f * (c.x / c.w + 1.0f);
        ny = 0.5f * (1.0f - c.y / c.w);
        return nx > 0.0f && nx < 1.0f && ny > 0.0f && ny < 1.0f;
    };
    auto fsFreeFromShown = [&]() {   // the script's camera takes over from the game's, from the pose on screen
        releaseRover();
        possess(-1);
        pickerOpen_ = false;
        flyCam_ = false;
        fsShown = FilmPose{shownPose_.eye, shownPose_.eye + shownPose_.forward() * 12.0f, shownPose_.fovDeg};
        camera_.mode = CameraMode::Free;
    };
    auto fsSet = [&](const std::string& n, const std::string& v) {
        const float x = static_cast<float>(std::atof(v.c_str()));
        const bool on = x != 0.0f || v == "on";
        auto pick = [&](std::initializer_list<const char*> names) {
            int i = 0;
            for (const char* c : names) {
                if (v == c) return i;
                ++i;
            }
            return static_cast<int>(x);
        };
        if (n == "view") settings_.view = static_cast<ViewMode>(pick({"thermal", "visible", "prims", "zbuf", "heatraw"}));
        else if (n == "edges") settings_.edges = static_cast<EdgeMode>(pick({"solid", "hidden", "xray"}));
        else if (n == "shading") settings_.shading = v == "flat" ? ShadingOverride::Flat : v == "gouraud" ? ShadingOverride::Gouraud
                                                     : v == "phong" ? ShadingOverride::Phong : ShadingOverride::PerObject;
        else if (n == "depth") settings_.depthTest = on;
        else if (n == "indexed") settings_.indexed = on;
        else if (n == "axes") settings_.showAxes = on;
        else if (n == "plan") settings_.showPlan = on;
        else if (n == "legend") settings_.showLegend = on;
        else if (n == "moon") settings_.moonOn = on;
        else if (n == "beacon") settings_.beaconOn = on;
        else if (n == "spot") settings_.spotOn = on;
        else if (n == "practicals") settings_.practicalsOn = on;
        else if (n == "quality") settings_.quality = static_cast<Quality>(std::clamp(pick({"low", "high", "ultra", "epic"}), 0, kQualityCount - 1));
        else if (n == "pbr") settings_.pbr = on;
        else if (n == "cinematic") settings_.cinematic = on;
        else if (n == "palette") settings_.palette = static_cast<int>(x) % 3;   // ironbow, white-hot, rainbow
        else if (n == "taa") settings_.taaOn = on;
        else if (n == "msaa") settings_.msaaOn = on;
        else if (n == "ssr") settings_.ssrOn = on;
        else if (n == "flare") settings_.flareOn = on;
        else if (n == "autoexposure") settings_.autoExposureOn = on;
        else if (n == "exposure") settings_.exposure = x;
        else if (n == "dof") settings_.dof = x;
        else if (n == "foliage") settings_.foliageOn = on;
        else if (n == "particles") settings_.particlesOn = on;
        else if (n == "shadows") settings_.shadowsOn = on;
        else if (n == "ssao") settings_.ssaoOn = on;
        else if (n == "fog") settings_.fogOn = on;
        else if (n == "bloom") settings_.bloomOn = on;
        else if (n == "bump") settings_.bumpOn = on;
        else if (n == "pcss") settings_.pcssOn = on;
        else if (n == "contact") settings_.contactShadowsOn = on;
        else if (n == "gtao") settings_.gtaoOn = on;
        else if (n == "aerial") settings_.aerialOn = on;
        else if (n == "spotshadow") settings_.spotShadowOn = on;
        else if (n == "vegetation") settings_.vegetationOn = on;
        else if (n == "motionblur") settings_.motionBlur = x;
        else if (n == "rtscale") settings_.rtScale = x, settings_.rtWhittedScale = x;
        else if (n == "rt") settings_.rtMode = static_cast<rt::RtMode>(pick({"raster", "hybrid", "whitted", "path", "light"}));
        else if (n == "rtoverlay") settings_.rtOverlay = on;
        else if (n == "raydebug") renderer_->setRayDebug(on);
        else if (n == "lightterms") settings_.lightTerms = pick({"all", "ambient", "diffuse", "specular"}) % 4;
        else if (n == "projection") settings_.projection = pick({"perspective", "top", "front", "side", "isometric"}) % 5;
        else if (n == "explode") exploded_ = on;
        else if (n == "hud") {
            showHud_ = x >= 1.0f;
            debugHud_ = x >= 2.0f;
        } else if (n == "feed") showFeed_ = on;
        else if (n == "game") gameMode_ = on;
        else if (n == "help") showHelp_ = on;
        else if (n == "fpv") thermalFpv_ = on;
        else if (n == "zoom") thermalFovDeg_ = clampf(x, 5.0f, 40.0f);   // the thermal camera's zoom steps (wheel: 40 / 20 / 8 degrees)
        else if (n == "rain") rainTarget_ = on ? 1.0f : 0.0f;
        else if (n == "props") {
            if (on) props.install(*scene_);
        } else return false;
        return true;
    };
    auto fsRun = [&](const FilmStep& st, float t) {
        bool ok = true;
        std::string note;
        if (st.op == "set") ok = fsSet(fsStr(st, 0), fsStr(st, 1));
        else if (st.op == "cam") {
            if (camera_.mode != CameraMode::Free) fsFreeFromShown();
            fsFrom = fsShown;
            fsTo = FilmPose{Vec3{fsNum(st, 0, 0), fsNum(st, 1, 5), fsNum(st, 2, 0)}, Vec3{fsNum(st, 3, 0), fsNum(st, 4, 0), fsNum(st, 5, 0)}, fsNum(st, 6, 50)};
            fsCamT0 = t;
            fsCamDur = fsNum(st, 7, 0.0f);
            fsCamMode = 0;
            fsCamCut = fsCamDur <= 0.0f;
        } else if (st.op == "orbit" || st.op == "track") {
            size_t i = 0;
            if (st.op == "track") {
                PovSource src;
                ok = fsSource(st, 0, src);
                fsOrbKey = ok ? src.key : 0;
                fsOrbC = src.centre;
                i = 2;
            } else {
                fsOrbC = Vec3{fsNum(st, 0, 0), fsNum(st, 1, 0), fsNum(st, 2, 0)};
                i = 3;
            }
            if (ok) {
                if (camera_.mode != CameraMode::Free) fsFreeFromShown();
                fsOrbR = fsNum(st, i, 10.0f);
                fsOrbH = fsNum(st, i + 1, 4.0f);
                fsOrbRate = fsNum(st, i + 2, 6.0f);
                fsOrbDeg0 = fsNum(st, i + 3, 0.0f);
                fsTo.fov = fsNum(st, i + 4, 50.0f);
                fsOrbT0 = t;
                fsCamMode = st.op == "track" ? 2 : 1;
                fsCamCut = fsStr(st, i + 5) == "cut";
            }
        } else if (st.op == "pov" || st.op == "control") {
            PovSource src;
            ok = fsSource(st, 0, src);
            if (ok && st.op == "pov") {
                const std::string v = fsStr(st, 2);
                ok = jumpToPov(src.key, v == "chase" ? 1 : v == "alt" ? 2 : v == "first" ? 0 : -1, true);
            } else if (ok) {
                const std::vector<PovSource> list = listPovSources(*scene_, shownPose_.eye);
                int at = -1;
                for (size_t k = 0; k < list.size(); ++k)
                    if (list[k].key == src.key) at = static_cast<int>(k);
                ok = controlSource(at);
            }
            note = src.name;
        } else if (st.op == "possess") {   // any object that can be operated, by a part of its name ("possess barrier"): keys 5-0, [ ]
            auto lower = [](std::string v) {
                for (char& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return v;
            };
            const std::vector<Controllable*> list = sa::controllables(*scene_);
            const std::string want = lower(fsStr(st, 0));
            ok = false;
            for (size_t k = 0; k < list.size() && !ok; ++k)
                if (list[k] && list[k]->possessable() && lower(list[k]->name()).find(want) != std::string::npos) {
                    possess(static_cast<int>(k));
                    note = list[k]->name();
                    ok = true;
                }
        } else if (st.op == "axes") {   // H, with the joint frames of one object ("axes soldier PATROL"; "axes off")
            PovSource src;
            settings_.showAxes = fsStr(st, 0) != "off";
            axesControllable_ = settings_.showAxes && fsSource(st, 0, src) ? src.controllable : -1;
        } else if (st.op == "povwait") {   // jump as soon as such an object exists (a round in flight), for at most S seconds
            fsWait = st;
            fsWaitUntil = t + fsNum(st, 3, 6.0f);
        } else if (st.op == "take") takeControl();
        else if (st.op == "jump") jumpEdge_ = true;
        else if (st.op == "nuc") fsNucT0 = t;
        else if (st.op == "aim") aiming_ = fsNum(st, 0, 1.0f) != 0.0f;
        else if (st.op == "crouch") crouchWant_ = static_cast<int>(fsNum(st, 0, 1.0f));
        else if (st.op == "next") cyclePovJump(+1);
        else if (st.op == "prev") cyclePovJump(-1);
        else if (st.op == "view") cyclePersonView();
        else if (st.op == "third") setViewThirdPerson(fsNum(st, 0, 1.0f) != 0.0f);
        else if (st.op == "release") fsFreeFromShown(), fsFrom = fsTo = fsShown, fsCamMode = 0, fsCamCut = true;
        else if (st.op == "picker") {
            const std::string a = fsStr(st, 0);
            if ((a == "open") != pickerOpen_ && (a == "open" || a == "close")) togglePicker();
            else if (a == "down") pickerMove(static_cast<int>(fsNum(st, 1, 1)));
            else if (a == "up") pickerMove(-static_cast<int>(fsNum(st, 1, 1)));
            else if (a == "enter") pickerConfirm();
            if (pickerOpen_ && pickerSel_ >= 0 && pickerSel_ < static_cast<int>(pickerList_.size())) note = pickerList_[static_cast<size_t>(pickerSel_)].name;
        } else if (st.op == "cursor") {
            const std::string a = fsStr(st, 0);
            if (a == "show" || a == "hide") fsCurShow = a == "show";
            else {
                fsCurShow = true;
                fsCurKey = 0;
                fsCurRow = -1;
                fsCurX0 = fsCurX, fsCurY0 = fsCurY;
                fsCurX1 = fsNum(st, 0, 0.5f), fsCurY1 = fsNum(st, 1, 0.5f);
                fsCurT0 = t, fsCurDur = fsNum(st, 2, 0.6f);
            }
        } else if (st.op == "cursorto") {
            PovSource src;
            ok = fsSource(st, 0, src);
            if (ok) {
                fsCurShow = true;
                fsCurKey = src.key;
                fsCurRow = -1;
                fsCurX0 = fsCurX, fsCurY0 = fsCurY;
                fsCurT0 = t, fsCurDur = fsNum(st, 2, 0.8f);
                note = src.name;
            }
        } else if (st.op == "cursorrow") {
            fsCurShow = true;
            fsCurKey = 0;
            fsCurRow = static_cast<int>(fsNum(st, 0, 0));
            fsCurX0 = fsCurX, fsCurY0 = fsCurY;
            fsCurT0 = t, fsCurDur = fsNum(st, 1, 0.6f);
        } else if (st.op == "click") {
            const std::string a = fsStr(st, 0);
            fsClickT = t;
            if (a == "middle") pickPovAt(fsCurX, fsCurY);
            else if (pickerOpen_ && pickerClick(fsCurX, fsCurY)) {
            } else {
                selectAt(fsCurX, fsCurY);
                if (a == "double") controlSelected();
            }
            fsCurKey = 0;
            fsCurRow = -1;
            fsCurX1 = fsCurX, fsCurY1 = fsCurY, fsCurDur = 0.0f;
            PovSource cur;
            if (currentSource(cur)) note = cur.name;
        } else if (st.op == "enter") controlSelected();
        else if (st.op == "hold") {
            static const char* axes[] = {"forward", "turn", "lift", "aimyaw", "aimpitch", "strafe"};
            const std::string a = fsStr(st, 0);
            ok = a == "boost";
            if (ok) fsBoostUntil = t + fsNum(st, 2, 1.0f);
            for (int k = 0; k < 6; ++k)
                if (a == axes[k]) {
                    fsHoldV[k] = fsNum(st, 1, 1.0f);
                    fsHoldUntil[k] = t + fsNum(st, 2, 1.0f);
                    ok = true;
                }
        } else if (st.op == "fire") {
            fsFireUntil = t + fsNum(st, 0, 0.2f);
            firePressedEdge_ = true;
        } else if (st.op == "action") actionEdge_ = true;
        else if (st.op == "use") {
            ok = updateInteraction();
            if (ok) performInteract();
        } else if (st.op == "look") {
            fsLookDx = fsNum(st, 0, 0), fsLookDy = fsNum(st, 1, 0);
            fsLookUntil = t + std::max(fsNum(st, 2, 1.0f), 0.05f);
        } else if (st.op == "menu") {
            const std::string a = fsStr(st, 0);
            if (a == "open") openMenu();
            else if (a == "close") closeMenu();
            else if (a == "tab") menuSelectTab(static_cast<int>(fsNum(st, 1, 0)));
        } else if (st.op == "explorer") {
            if (fsStr(st, 0) == "off") {
                if (fsExplorer) fsExplorer->close();
                fsExplorer.reset();
            } else {
                if (!fsExplorer) {   // freeze the frame on screen; later steps walk through its stages
                    fsExplorer = std::make_unique<FrameExplorer>();
                    CameraView cv;
                    cv.view = lookAt(fsShown.eye, fsShown.aim, {0, 1, 0});
                    cv.proj = perspective(radians(fsShown.fov), aspect, 0.05f, 600.0f);
                    cv.position = fsShown.eye;
                    fsExplorer->setConvergence(8, false);
                    if (!fsExplorer->open(*renderer_, *scene_, cv, settings_, w, h)) fsExplorer.reset(), ok = false;
                }
                if (fsExplorer) {
                    fsExStage = static_cast<int>(fsNum(st, 0, 0));
                    fsExLen = fsNum(st, 1, 6.0f);
                    fsExT0 = t;
                    fsExplorer->setStage(fsExStage);
                    fsExplorer->setTransition(0.0f);
                }
            }
        } else if (st.op == "raypick") {
            fsRayPick = true;
            fsRayX = fsNum(st, 0, 0.5f), fsRayY = fsNum(st, 1, 0.5f);
        } else if (st.op == "hour" || st.op == "clock" || st.op == "scrub") {
            if (DayCycle* clock = sa::dayCycle(*scene_)) {
                if (st.op == "hour") clock->setAuto(false), clock->setHour(fsNum(st, 0, 12.0f)), fsScrubDur = 0.0f;
                else if (st.op == "clock") clock->setAuto(fsStr(st, 0) == "auto"), fsScrubDur = 0.0f;
                else fsScrubH0 = fsNum(st, 0, 6.0f), fsScrubH1 = fsNum(st, 1, 18.0f), fsScrubT0 = t, fsScrubDur = fsNum(st, 2, 10.0f), clock->setAuto(false);
            }
        } else if (st.op == "dayops") setDayOps(fsNum(st, 0, 1.0f) != 0.0f);
        else if (st.op != "log") ok = false;
        if (!ok) std::printf("film script: step failed at %.2f s:%s\n", t, st.line.c_str());
        if (fsLog.is_open()) fsLog << t << (ok ? " ok " : " FAILED ") << st.line << (note.empty() ? "" : "  -> " + note) << '\n' << std::flush;
    };
    // TS_FILM_EVENTS = file: every game event with the video time it happened at (cutting the film by events), plus
    // the squad's roles once a second.
    std::ofstream evOut;
    if (const char* ef = std::getenv("TS_FILM_EVENTS")) evOut.open(ef);
    const char* evNames[] = {"WaveStart", "EnemySpotted", "LockOn", "RoundLaunched", "CombinedStrike", "Explosion", "EnemyHit",
                             "EnemyDestroyed", "Breach", "BaseLost", "CannonFire", "GatlingBurst", "DoorMoving", "Dusk",
                             "NightFalls", "Dawn", "DayBreak", "ShiftChange", "Withdraw", "RifleFire"};
    // TS_FILM_ALGO = file: what the algorithms hold, ten times a second, as JSON lines (the film's explanatory plates are
    // drawn from it): UAV poses and roles, the strike phases, every track of the shared tracker, every rover (pose, wheel
    // speeds, line sensors, hiding / pausing) and, twice a second, the cost grid of one rover (TS_FILM_ALGO_ROVER = slot,
    // default the first one alive): cells hidden from the UAVs, the flood-fill field and the guide line.
    std::ofstream algoOut;
    if (const char* af = std::getenv("TS_FILM_ALGO")) algoOut.open(af);
    const int algoRoverEnv = static_cast<int>(envFloat("TS_FILM_ALGO_ROVER", -1.0f));
    bool algoFreeDone = false;
    auto algoDump = [&](int f) {
        const float t = static_cast<float>(f) / static_cast<float>(fps);
        std::ostringstream o;
        o.setf(std::ios::fixed);
        o.precision(3);
        const Squad& sq = scene_->squad();
        const DayCycle* dc = sa::dayCycle(*scene_);
        o << "{\"t\":" << t << ",\"hour\":" << (dc ? dc->hour() : 0.0f) << ",\"strike\":\"" << strikePhaseName(sq.strike().phase)
          << "\",\"side\":\"" << strikePhaseName(sq.sideStrike().phase) << "\",\"target\":" << sq.targetTrack() << ",\"uav\":[";
        for (int i = 0; i < 2; ++i) {
            const DroneState& d = scene_->droneState(i);
            o << (i ? "," : "") << "{\"p\":[" << d.position.x << ',' << d.position.y << ',' << d.position.z << "],\"yaw\":" << d.yawDeg
              << ",\"gyaw\":" << d.gimbalYawDeg << ",\"gpitch\":" << d.gimbalPitchDeg << ",\"lock\":" << d.lockTime << ",\"role\":\""
              << squadRoleName(sq.roles[i]) << "\",\"duty\":\"" << scene_->uavDutyState(i) << "\"}";
        }
        o << "],\"tracks\":[";
        bool first = true;
        for (const Track& tr : sq.tracker().tracks()) {
            o << (first ? "" : ",") << "{\"id\":" << tr.id << ",\"s\":" << static_cast<int>(tr.state) << ",\"x\":" << tr.x[0] << ",\"z\":" << tr.x[1]
              << ",\"vx\":" << tr.x[2] << ",\"vz\":" << tr.x[3] << ",\"sig\":" << tr.positionSigma() << ",\"seen\":" << tr.seenNow
              << ",\"tri\":" << (tr.triangulated ? 1 : 0) << ",\"hits\":" << tr.hits << ",\"since\":" << tr.sinceUpdate << '}';
            first = false;
        }
        o << "],\"rovers\":[";
        first = true;
        int gridRover = -1, k = 0;
        for (const auto& in : scene_->intruders()) {
            const int slot = k++;
            if (in->life != Intruder::Life::Alive) continue;
            if (gridRover < 0 && (algoRoverEnv < 0 || algoRoverEnv == slot)) gridRover = slot;
            const RoverState& r = in->state;
            const Vec3 goal = in->hide.currentGoal();
            o << (first ? "" : ",") << "{\"i\":" << slot << ",\"x\":" << r.position.x << ",\"z\":" << r.position.z << ",\"yaw\":" << r.yaw
              << ",\"l\":" << r.leftSpeed << ",\"r\":" << r.rightSpeed << ",\"ir\":[";
            for (int q = 0; q < kIrSensorCount; ++q) o << (q ? "," : "") << (r.sensors[q] ? 1 : 0);
            o << "],\"hid\":" << (r.hidden ? 1 : 0) << ",\"ev\":" << (in->hide.evading() ? 1 : 0) << ",\"pause\":" << (in->hide.pausing() ? 1 : 0)
              << ",\"hes\":" << (in->hide.hesitating() ? 1 : 0) << ",\"ret\":" << (in->hide.retreating() ? 1 : 0) << ",\"hp\":" << in->health
              << ",\"obj\":[" << in->objective.x << ',' << in->objective.z << "],\"goal\":[" << goal.x << ',' << goal.z << "]}";
            first = false;
        }
        o << "],\"soldiers\":[";
        first = true;
        for (const PovSource& ps : listPovSources(*scene_, Vec3{}))
            if (ps.kind == PovKind::Soldier) {
                o << (first ? "" : ",") << "[" << ps.position.x << ',' << ps.position.z << "]";
                first = false;
            }
        o << "]";
        if (f % (fps / 2) == 0 && gridRover >= 0) {
            const HideController& hc = scene_->intruders()[static_cast<size_t>(gridRover)]->hide;
            o << ",\"grid\":{\"i\":" << gridRover << ",\"line\":[";
            first = true;
            for (const Vec3& q : hc.guideLine()) {
                o << (first ? "" : ",") << '[' << q.x << ',' << q.z << ']';
                first = false;
            }
            o << "],\"flood\":[";
            for (int cz = 0; cz < kGridCells; ++cz)
                for (int cx = 0; cx < kGridCells; ++cx) o << (cz + cx ? "," : "") << hc.floodValue(cx, cz);
            o << "],\"hidden\":\"";
            for (int cz = 0; cz < kGridCells; ++cz)
                for (int cx = 0; cx < kGridCells; ++cx) o << (hc.cellHidden(cx, cz) ? '1' : '0');
            o << "\"";
            if (!algoFreeDone) {
                o << ",\"free\":\"";
                for (int cz = 0; cz < kGridCells; ++cz)
                    for (int cx = 0; cx < kGridCells; ++cx) o << (hc.cellFree(cx, cz) ? '1' : '0');
                o << "\"";
                algoFreeDone = true;
            }
            o << "}";
        }
        o << "}\n";
        algoOut << o.str();
    };
    const double start = nowSeconds();
    for (int f = 0; f < frames; ++f) {
        const float tDir = dirStart + dirSpeed * static_cast<float>(f) / static_cast<float>(fps);
        DirectorShot shot;
        if (cut == DirectorCut::V7) {
            shot = directorShot(*scene_, tDir, cut);
            if (shot.script != segment) {
                std::printf("v7 cut %6.1f s: %s\n", tDir, shot.name);
                enter(shot, segment);
                segment = shot.script;
            }
            if (DayCycle* clock = sa::dayCycle(*scene_)) {  // the director's clock (manual time)
                clock->setAuto(false);
                clock->setHour(directorScript(tDir, cut).hour);
            }
            // per-frame actions (inputs held over this frame's simulation steps)
            if (shot.act == V7Act::Picker) {
                pickerT += dirSpeed / static_cast<float>(fps);
                if (pickerT > 0.55f) {
                    pickerT = 0.0f;
                    pickerMove(1);
                }
            } else if ((shot.act == V7Act::PovUav && shot.local > shot.length - 1.5f) || shot.act == V7Act::PovMissile) {
                // a round launched from 2 s into the UAV segment on is ridden at once (launches cannot be scripted:
                // the squad fires only at what its cameras see); the ride carries on into the round segment
                PovSource cur;
                const bool riding = currentSource(cur) && povProjectile(cur.kind);   // on a round or holding its impact
                if (!riding)
                    if (const uint32_t round = findPov(PovKind::Missile)) jumpToPov(round, static_cast<int>(PovView::First), true);
            } else if (shot.act == V7Act::SoldierFire) {
                if (shot.local > 0.8f && camera_.mode == CameraMode::Pov) takeControl();
                held_.fire = shot.local > 1.2f && std::fmod(shot.local - 1.2f, 1.4f) < 0.6f;   // bursts
                held_.turn = shot.local > 1.2f ? 0.12f * std::sin(shot.local * 0.9f) : 0.0f;   // a slow scan
            }
            props.ensure();
        }
        if (fsOn) {
            const float t = static_cast<float>(f) / static_cast<float>(fps);
            while (fsK < fsteps.size() && fsteps[fsK].t <= t + 1e-4f) fsRun(fsteps[fsK++], t);
            if (fsNucT0 >= 0.0f) {   // the shutter of Interactive.cpp: shut in 0.06 s, held, open over the last 0.1 s of 0.4 s
                const float a = t - fsNucT0;
                settings_.nucShutter = clampf(std::min(a / 0.06f, (0.4f - a) / 0.1f), 0.0f, 1.0f);
                if (a >= 0.4f) fsNucT0 = -1.0f, settings_.nucShutter = 0.0f;
            }
            if (t < fsWaitUntil) {
                PovSource src;
                const std::string v = fsStr(fsWait, 2);
                if (fsSource(fsWait, 0, src) && jumpToPov(src.key, v == "chase" ? 1 : v == "alt" ? 2 : 0, true)) {
                    fsWaitUntil = -1.0f;
                    if (fsLog.is_open()) fsLog << t << " ok povwait -> " << src.name << '\n' << std::flush;
                }
            }
            if (!tourOn) {   // held keys of the script (a tour holds its own)
                held_ = ControlInput{};
                float* axis[6] = {&held_.forward, &held_.turn, &held_.lift, &held_.aimYaw, &held_.aimPitch, &held_.strafe};
                for (int k = 0; k < 6; ++k)
                    if (t < fsHoldUntil[k]) *axis[k] = fsHoldV[k];
                held_.boost = t < fsBoostUntil;
                held_.fire = t < fsFireUntil;
                if (t < fsLookUntil) {
                    aimYawAcc_ += fsLookDx / static_cast<float>(fps);
                    aimPitchAcc_ += fsLookDy / static_cast<float>(fps);
                }
            }
            if (fsScrubDur > 0.0f)
                if (DayCycle* clock = sa::dayCycle(*scene_)) {
                    const float u = clampf((t - fsScrubT0) / fsScrubDur, 0.0f, 1.0f);
                    clock->setHour(fsScrubH0 + (fsScrubH1 - fsScrubH0) * u);
                    if (u >= 1.0f) fsScrubDur = 0.0f;
                }
            // the pointer: towards a fixed point, a row of the POV list, or an object it follows until the click
            if (fsCurKey != 0) {
                for (const PovSource& p : listPovSources(*scene_, shownPose_.eye))
                    if (p.key == fsCurKey) fsProject(p.centre, fsCurX1, fsCurY1);
            } else if (fsCurRow >= 0 && pickerOpen_) {
                const PickerLayout l = pickerLayout(w, h, static_cast<int>(pickerList_.size()), pickerSel_);
                fsCurX1 = (l.x + 0.45f * l.w) / static_cast<float>(w);
                fsCurY1 = (l.y + l.headerH + (static_cast<float>(fsCurRow - l.firstRow) + 0.5f) * l.rowH) / static_cast<float>(h);
            }
            const float cu = fsCurDur > 0.0f ? clampf((t - fsCurT0) / fsCurDur, 0.0f, 1.0f) : 1.0f;
            const float ce = cu * cu * (3.0f - 2.0f * cu);
            fsCurX = fsCurX0 + (fsCurX1 - fsCurX0) * ce;
            fsCurY = fsCurY0 + (fsCurY1 - fsCurY0) * ce;
        }
        if (tourOn) {
            const std::vector<Controllable*> cl = sa::controllables(*scene_);
            const int pi = possessedSoldierIndex();
            const Soldier* me = pi >= 0 && pi < static_cast<int>(cl.size()) ? dynamic_cast<const Soldier*>(cl[pi]) : nullptr;
            held_ = ControlInput{};
            const float frameDt = 1.0f / static_cast<float>(fps);
            Controllable* veh = !me && possessed_ >= 0 && possessed_ < static_cast<int>(cl.size()) ? cl[static_cast<size_t>(possessed_)] : nullptr;
            // steps that are not waypoints: done by the game's own functions, like key presses
            while (tourK < tour.size() && tour[tourK].op != TourOp::Walk) {
                TourPt& op = tour[tourK];
                bool done = true;
                switch (op.op) {
                    case TourOp::Use:
                        if (updateInteraction()) performInteract();
                        else {
                            std::printf("tour: nothing to use at step %zu (frame %d); in range:", tourK, f);
                            InteractView iv;
                            for (const Interactable& c : gatherInteractables(iv))
                                if (length(c.point - iv.player) < 9.0f)
                                    std::printf(" [%s %s %.1f m reach %.1f]", interactActionName(c.action), c.name.c_str(), length(c.point - iv.player), c.reach);
                            std::printf("\n");
                        }
                        break;
                    case TourOp::Exit: exitVehicle(); break;
                    case TourOp::View: setViewThirdPerson(op.run); break;
                    case TourOp::Sight: camera_.possessSight = true; possessAlt_ = false; break;
                    case TourOp::Chase: camera_.possessSight = false; possessAlt_ = false; break;
                    case TourOp::Action: actionEdge_ = true; break;
                    case TourOp::AimOff: aimOn = false; break;
                    case TourOp::Hud: showHud_ = op.x > 0.5f; break;
                    case TourOp::Cam:
                    case TourOp::CamFix:
                        followOn = true;
                        followValid = false;
                        followFixed = op.op == TourOp::CamFix;
                        rig_.cancel();   // no blend from the boarding jump: the free camera does not advance its clock
                        followArm = {op.x, op.y, op.z};
                        break;
                    case TourOp::CamOff:
                        followOn = false;
                        if (possessed_ >= 0) camera_.mode = CameraMode::Possess;
                        break;
                    case TourOp::Turn: {
                        const Tank* tank = dynamic_cast<const Tank*>(veh);
                        if (!tank) break;
                        const float err = std::fmod(op.x - tank->yawDeg() + 540.0f, 360.0f) - 180.0f;
                        held_.turn = clampf(-err / 18.0f, -1.0f, 1.0f);
                        op.t += frameDt;
                        done = std::fabs(err) < 2.0f || op.t > 20.0f;
                        break;
                    }
                    case TourOp::Aim:
                        aimOn = true;
                        aimAt = {op.x, op.y, op.z};
                        break;
                    case TourOp::Wait:
                        op.t += frameDt;
                        done = op.t >= op.x;
                        break;
                    case TourOp::Fire:
                        if (op.t == 0.0f) firePressedEdge_ = true;
                        held_.fire = true;
                        op.t += frameDt;
                        done = op.t >= op.x;
                        break;
                    case TourOp::Face: {
                        // the camera arm (and with it the soldier, who is asked to step that way) turns to the point
                        if (!me) break;
                        const Vec3 p = me->position();
                        const float want = degrees(std::atan2(op.x - p.x, op.z - p.z));
                        const float diff = std::fmod(want - tp_.yawDeg + 540.0f, 360.0f) - 180.0f;
                        const float body = std::fmod(want - me->yawDeg() + 540.0f, 360.0f) - 180.0f;
                        tpLookYaw_ -= clampf(diff * 3.0f * frameDt, -3.0f, 3.0f);
                        if (std::fabs(body) > 6.0f) {   // a small step towards it turns the body
                            const float cy = radians(tp_.yawDeg), dx = op.x - p.x, dz = op.z - p.z, d = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f);
                            held_.forward = 0.25f * (dx * std::sin(cy) + dz * std::cos(cy)) / d;
                            held_.turn = 0.25f * (-dx * std::cos(cy) + dz * std::sin(cy)) / d;
                        }
                        op.t += frameDt;
                        done = (std::fabs(diff) < 4.0f && std::fabs(body) < 8.0f) || op.t > 4.0f;
                        break;
                    }
                    case TourOp::Drive: {
                        const Tank* tank = dynamic_cast<const Tank*>(veh);
                        if (!tank) break;
                        const Vec3 p = tank->position();
                        const float dx = op.x - p.x, dz = op.z - p.z, d = std::sqrt(dx * dx + dz * dz);
                        const float err = std::fmod(degrees(std::atan2(dx, dz)) - tank->yawDeg() + 540.0f, 360.0f) - 180.0f;
                        held_.turn = clampf(-err / 25.0f, -1.0f, 1.0f);   // Tank: positive turn = right = heading falls
                        held_.forward = std::fabs(err) > 50.0f ? 0.0f : clampf(d / 4.0f, 0.25f, 1.0f) * (1.0f - std::fabs(err) / 70.0f);
                        if (std::getenv("TS_TOUR_DEBUG") && f % 15 == 0)
                            std::printf("tour: drive f %d pos (%.1f, %.1f) yaw %.0f err %.0f d %.1f fwd %.2f turn %.2f speed %.1f\n", f, p.x, p.z,
                                        tank->yawDeg(), err, d, held_.forward, held_.turn, tank->speed());
                        if (std::getenv("TS_TOUR_DEBUG") && f % 60 == 0 && std::fabs(tank->speed()) < 0.05f && op.t > 2.0f) {
                            const float yr = radians(tank->yawDeg());
                            const Vec3 ahead = p + Vec3{std::sin(yr), 0.0f, std::cos(yr)} * 4.2f;
                            for (const SceneNode* n : scene_->leaves()) {
                                if (!n->obstacle || !n->visible) continue;
                                const Vec3 c = transformPoint(n->world, {0, 0, 0});
                                if (std::fabs(c.x - ahead.x) < 2.5f && std::fabs(c.z - ahead.z) < 2.5f)
                                    std::printf("tour:   near the bow: %s / %s at (%.1f, %.1f, %.1f)\n", n->object.c_str(), n->name.c_str(), c.x, c.y, c.z);
                            }
                        }
                        op.t += frameDt;
                        done = d < 1.6f || op.t > 40.0f;
                        if (op.t > 40.0f) std::printf("tour: drive step %zu timed out at (%.1f, %.1f)\n", tourK, p.x, p.z);
                        break;
                    }
                    case TourOp::Walk: break;
                }
                if (!done) break;
                ++tourK;
                tourLastD = 1e9f;
                tourStuck = 0.0f;
            }
            if (aimOn && veh) {   // lay the sight on the point: the arrow keys, proportional to the error seen in the sight
                const CameraHint sight = veh->camera(true);
                const Vec3 fwd = normalize(sight.target - sight.eye), want = normalize(aimAt - sight.eye);
                const Vec3 right = normalize(cross(fwd, Vec3{0.0f, 1.0f, 0.0f}));
                const float ex = degrees(std::atan2(dot(want, right), dot(want, fwd)));
                const float ey = degrees(std::asin(clampf(want.y, -1.0f, 1.0f)) - std::asin(clampf(fwd.y, -1.0f, 1.0f)));
                held_.aimYaw = clampf(ex / 6.0f, -1.0f, 1.0f);
                held_.aimPitch = clampf(ey / 5.0f, -1.0f, 1.0f);
            }
            if (me && tourK < tour.size() && tour[tourK].op == TourOp::Walk) {
                const Vec3 p = me->position();
                float dx = tour[tourK].x - p.x, dz = tour[tourK].z - p.z, d = std::sqrt(dx * dx + dz * dz);
                const bool walkOn = tourK + 1 < tour.size() && tour[tourK + 1].op == TourOp::Walk;   // passes through the point
                if (d < (walkOn ? 0.9f : 0.35f)) {
                    ++tourK;
                    tourLastD = 1e9f;
                    tourStuck = 0.0f;
                }
                if (tourK < tour.size() && tour[tourK].op == TourOp::Walk) {
                    dx = tour[tourK].x - p.x;
                    dz = tour[tourK].z - p.z;
                    d = std::max(std::sqrt(dx * dx + dz * dz), 1e-3f);
                    const float cy = radians(tp_.yawDeg);
                    const float fw = (dx * std::sin(cy) + dz * std::cos(cy)) / d, rt = (-dx * std::cos(cy) + dz * std::sin(cy)) / d;
                    held_.forward = fw;
                    held_.turn = rt;
                    held_.boost = tour[tourK].run;
                    // the arm eases round behind the walk (look(): yaw -= dYaw)
                    const float want = degrees(std::atan2(dx, dz));
                    float diff = std::fmod(want - tp_.yawDeg + 540.0f, 360.0f) - 180.0f;
                    tpLookYaw_ -= clampf(diff * 1.4f / static_cast<float>(fps), -2.2f, 2.2f);
                    tourStuck = d < tourLastD - 0.02f ? 0.0f : tourStuck + 1.0f / static_cast<float>(fps);
                    tourLastD = std::min(tourLastD, d);
                    if (tourStuck > 3.0f) {
                        std::printf("tour: stuck before waypoint %zu at (%.1f, %.1f) frame %d, skipping it\n", tourK, p.x, p.z, f);
                        ++tourK;
                        tourStuck = 0.0f;
                        tourLastD = 1e9f;
                    }
                }
            }
        }
        for (int k = 0; k < stepsPerFrame; ++k) {
            step(kSimStep);
            if (evOut.is_open()) {
                const float vt = static_cast<float>(f) / static_cast<float>(fps) + static_cast<float>(k) * kSimStep;
                for (const GameEvent& e : scene_->events()) {
                    const int ti = static_cast<int>(e.type);
                    if (e.type == GameEventType::RifleFire && k != 0) continue;   // one line per frame is enough
                    evOut << vt << ' ' << (ti >= 0 && ti < 20 ? evNames[ti] : "?") << " uav " << e.uav << " track " << e.track << " value "
                          << e.value << " combined " << e.combined << " at " << e.position.x << ' ' << e.position.z << '\n';
                }
                if (k == 0 && f % fps == 0) {
                    const Squad& sq = scene_->squad();
                    if (const DayCycle* dc = sa::dayCycle(*scene_))
                        evOut << vt << " STATE hour " << dc->hour() << " uav0 " << squadRoleName(sq.roles[0]) << " uav1 "
                              << squadRoleName(sq.roles[1]) << " strike " << strikePhaseName(sq.strike().phase) << " side "
                              << strikePhaseName(sq.sideStrike().phase) << " tracks " << sq.tracker().tracks().size() << '\n';
                }
            }
            if ((cut == DirectorCut::V7 || tourOn || filmUav >= 0 || fsOn) && camera_.mode != CameraMode::Cinematic && camera_.mode != CameraMode::Free) updateGameCamera(kSimStep);
            if (audioOn) audioEngine.pollEvents(*scene_);  // scene.events() only holds the last step's events
        }
        if (algoOut.is_open() && f % (fps / 10) == 0) algoDump(f);
        if (tourOn) {
            settings_.letterbox = 0.0f;
            settings_.fade = 1.0f;
            const std::vector<Controllable*> cl = sa::controllables(*scene_);
            if (followOn && possessed_ >= 0 && possessed_ < static_cast<int>(cl.size()) && !dynamic_cast<const Soldier*>(cl[static_cast<size_t>(possessed_)])) {
                // low follow camera: behind (and beside) the hull heading, eased so the hull's shake does not reach it
                const Controllable* v = cl[static_cast<size_t>(possessed_)];
                const CameraHint ch = v->camera(false);
                float hd = std::atan2(ch.target.x - ch.eye.x, ch.target.z - ch.eye.z);
                if (const Tank* tank = dynamic_cast<const Tank*>(v)) hd = radians(tank->yawDeg());
                const Vec3 d{std::sin(hd), 0.0f, std::cos(hd)}, side{std::cos(hd), 0.0f, -std::sin(hd)};
                const Vec3 c = v->position();
                const Vec3 eye = followFixed ? followArm : c - d * followArm.x + side * followArm.z + Vec3{0.0f, followArm.y, 0.0f};
                const Vec3 aim = followFixed ? c + Vec3{0.0f, 1.6f, 0.0f} : c + d * 2.5f + Vec3{0.0f, 1.5f, 0.0f};
                const float k = followValid ? 0.08f : 1.0f;
                followEye = followEye + (eye - followEye) * k;
                followAim = followAim + (aim - followAim) * (followValid ? 0.2f : 1.0f);
                followValid = true;
                setFree(followEye, followAim, 55.0f);
            }
        } else if (fsOn) {
            const float t = static_cast<float>(f) / static_cast<float>(fps);
            if (camera_.mode == CameraMode::Free) {   // the script's camera; in a POV or in control the game's own rig films
                FilmPose want = fsTo;
                if (fsCamMode == 0) {
                    const float u = fsCamDur > 0.0f ? clampf((t - fsCamT0) / fsCamDur, 0.0f, 1.0f) : 1.0f;
                    const float e = u * u * (3.0f - 2.0f * u);
                    want = FilmPose{lerp(fsFrom.eye, fsTo.eye, e), lerp(fsFrom.aim, fsTo.aim, e), fsFrom.fov + (fsTo.fov - fsFrom.fov) * e};
                    fsShown = want;
                } else {
                    if (fsCamMode == 2)
                        for (const PovSource& p : listPovSources(*scene_, shownPose_.eye))
                            if (p.key == fsOrbKey) fsOrbC = p.centre;
                    const float a = radians(fsOrbDeg0 + fsOrbRate * (t - fsOrbT0));
                    want.aim = fsOrbC;
                    want.eye = fsOrbC + Vec3{std::cos(a) * fsOrbR, fsOrbH, std::sin(a) * fsOrbR};
                    const float k = fsCamCut ? 1.0f : 0.06f;
                    fsShown.eye = fsShown.eye + (want.eye - fsShown.eye) * k;
                    fsShown.aim = fsShown.aim + (want.aim - fsShown.aim) * (fsCamCut ? 1.0f : 0.12f);
                    fsShown.fov = fsShown.fov + (want.fov - fsShown.fov) * k;
                    fsCamCut = false;
                }
                setFree(fsShown.eye, fsShown.aim, fsShown.fov);
            }
            settings_.letterbox = 0.0f;
            settings_.fade = 1.0f;
        } else if (filmCam && filmUav >= 0) {
            // ride the UAV's thermal gimbal (the game's own first-person UAV view and HUD)
            if (camera_.mode != CameraMode::Pov) {
                gameMode_ = true;
                showHud_ = true;
                for (const PovSource& p : listPovSources(*scene_, shownPose_.eye))
                    if (p.kind == PovKind::Uav && p.uav == filmUav) jumpToPov(p.key, static_cast<int>(PovView::First), false);
            }
            settings_.letterbox = 0.0f;
            settings_.fade = 1.0f;
        } else if (filmCam && filmView == "flir") {
            // the thermal first-person view through UAV-1's gimbal camera with its FLIR HUD (track boxes, PERS brackets)
            camera_.mode = CameraMode::Free;
            settings_.view = ViewMode::Thermal;
            thermalFpv_ = true;
            showHud_ = true;
            settings_.letterbox = 0.0f;
            settings_.fade = 1.0f;
        } else if (filmCam && filmView == "follow") {
            const float tNow = static_cast<float>(f) / static_cast<float>(fps);
            const Squad& sq = scene_->squad();
            const Track* tr = sq.tracker().find(sq.targetTrack());
            const float u = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
            Vec3 eye = lerp(Vec3{film[1], film[2], film[3]}, Vec3{film[7], film[8], film[9]}, u);
            Vec3 aim = lerp(Vec3{film[4], film[5], film[6]}, Vec3{film[10], film[11], film[12]}, u);
            if (tr) {
                followHold = tNow;
                followAim2 = tr->position() + Vec3{0.0f, 0.6f, 0.0f};
            }
            if (tNow - followHold < 4.0f) {   // on the target, and 4 s after it is gone (the wreck burning)
                const float a = 0.12f * tNow + 0.8f;
                aim = followAim2;
                eye = followAim2 + Vec3{std::cos(a) * 10.0f, 4.2f, std::sin(a) * 10.0f};
            }
            const float k = followLive ? 0.045f : 1.0f;
            followEye2 = followEye2 + (eye - followEye2) * k;
            followAim = followAim + (aim - followAim) * (followLive ? 0.08f : 1.0f);
            followLive = true;
            setFree(followEye2, followAim, film[13]);
            settings_.letterbox = 0.0f;
            settings_.fade = 1.0f;
        } else if (filmCam) {
            const float u = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
            const float e = u * u * (3.0f - 2.0f * u);   // ease in and out
            const Vec3 eye = lerp(Vec3{film[1], film[2], film[3]}, Vec3{film[7], film[8], film[9]}, e);
            const Vec3 aim = lerp(Vec3{film[4], film[5], film[6]}, Vec3{film[10], film[11], film[12]}, e);
            setFree(eye, aim, film[13]);
            settings_.letterbox = 0.0f;
            settings_.fade = 1.0f;
        } else         if (cut != DirectorCut::V7 || shot.act == V7Act::Camera) {
            if (cut == DirectorCut::V7) camera_.mode = CameraMode::Cinematic;
            camera_.directorTime = tDir;
            applyCinematic();
            if (filmClean) settings_.letterbox = 0.0f;
        } else if (shot.act == V7Act::Picker || shot.act == V7Act::RtMode || shot.act == V7Act::RtTree || shot.act == V7Act::Explorer) {
            setFree(shot.eye, shot.target, shot.fovDeg);
            settings_.letterbox = 0.0f;
            settings_.fade = 1.0f;
        }
        if (audioOn) {
            Vec3 pos, fwd, up;
            if (camera_.mode == CameraMode::Pov || camera_.mode == CameraMode::Possess) {
                pos = shownPose_.eye;
                fwd = shownPose_.forward();
                up = shownPose_.up();
            } else {
                const CameraView cv = camera_.compute(*scene_, aspect);
                pos = cv.position;
                fwd = Vec3{-cv.view.at(2, 0), -cv.view.at(2, 1), -cv.view.at(2, 2)};
                up = Vec3{cv.view.at(1, 0), cv.view.at(1, 1), cv.view.at(1, 2)};
            }
            audioEngine.update(*scene_, pos, fwd, up, 1.0f / static_cast<float>(fps));
            audioEngine.renderOffline(1.0f / static_cast<float>(fps), audioBlock);
            wav.write(audioBlock.data(), audioBlock.size());
        }
        target.bind();
        if (fsOn && fsExplorer) {
            const float local = static_cast<float>(f) / static_cast<float>(fps) - fsExT0;
            GLint fbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            fsExplorer->setTransition(clampf(local / 0.6f, 0.0f, 1.0f));
            fsExplorer->setAnimation(fsExStage == 3 ? local / std::max(fsExLen, 0.1f) * fsExplorer->rasterProbeTime() * 1.25f : local * 1.6f);
            fsExplorer->draw(*renderer_, static_cast<GLuint>(fbo), w, h);
        } else
        if (cut == DirectorCut::V7 && shot.act == V7Act::Explorer && explorer) {
            GLint fbo = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
            explorer->setTransition(clampf(shot.local / 0.6f, 0.0f, 1.0f));
            // raster stage: the scan sweeps the frame over the segment; the other stages run their animations
            explorer->setAnimation(shot.param == 3 ? shot.local / std::max(shot.length, 0.1f) * explorer->rasterProbeTime() * 1.25f
                                                   : shot.local * 1.6f);
            explorer->draw(*renderer_, static_cast<GLuint>(fbo), w, h);
        } else {
            renderFrame(w, h, true);
            if (cut == DirectorCut::V7 && shot.act == V7Act::RtTree && !treePicked && shot.local > 0.25f) {
                // pick the glass ball's pixel from the frame just drawn (still the showcase camera), Whitted tree
                Vec3 e, t;
                float fov = 50.0f;
                directorRtCamera(e, t, fov, false);
                const Mat4 vp = perspective(radians(fov), aspect, 0.05f, 600.0f) * lookAt(e, t, {0, 1, 0});
                const Vec3 ball{kV7PropsX - 0.15f, terrainHeight(kV7PropsX, kV7PropsZ) + 0.82f, kV7PropsZ};
                const Vec4 c = vp * Vec4{ball.x, ball.y, ball.z, 1.0f};
                const float u = 0.5f * (c.x / c.w + 1.0f), v = 0.5f * (1.0f - c.y / c.w);
                renderer_->setRayDebug(true);
                treePicked = renderer_->rayDebugPick(*scene_, u, v);
                settings_.rtOverlay = true;
            }
        }
        if (cut == DirectorCut::V7) {  // segment title (top left), fading in and out over the first 3.5 s
            const float a = clampf(std::min(shot.local / 0.4f, (3.5f - shot.local) / 0.6f), 0.0f, 1.0f);
            Hud& hud = renderer_->hud();
            const float sc = static_cast<float>(h) / 1080.0f;
            hud.begin(w, h);
            if (a > 0.0f && shot.act != V7Act::Explorer && !filmClean) {  // the Explorer has its own stage header
                const float x = 56.0f * sc, y = 150.0f * sc;
                hud.rect(x - 18.0f * sc, y - 14.0f * sc, 6.0f * sc, 78.0f * sc, Rgba{1.0f, 0.62f, 0.15f, 0.9f * a});
                hud.text(x, y, shot.name, 34.0f * sc, Rgba{1.0f, 1.0f, 1.0f, a});
                hud.text(x, y + 44.0f * sc, shot.subtitle, 20.0f * sc, Rgba{0.82f, 0.86f, 0.9f, 0.9f * a});
            }
            hud.end();
        }
        if (fsOn && fsRayPick) {   // the ray debugger's click, on the frame just drawn
            fsRayPick = false;
            renderer_->setRayDebug(true);
            settings_.rtOverlay = true;
            if (!renderer_->rayDebugPick(*scene_, fsRayX, fsRayY)) std::printf("film script: the ray debugger found nothing at %.2f %.2f\n", fsRayX, fsRayY);
        }
        if (fsOn && fsCurShow) {   // the pointer and the ring of a click
            const float t = static_cast<float>(f) / static_cast<float>(fps);
            Hud& hud = renderer_->hud();
            const float sc = static_cast<float>(h) / 1080.0f * 1.5f, x = fsCurX * static_cast<float>(w), y = fsCurY * static_cast<float>(h);
            hud.begin(w, h);
            const float ring = (t - fsClickT) / 0.45f;
            if (ring >= 0.0f && ring < 1.0f) {
                const float r = (8.0f + 30.0f * ring) * sc;
                for (int k = 0; k < 28; ++k) {
                    const float a0 = 6.2831853f * static_cast<float>(k) / 28.0f, a1 = 6.2831853f * static_cast<float>(k + 1) / 28.0f;
                    for (float d = -1.0f; d <= 1.0f; d += 1.0f)
                        hud.line(x + std::cos(a0) * (r + d), y + std::sin(a0) * (r + d), x + std::cos(a1) * (r + d), y + std::sin(a1) * (r + d),
                                 Rgba{1.0f, 0.78f, 0.2f, 1.0f - ring});
                }
            }
            const Rgba dark{0.05f, 0.06f, 0.08f, 0.95f}, white{1.0f, 1.0f, 1.0f, 1.0f};
            for (int pass = 0; pass < 2; ++pass) {
                const float g = pass == 0 ? 2.2f * sc : 0.0f;   // dark rim first
                const Rgba c = pass == 0 ? dark : white;
                hud.tri(x - g * 0.6f, y - g * 1.6f, x - g * 0.6f, y + 21.0f * sc + g * 1.4f, x + 15.0f * sc + g * 1.6f, y + 15.0f * sc + g * 0.6f, c);
                hud.tri(x + 4.5f * sc - g, y + 14.0f * sc, x + 9.5f * sc - g * 0.4f, y + 26.0f * sc + g, x + 13.5f * sc + g, y + 24.0f * sc + g * 0.6f, c);
                hud.tri(x + 4.5f * sc - g, y + 14.0f * sc, x + 13.5f * sc + g, y + 24.0f * sc + g * 0.6f, x + 9.5f * sc + g, y + 13.0f * sc, c);
            }
            hud.end();
        }
        target.read(rgb);
        if (pipe) {
            if (std::fwrite(rgb.data(), 1, rgb.size(), pipe) != rgb.size()) {
                std::fprintf(stderr, "ffmpeg pipe closed early\n");
                break;
            }
        } else {
            char name[64];
            std::snprintf(name, sizeof(name), "/frame_%05d.png", f);
            writePng(pngDir + name, w, h, rgb);
        }
        if (f % fps == 0) {
            const double el = nowSeconds() - start;
            std::printf("frame %d / %d  (%.1f s elapsed, %.1f frames/s)\n", f, frames, el, f / std::max(el, 1e-3));
        }
        std::fflush(stdout);
    }
    int status = pipe ? closePipe(pipe) : 0;
    bool wavOk = false;
    if (audioOn) wavOk = wav.close();
    if (pipe && audioOn) {
        if (status == 0 && wavOk) {
            const std::string mux = "\"" + opt_.ffmpeg + "\" -y -loglevel error -i \"" + silentPath + "\" -i \"" + wavPath +
                                    "\" -c:v copy -c:a aac -b:a 192k -shortest \"" + out + "\"";
            const int ms = runCommand(mux);
            if (ms == 0) {
                std::remove(silentPath.c_str());
                std::remove(wavPath.c_str());
                std::printf("Audio track muxed (%.1f s of 48 kHz stereo)\n", frames / static_cast<float>(fps));
            } else {
                std::fprintf(stderr, "audio mux failed (status %d); keeping %s and %s\n", ms, silentPath.c_str(), wavPath.c_str());
                std::filesystem::copy_file(silentPath, out, std::filesystem::copy_options::overwrite_existing);
                status = ms;
            }
        } else {
            std::fprintf(stderr, "video encode or WAV write failed; leaving %s\n", silentPath.c_str());
        }
    } else if (audioOn) {
        std::printf("Audio written to %s (no ffmpeg: frames only)\n", wavPath.c_str());
    }
    std::printf("Recorded %d frames to %s (encoder status %d)\n", frames, pipe ? out.c_str() : pngDir.c_str(), status);
    return status == 0 ? 0 : 1;
}

// ------------------------------------------------------------------------- v9 M9: the replay video (--replay --record)
// One video frame per simulation tick inside the clip (exactly 60 fps of simulated time), drawn by a second renderer
// (its own TAA / exposure history and buffers; the replayed live frames keep theirs) at --ss times the output size and
// box-filtered down (CINEMATIC: EPIC + 2x2 supersampling), with the HUD of the session, and the procedural sound.

struct App::ReplayVideo {
    std::FILE* pipe = nullptr;
    std::string out, videoTarget, wavPath, silentPath, pngDir;
    int w = 0, h = 0, ss = 1;
    RenderTarget hi, lo;
    std::vector<uint8_t> rgb;
    audio::AudioEngine audio;
    audio::WavWriter wav;
    bool audioOn = false;
    std::vector<float> block;
    long long clipIn = 0, clipOut = 0, written = 0;
    double start = 0.0;
    bool failed = false;
};

bool App::replayVideoOpen() {
    auto v = std::make_unique<ReplayVideo>();
    v->out = opt_.outPath;
    v->w = std::max(16, videoW_);
    v->h = std::max(16, videoH_);
    v->ss = std::max(1, std::min(2, opt_.supersample));   // 2x2: one bilinear tap per output pixel = an exact box filter
    v->clipIn = std::max(0LL, replayHeader_.clipIn);
    v->clipOut = replayHeader_.clipOut >= 0 ? replayHeader_.clipOut : (1LL << 60);
    if (!safeForShell(v->out) || !safeForShell(opt_.ffmpeg) || !safeForShell(opt_.encoder)) {
        std::fprintf(stderr, "--record: output path, --ffmpeg and --encoder must not contain \" & | ^ < > %% ! $ ` ;\n");
        return false;
    }
    videoRenderer_ = std::make_unique<Renderer>();
    if (!videoRenderer_->init()) {
        std::fprintf(stderr, "replay video: renderer initialisation failed\n");
        return false;
    }
    if (!v->hi.create(v->w * v->ss, v->h * v->ss, 0, false) || !v->lo.create(v->w, v->h, 0, false)) return false;
    const std::string stem = v->out.size() > 4 && v->out.compare(v->out.size() - 4, 4, ".mp4") == 0 ? v->out.substr(0, v->out.size() - 4) : v->out;
    v->wavPath = stem + "_audio.wav";
    v->silentPath = stem + "_noaudio.mp4";
    v->audioOn = std::getenv("TS_NO_AUDIO") == nullptr && v->wav.open(v->wavPath);
    v->videoTarget = v->audioOn ? v->silentPath : v->out;
    const bool nvenc = opt_.encoder.find("nvenc") != std::string::npos;
    const std::string codec = nvenc ? "-c:v " + opt_.encoder + " -preset p7 -tune hq -rc vbr -cq 18 -b:v 0 "
                                    : "-c:v " + opt_.encoder + " -preset slow -crf 17 ";
    const std::string cmd = "\"" + opt_.ffmpeg + "\" -y -loglevel error -f rawvideo -pix_fmt rgb24 -s " + std::to_string(v->w) + "x" +
                            std::to_string(v->h) + " -r 60 -i - " + codec + "-pix_fmt yuv420p -movflags +faststart \"" +
                            v->videoTarget + "\"";
    v->pipe = openPipe(cmd);
    if (!v->pipe) {
        v->pngDir = v->out + "_frames";
        std::filesystem::create_directories(v->pngDir);
        std::fprintf(stderr, "ffmpeg not available, writing PNG frames to %s\n", v->pngDir.c_str());
    }
    std::printf("Replay video: %dx%d at 60 fps (rendered %dx%d, %s), clip ticks %lld..%lld -> %s\n", v->w, v->h, v->w * v->ss,
                v->h * v->ss, qualityName(settings_.quality), v->clipIn, replayHeader_.clipOut, v->out.c_str());
    std::fflush(stdout);
    v->start = nowSeconds();
    video_ = v.release();
    return true;
}

bool App::replayVideoDone() const { return video_ && (video_->failed || simClock_.ticks() >= video_->clipOut); }

void App::replayVideoFrame(float aspect, long long tick) {
    ReplayVideo* v = video_;
    if (!v || v->failed) return;
    if (tick > v->clipOut || tick <= v->clipIn - 30) return;   // 30 ticks of pre-roll settle TAA and exposure
    const bool write = tick > v->clipIn;
    if (v->audioOn) v->audio.pollEvents(*scene_);
    // Everything renderFrame leaves behind for the live frames (the shown pose drives interaction picks) is put back.
    const RigPose savedPose = shownPose_;
    const bool savedValid = shownValid_;
    const float savedAspect = lastAspect_;
    const int savedW = lastW_, savedH = lastH_;
    const FrameStats savedStats = stats_;
    const Interactable savedInteract = interact_;
    const bool savedInteractValid = interactValid_;
    const std::string savedPrompt = interactPrompt_;
    const RigPose* savedOverride = viewOverride_;
    viewOverride_ = tickPoseValid_[1] && !thermalFirstPerson() ? &tickPose_[1] : nullptr;   // the tick itself (no blend)
    std::swap(renderer_, videoRenderer_);
    v->hi.bind();
    renderFrame(v->w * v->ss, v->h * v->ss, true);
    std::swap(renderer_, videoRenderer_);
    const RigPose videoPose = shownPose_;
    shownPose_ = savedPose;
    shownValid_ = savedValid;
    lastAspect_ = savedAspect;
    lastW_ = savedW;
    lastH_ = savedH;
    stats_ = savedStats;
    interact_ = savedInteract;
    interactValid_ = savedInteractValid;
    interactPrompt_ = savedPrompt;
    viewOverride_ = savedOverride;
    (void)aspect;
    if (!write) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    if (v->audioOn) {
        v->audio.update(*scene_, videoPose.eye, videoPose.forward(), videoPose.up(), 1.0f / 60.0f);
        v->audio.renderOffline(1.0f / 60.0f, v->block);
        v->wav.write(v->block.data(), v->block.size());
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, v->hi.fbo());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, v->lo.fbo());
    glBlitFramebuffer(0, 0, v->w * v->ss, v->h * v->ss, 0, 0, v->w, v->h, GL_COLOR_BUFFER_BIT, v->ss > 1 ? GL_LINEAR : GL_NEAREST);
    v->lo.read(v->rgb);
    if (v->pipe) {
        if (std::fwrite(v->rgb.data(), 1, v->rgb.size(), v->pipe) != v->rgb.size()) {
            std::fprintf(stderr, "ffmpeg pipe closed early\n");
            v->failed = true;
        }
    } else {
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%05lld.png", v->written);
        writePng(v->pngDir + name, v->w, v->h, v->rgb);
    }
    ++v->written;
    if (v->written % 60 == 0) {
        const double el = nowSeconds() - v->start;
        std::printf("video frame %lld (%.1f s of video, %.1f s elapsed)\n", v->written, v->written / 60.0, el);
        std::fflush(stdout);
    }
    (void)aspect;
}

void App::replayVideoClose() {
    ReplayVideo* v = video_;
    if (!v) return;
    video_ = nullptr;
    const int status = v->pipe ? closePipe(v->pipe) : 0;
    const bool wavOk = v->audioOn && v->wav.close();
    if (v->pipe && v->audioOn && status == 0 && wavOk) {
        const std::string mux = "\"" + opt_.ffmpeg + "\" -y -loglevel error -i \"" + v->silentPath + "\" -i \"" + v->wavPath +
                                "\" -c:v copy -c:a aac -b:a 192k -shortest -movflags +faststart \"" + v->out + "\"";
        if (runCommand(mux) == 0) {
            std::remove(v->silentPath.c_str());
            std::remove(v->wavPath.c_str());
        } else {
            std::fprintf(stderr, "audio mux failed; keeping %s and %s\n", v->silentPath.c_str(), v->wavPath.c_str());
        }
    }
    std::printf("Replay video: %lld frames (%.2f s at 60 fps) -> %s (encoder status %d, %.1f s)\n", v->written, v->written / 60.0,
                v->pipe ? v->out.c_str() : v->pngDir.c_str(), status, nowSeconds() - v->start);
    std::fflush(stdout);
    delete v;
}

}  // namespace ts
