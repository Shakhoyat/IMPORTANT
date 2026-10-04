// Renders a 10 s demo mix (ambient, two drones passing, a launch, an explosion, a siren) to a WAV and prints
// RMS / peak. Build: see docs/v5_notes/v5-a5.md.
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/AudioEngine.h"
#include "audio/Wav.h"

using namespace ts;
using namespace ts::audio;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "build/audio_demo.wav";
    AudioEngine e;
    const float fps = 60.0f, dt = 1.0f / fps;
    std::vector<float> all, blk;
    const Vec3 lis{0, 1.7f, 0};
    for (int f = 0; f < 600; ++f) {
        const float t = f * dt;
        e.setListener(lis, {0, 0, -1}, {0, 1, 0}, dt);
        // drone 0 flies left -> right at 12 m/s, 10 m ahead; drone 1 right -> left, 18 m ahead, slower
        const Vec3 p0{-60.0f + 12.0f * t, 6.0f, -10.0f}, v0{12, 0, 0};
        const Vec3 p1{50.0f - 6.0f * t, 8.0f, -18.0f}, v1{-6, 0, 0};
        e.setSource(Loop::Drone, 0, p0, &v0, 12.0f, dt);
        e.setSource(Loop::Drone, 1, p1, &v1, 6.0f, dt);
        e.endFrame(dt);
        GameEvent ev;
        if (f == 180) { ev.type = GameEventType::LockOn; e.handleEvent(ev); }
        if (f == 210) { ev.type = GameEventType::RoundLaunched; ev.position = {-5, 6, -10}; e.handleEvent(ev); }
        if (f == 300) { ev.type = GameEventType::Explosion; ev.position = {14, 0.5f, -30}; ev.value = 150; e.handleEvent(ev); }
        if (f == 306) { ev.type = GameEventType::EnemyDestroyed; ev.value = 100; e.handleEvent(ev); }
        if (f == 420) { ev.type = GameEventType::WaveStart; ev.position = {0, 6, -20}; ev.value = 2; e.handleEvent(ev); }
        e.renderOffline(dt, blk);
        all.insert(all.end(), blk.begin(), blk.end());
    }
    double sum = 0;
    float pk = 0;
    for (float s : all) { sum += static_cast<double>(s) * s; pk = std::max(pk, std::fabs(s)); }
    const double rms = std::sqrt(sum / static_cast<double>(all.size()));
    std::printf("samples %zu (%.2f s)  RMS %.1f dBFS  peak %.2f dBFS\n", all.size() / 2, all.size() / 2.0 / 48000.0,
                20 * std::log10(rms), 20 * std::log10(pk));
    return writeWav16(path, all) && pk < 0.891f ? 0 : 1;
}
