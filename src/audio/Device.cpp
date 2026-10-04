// miniaudio playback device (the only translation unit that includes the library implementation). Compiles on Linux
// (needs -ldl -lpthread -lm) and Windows (WASAPI is loaded dynamically). Without any usable backend startDevice()
// returns false and the game simply runs silent.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_NULL
#define MINIAUDIO_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
#include "miniaudio.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "audio/AudioEngine.h"

namespace ts::audio {

namespace {
void dataCallback(ma_device* dev, void* output, const void*, ma_uint32 frames) {
    static_cast<AudioEngine*>(dev->pUserData)->render(static_cast<float*>(output), static_cast<int>(frames));
}
}  // namespace

bool AudioEngine::startDevice() {
    if (device_) return true;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = kSampleRate;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = this;
    ma_device* dev = new ma_device;
    if (ma_device_init(nullptr, &cfg, dev) != MA_SUCCESS) {
        delete dev;
        return false;
    }
    prewarm();
    if (ma_device_start(dev) != MA_SUCCESS) {
        ma_device_uninit(dev);
        delete dev;
        return false;
    }
    device_ = dev;
    return true;
}

void AudioEngine::stop() {
    if (!device_) return;
    ma_device* dev = static_cast<ma_device*>(device_);
    device_ = nullptr;
    ma_device_uninit(dev);
    delete dev;
}

}  // namespace ts::audio
