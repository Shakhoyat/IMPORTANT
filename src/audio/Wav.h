// 16-bit PCM WAV output: whole-buffer helpers and a streaming writer (the recorder appends one video frame of audio
// at a time, so a long recording never holds the whole track in memory).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ts::audio {

// 44-byte canonical header for 'dataBytes' of PCM data.
std::vector<uint8_t> wavHeader(uint32_t dataBytes, int sampleRate, int channels);
// Float [-1, 1] -> int16 with clipping (the master limiter keeps the signal below this anyway).
int16_t toPcm16(float v);
// Header + samples in memory.
std::vector<uint8_t> makeWav16(const std::vector<float>& interleaved, int sampleRate = 48000, int channels = 2);
bool writeWav16(const std::string& path, const std::vector<float>& interleaved, int sampleRate = 48000, int channels = 2);

class WavWriter {
public:
    ~WavWriter() { close(); }
    bool open(const std::string& path, int sampleRate = 48000, int channels = 2);
    bool write(const float* interleaved, size_t sampleCount);
    bool close();  // patches the header sizes; false if the file was never opened or a write failed
    uint64_t bytesWritten() const { return bytes_; }
private:
    FILE* f_ = nullptr;
    int rate_ = 48000, channels_ = 2;
    uint64_t bytes_ = 0;
    bool ok_ = true;
};

}  // namespace ts::audio
