// Video recorder: runs the real simulation at 60 Hz, films it with the cinematic director camera and
// pipes 30 fps RGB frames into ffmpeg (H.264). Without ffmpeg the frames are written as PNG files.
#include <glad/gl.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <string>

#include "app/App.h"
#include "app/HeadlessGL.h"
#include "audio/AudioEngine.h"
#include "audio/Wav.h"
#include "scene/Camera.h"
#include "scene/Director.h"
#include "scene/Pov.h"
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
        for (int k = 0; k < stepsPerFrame; ++k) {
            step(kSimStep);
            if (cut == DirectorCut::V7 && camera_.mode != CameraMode::Cinematic && camera_.mode != CameraMode::Free) updateGameCamera(kSimStep);
            if (audioOn) audioEngine.pollEvents(*scene_);  // scene.events() only holds the last step's events
        }
        if (cut != DirectorCut::V7 || shot.act == V7Act::Camera) {
            if (cut == DirectorCut::V7) camera_.mode = CameraMode::Cinematic;
            camera_.directorTime = tDir;
            applyCinematic();
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
            if (a > 0.0f && shot.act != V7Act::Explorer) {  // the Explorer has its own stage header
                const float x = 56.0f * sc, y = 150.0f * sc;
                hud.rect(x - 18.0f * sc, y - 14.0f * sc, 6.0f * sc, 78.0f * sc, Rgba{1.0f, 0.62f, 0.15f, 0.9f * a});
                hud.text(x, y, shot.name, 34.0f * sc, Rgba{1.0f, 1.0f, 1.0f, a});
                hud.text(x, y + 44.0f * sc, shot.subtitle, 20.0f * sc, Rgba{0.82f, 0.86f, 0.9f, 0.9f * a});
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
