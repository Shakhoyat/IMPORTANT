// Video recorder: runs the real simulation at 60 Hz, films it with the cinematic director camera and
// pipes 30 fps RGB frames into ffmpeg (H.264). Without ffmpeg the frames are written as PNG files.
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <string>

#include "app/App.h"
#include "rendering/ImageWriter.h"

namespace ts {

namespace {

// The paths go into a cmd.exe command line: refuse characters that could break the quoting or chain commands.
bool safeForShell(const std::string& s) { return !s.empty() && s.find_first_of("\"&|^<>%!") == std::string::npos; }

constexpr float kSimStep = 1.0f / 60.0f;
constexpr int kStepsPerFrame = 2;  // 30 fps video from a 60 Hz simulation

}  // namespace

int App::recordMode() {
    const int w = opt_.width, h = opt_.height;
    const std::string out = opt_.outPath.empty() ? "report/demo.mp4" : opt_.outPath;
    const int frames = static_cast<int>(opt_.seconds * 60.0f / kStepsPerFrame);

    if (!safeForShell(out) || !safeForShell(opt_.ffmpeg)) {
        std::fprintf(stderr, "--record: output path and --ffmpeg path must not contain \" & | ^ < > %% !\n");
        return 1;
    }
    // Warm-up: let the chase develop before the first frame.
    for (int i = 0; i < 60 * 8; ++i) step(kSimStep);

    const std::string cmd = "\"" + opt_.ffmpeg + "\" -y -loglevel error -f rawvideo -pix_fmt rgb24 -s " +
                            std::to_string(w) + "x" + std::to_string(h) + " -r 30 -i - -c:v libx264 -preset slow " +
                            "-crf 17 -pix_fmt yuv420p -movflags +faststart \"" + out + "\"";
    FILE* pipe = _popen(("\"" + cmd + "\"").c_str(), "wb");  // cmd.exe strips one outer pair of quotes
    std::string pngDir;
    if (!pipe) {
        pngDir = out + "_frames";
        std::filesystem::create_directories(pngDir);
        std::fprintf(stderr, "ffmpeg not available, writing PNG frames to %s\n", pngDir.c_str());
    }

    settings_ = RenderSettings{};
    settings_.quality = Quality::Ultra;
    camera_ = Camera{};
    camera_.mode = CameraMode::Cinematic;
    RenderTarget target;
    GLint maxSamples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    if (!target.create(w, h, std::min(4, static_cast<int>(maxSamples)), false)) return 1;
    std::vector<uint8_t> rgb;
    const double start = glfwGetTime();
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < kStepsPerFrame; ++k) step(kSimStep);
        camera_.directorTime += kSimStep * kStepsPerFrame;
        applyCinematic();
        target.bind();
        renderFrame(w, h, true);
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
        if (f % 30 == 0)
            std::printf("frame %d / %d  (%.1f s elapsed)\n", f, frames, glfwGetTime() - start);
        std::fflush(stdout);
    }
    const int status = pipe ? _pclose(pipe) : 0;
    std::printf("Recorded %d frames to %s (encoder status %d)\n", frames, pipe ? out.c_str() : pngDir.c_str(), status);
    return status == 0 ? 0 : 1;
}

}  // namespace ts
