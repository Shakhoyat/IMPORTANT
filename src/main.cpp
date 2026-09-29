// ThermalSense: Autonomous UAV Patrol System
// Md. Shakhoyat Rahman Shujon, Roll 2107104. Computer Graphics Lab project.
//
//   ThermalSense                             interactive simulation (F1 = controls)
//   ThermalSense --export-docs FILE          write the scene/primitive tables used by the Excel workbook
//   ThermalSense --capture DIR               render the report screenshots into DIR
//   ThermalSense --benchmark FILE            measure FPS per preset / feature into FILE
//   ThermalSense --trace FILE SECONDS        log the UAV / rover behaviour (4 Hz) to a CSV file
//   ThermalSense --record FILE SECONDS       cinematic video through ffmpeg
// Common options: --quality low|high|ultra|epic, --size WxH, --headless (EGL, no window: remote GPU over SSH),
//                 --gpu N (EGL device), --fullscreen, --fps 30|60, --encoder libx264|h264_nvenc|hevc_nvenc
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/App.h"
#include "app/DocExport.h"

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
        "  --record FILE SECONDS        cinematic video (needs ffmpeg)\n"
        "  --benchmark FILE             FPS per preset + per-feature ablation + GPU pass times\n"
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
        "  --ffmpeg PATH                 record: ffmpeg executable\n");
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
            if (i + 2 < argc) opt.seconds = static_cast<float>(std::atof(argv[i + 2]));
            if (!opt.sizeGiven) {
                opt.width = 1920;
                opt.height = 1080;
            }
            i += 2;
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
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option '%s' (see --help)\n", arg.c_str());
            return 1;
        }
    }
    ts::App app(opt);
    return app.run();
}
