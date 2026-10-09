#include "assets/AssetPaths.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <climits>
#endif

namespace ts::asset {

bool fileExists(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

namespace {

std::string parentOf(const std::string& dir) {
    const size_t s = dir.find_last_of("/\\");
    if (s == std::string::npos || s == 0) return "";
    return dir.substr(0, s);
}

}  // namespace

std::string executableDir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "";
    return parentOf(std::string(buf, n));
#else
    char buf[PATH_MAX] = {};
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    return parentOf(std::string(buf, static_cast<size_t>(n)));
#endif
}

std::string findAssetRoot(const std::string& probe) {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("TS_ASSETS"); env && *env) candidates.push_back(env);
    std::string d = executableDir();
    for (int i = 0; i < 4 && !d.empty(); ++i, d = parentOf(d)) candidates.push_back(d + "/assets");
    std::string cwd = ".";
    for (int i = 0; i < 4; ++i, cwd += "/..") candidates.push_back(cwd + "/assets");
    for (const std::string& c : candidates)
        if (fileExists(c + "/" + probe)) return c;
    return "";
}

}  // namespace ts::asset
