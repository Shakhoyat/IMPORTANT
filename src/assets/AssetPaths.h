// v9 M6: where the assets/ folder is. Searched in this order: $TS_ASSETS, the executable's directory and up to three
// parents (ThermalSense.exe in the repository root, build/ts-* one level down), then the working directory and its
// parents. Empty when not found: the renderer then keeps the procedural v8 vegetation and terrain shading.
#pragma once

#include <string>

namespace ts::asset {

std::string executableDir();
// Directory that contains 'probe' (e.g. "vegetation/fir_tree_01/fir_tree_01.gltf"); returns ".../assets" or "".
std::string findAssetRoot(const std::string& probe);
bool fileExists(const std::string& path);

}  // namespace ts::asset
