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
    // Warm-up (8 s, no inputs): the UAV patrol develops before the first frame; scripts are keyed to the director
    // clock, which starts after it.
    for (int i = 0; i < 60 * 8; ++i) step(kSimStep);

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
    scriptClock_ = 0.0f;  // the director drives the tank, helicopter, AA gun, doors and the UAV strike
    RenderTarget target;
    if (!target.create(w, h, 0, false)) return 1;  // the renderer anti-aliases (MSAA + TAA) before this target
    std::vector<uint8_t> rgb;
    const double start = nowSeconds();
    for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < stepsPerFrame; ++k) {
            step(kSimStep);
            if (audioOn) audioEngine.pollEvents(*scene_);  // scene.events() only holds the last step's events
        }
        camera_.directorTime += kSimStep * static_cast<float>(stepsPerFrame);
        applyCinematic();
        if (audioOn) {
            const CameraView cv = camera_.compute(*scene_, static_cast<float>(w) / static_cast<float>(h));
            const Vec3 fwd{-cv.view.at(2, 0), -cv.view.at(2, 1), -cv.view.at(2, 2)};
            const Vec3 up{cv.view.at(1, 0), cv.view.at(1, 1), cv.view.at(1, 2)};
            audioEngine.update(*scene_, cv.position, fwd, up, 1.0f / static_cast<float>(fps));
            audioEngine.renderOffline(1.0f / static_cast<float>(fps), audioBlock);
            wav.write(audioBlock.data(), audioBlock.size());
        }
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

}  // namespace ts
