// Headless documentation exporter: writes build/scene_export.json without creating a window or GL context.
// Build + run:  mingw32-make docs   (target build/export_docs.exe)
#include <cstdio>
#include <string>

#include "app/DocExport.h"

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "build/scene_export.json";
    if (!ts::exportSceneDocs(path)) {
        std::fprintf(stderr, "export_docs: failed to write %s\n", path.c_str());
        return 1;
    }
    return 0;
}
