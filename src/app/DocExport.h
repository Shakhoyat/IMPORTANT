#pragma once

#include <string>

namespace ts {

// Writes build/scene_export.json: primitive statistics, every component's S/R/T, local and world 4x4
// matrices (row-major in the JSON), parent, material (incl. PBR roughness/metalness/surface), lights,
// and the cube's full vertex/index data plus the first vertices of the sphere and cylinder.
// tools/build_workbook.py turns it into the Excel workbook, so the workbook always matches the code.
// Needs no OpenGL context (see tools/export_docs_main.cpp / `mingw32-make docs`).
bool exportSceneDocs(const std::string& path);

}  // namespace ts
