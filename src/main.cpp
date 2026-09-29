// ThermalSense: Autonomous UAV Patrol System
// Md. Shakhoyat Rahman Shujon, Roll 2107104. Computer Graphics Lab project.
//
//   ThermalSense.exe                       interactive simulation (F1 = controls)
//   ThermalSense.exe --export-docs FILE    write the scene/primitive tables used by the Excel workbook
//   ThermalSense.exe --capture DIR         render the report screenshots into DIR
//   ThermalSense.exe --benchmark FILE      measure FPS (indexed vs non-indexed) into FILE
//   ThermalSense.exe --trace FILE SECONDS  log the UAV / rover behaviour (4 Hz) to a CSV file
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "app/App.h"
#include "app/DocExport.h"

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
            opt.width = 1920;
            opt.height = 1080;
            i += 2;
        } else if (arg == "--ffmpeg") {
            opt.ffmpeg = value;
            ++i;
        } else if (arg == "--only") {
            opt.only = "," + value + ",";
            ++i;
        } else if (arg == "--benchmark") {
            opt.mode = ts::AppOptions::Mode::Benchmark;
            opt.outPath = value;
            opt.width = 1280;
            opt.height = 720;
            ++i;
        } else if (arg == "--trace") {
            opt.mode = ts::AppOptions::Mode::Trace;
            opt.outPath = value;
            if (i + 2 < argc) opt.seconds = static_cast<float>(std::atof(argv[i + 2]));
            i += 2;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: ThermalSense [--export-docs FILE | --capture DIR | --benchmark FILE | --trace FILE SECONDS]\n");
            return 0;
        }
    }
    ts::App app(opt);
    return app.run();
}
