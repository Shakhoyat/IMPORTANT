// ThermalSense: Autonomous UAV Patrol System
// Md. Shakhoyat Rahman Shujon, Roll 2107104. Computer Graphics Lab project.
//
//   ThermalSense                             interactive simulation (F1 = controls)
//   ThermalSense --export-docs FILE          write the scene/primitive tables used by the Excel workbook
//   ThermalSense --capture DIR               render the report screenshots into DIR
//   ThermalSense --benchmark FILE            measure FPS per preset / feature into FILE
//   ThermalSense --trace FILE SECONDS        log the UAV / rover behaviour (4 Hz) to a CSV file
//   ThermalSense --record FILE [SECONDS]     cinematic (default 120 s) video through ffmpeg
// Common options: --quality low|high|ultra|epic, --size WxH, --headless (EGL, no window: remote GPU over SSH),
//                 --gpu N (EGL device), --fullscreen, --fps 30|60, --encoder libx264|h264_nvenc|hevc_nvenc
// v6 world options: --seed N, --time day|night|dusk|dawn|cycle|HH:MM, --cycle-seconds N, --no-garrison
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "app/App.h"
#include "app/DocExport.h"
#include "scene/HudMath.h"

#if defined(_WIN32)
// Laptops with hybrid graphics: ask the NVIDIA / AMD driver to run this program on the discrete GPU.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

void usage() {
    std::printf(
        "usage: ThermalSense [mode] [options]\n"
        "modes:\n"
        "  (none)                       interactive simulation (F1 in the window lists the controls)\n"
        "  --capture DIR [--only a,b]   render the report figures\n"
        "  --record FILE [SECONDS]      cinematic video, default 120 s (needs ffmpeg)\n"
        "  --benchmark FILE             FPS per preset + per-feature ablation + GPU pass times\n"
        "  --bench-shots FILE           fixed shots x day / night: FPS, CPU phases, GPU passes, draws (+ FILE.md)\n"
        "  --trace FILE SECONDS         UAV / rover behaviour log (4 Hz CSV)\n"
        "  --export-docs FILE           GPU-free export of every part, matrix and mesh\n"
        "options:\n"
        "  --quality low|high|ultra|epic   rendering preset (default: auto; EPIC on RTX / discrete GPUs)\n"
        "  --size WxH                    window / output resolution (e.g. 2560x1440, 3840x2160)\n"
        "  --fullscreen                  interactive: full screen on the primary monitor\n"
        "  --headless                    no window: EGL off-screen context (Linux GPU server over SSH)\n"
        "  --gpu N                       headless: EGL device index (or env TS_EGL_DEVICE)\n"
        "  --fps 30|60                   record: video frame rate\n"
        "  --encoder NAME                record: ffmpeg codec (libx264, h264_nvenc, hevc_nvenc)\n"
        "  --ffmpeg PATH                 record: ffmpeg executable\n"
        "world (v6):\n"
        "  --seed N                      run seed: rover routes, patrols, sensor noise (default: random, printed;\n"
        "                                1 for capture / benchmark / trace / record; 0 = the fixed v5 run, ideal sensor)\n"
        "  --time T                      day | night | dusk | dawn | HH:MM (manual clock) | cycle (automatic)\n"
        "                                default: cycle from 16:30 (interactive), night 23:00 manual (offline modes)\n"
        "  --cycle-seconds N             length of one automatic day + night cycle\n"
        "testing (v9):\n"
        "  --input-script FILE           interactive: play timed key / mouse events (scene/InputScript.h)\n"
        "  --frames DIR                  where the script's 'shot' frames go (default script_frames)\n"
        "  --vsync 0|1                   swap interval (default 0: the frame limiter paces the interactive loop)\n"
        "  --fps-limit N                 interactive frame limiter (default: the display refresh; 0 = uncapped)\n"
        "  --no-garrison                 no soldiers\n");
}

int parseQuality(const std::string& v) {
    if (v == "low") return 0;
    if (v == "high") return 1;
    if (v == "ultra") return 2;
    if (v == "epic") return 3;
    return -1;
}

}  // namespace

int main(int argc, char** argv) {
    ts::AppOptions opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::string value = (i + 1 < argc) ? argv[i + 1] : "";
        if (arg == "--export-docs") {
            return ts::exportSceneDocs(value.empty() ? "build/scene_export.json" : value) ? 0 : 1;
        } else if (arg == "--capture") {
            opt.mode = ts::AppOptions::Mode::Capture;
            opt.outPath = value;
            ++i;
        } else if (arg == "--record") {
            opt.mode = ts::AppOptions::Mode::Record;
            opt.outPath = value;
            opt.seconds = 120.0f;  // the director sequence is 120 s long
            ++i;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {  // optional length in seconds
                opt.seconds = static_cast<float>(std::atof(argv[i + 1]));
                ++i;
            }
            if (!opt.sizeGiven) {
                opt.width = 1920;
                opt.height = 1080;
            }
        } else if (arg == "--ffmpeg") {
            opt.ffmpeg = value;
            ++i;
        } else if (arg == "--encoder") {
            opt.encoder = value;
            ++i;
        } else if (arg == "--fps") {
            opt.fps = std::atoi(value.c_str());
            ++i;
        } else if (arg == "--only") {
            opt.only = "," + value + ",";
            ++i;
        } else if (arg == "--benchmark") {
            opt.mode = ts::AppOptions::Mode::Benchmark;
            opt.outPath = value;
            if (!opt.sizeGiven) {
                opt.width = 1280;
                opt.height = 720;
            }
            ++i;
        } else if (arg == "--bench-shots") {   // v9 M0: fixed camera shots x day / night, per-pass timings
            opt.mode = ts::AppOptions::Mode::BenchShots;
            opt.outPath = value;
            if (!opt.sizeGiven) {
                opt.width = 1920;
                opt.height = 1080;
            }
            ++i;
        } else if (arg == "--input-script") {
            opt.inputScript = value;
            ++i;
        } else if (arg == "--frames") {
            opt.framesDir = value;
            ++i;
        } else if (arg == "--fps-limit") {
            opt.fpsLimit = static_cast<float>(std::atof(value.c_str()));
            ++i;
        } else if (arg == "--vsync") {
            opt.vsync = std::atoi(value.c_str()) != 0 ? 1 : 0;
            ++i;
        } else if (arg == "--trace") {
            opt.mode = ts::AppOptions::Mode::Trace;
            opt.outPath = value;
            if (i + 2 < argc) opt.seconds = static_cast<float>(std::atof(argv[i + 2]));
            i += 2;
        } else if (arg == "--quality") {
            opt.quality = parseQuality(value);
            if (opt.quality < 0) {
                std::fprintf(stderr, "--quality must be low, high, ultra or epic\n");
                return 1;
            }
            ++i;
        } else if (arg == "--size") {
            int w = 0, h = 0;
            if (std::sscanf(value.c_str(), "%dx%d", &w, &h) != 2 || w < 64 || h < 64) {
                std::fprintf(stderr, "--size expects WxH, e.g. 1920x1080\n");
                return 1;
            }
            opt.width = w;
            opt.height = h;
            opt.sizeGiven = true;
            ++i;
        } else if (arg == "--headless") {
            opt.headless = true;
        } else if (arg == "--gpu") {
            opt.device = std::atoi(value.c_str());
            ++i;
        } else if (arg == "--fullscreen") {
            opt.fullscreen = true;
        } else if (arg == "--seed") {
            char* end = nullptr;
            const unsigned long v = std::strtoul(value.c_str(), &end, 0);
            if (value.empty() || (end && *end)) {
                std::fprintf(stderr, "--seed expects a number\n");
                return 1;
            }
            opt.seed = static_cast<uint32_t>(v);
            opt.seedGiven = true;
            ++i;
        } else if (arg == "--time") {
            if (!ts::parseTimeOfDay(value, opt.hour, opt.autoCycle)) {
                std::fprintf(stderr, "--time expects day, night, dusk, dawn, cycle or HH:MM\n");
                return 1;
            }
            opt.timeGiven = true;
            ++i;
        } else if (arg == "--cycle-seconds") {
            opt.cycleSeconds = static_cast<float>(std::atof(value.c_str()));
            if (opt.cycleSeconds < 10.0f) {
                std::fprintf(stderr, "--cycle-seconds expects a length of at least 10 s\n");
                return 1;
            }
            ++i;
        } else if (arg == "--no-garrison") {
            opt.garrison = false;
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option '%s' (see --help)\n", arg.c_str());
            return 1;
        }
    }
    if (!opt.seedGiven) {
        // Offline modes are reproducible by default; the interactive game differs on every start.
        if (opt.mode == ts::AppOptions::Mode::Interactive) {
            std::random_device rd;
            const auto ticks = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            opt.seed = ts::mixRunSeed(rd(), ticks);
        } else {
            opt.seed = 1;
        }
    }
    std::printf("Run seed %u (repeat with --seed %u)\n", opt.seed, opt.seed);
    ts::App app(opt);
    return app.run();
}
