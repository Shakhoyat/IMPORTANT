#include "audio/Wav.h"

#include <cmath>

namespace ts::audio {

namespace {
void put32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF)); }
void put16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(static_cast<uint8_t>(x & 0xFF)); v.push_back(static_cast<uint8_t>(x >> 8)); }
void putTag(std::vector<uint8_t>& v, const char* t) { for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(t[i])); }
}  // namespace

std::vector<uint8_t> wavHeader(uint32_t dataBytes, int sampleRate, int channels) {
    std::vector<uint8_t> h;
    h.reserve(44);
    putTag(h, "RIFF");
    put32(h, 36u + dataBytes);
    putTag(h, "WAVE");
    putTag(h, "fmt ");
    put32(h, 16);
    put16(h, 1);  // PCM
    put16(h, static_cast<uint16_t>(channels));
    put32(h, static_cast<uint32_t>(sampleRate));
    put32(h, static_cast<uint32_t>(sampleRate * channels * 2));
    put16(h, static_cast<uint16_t>(channels * 2));
    put16(h, 16);
    putTag(h, "data");
    put32(h, dataBytes);
    return h;
}

int16_t toPcm16(float v) {
    if (!(v == v)) return 0;
    const float c = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    return static_cast<int16_t>(std::lrint(c * 32767.0f));
}

std::vector<uint8_t> makeWav16(const std::vector<float>& s, int rate, int channels) {
    std::vector<uint8_t> out = wavHeader(static_cast<uint32_t>(s.size() * 2), rate, channels);
    out.reserve(out.size() + s.size() * 2);
    for (float f : s) put16(out, static_cast<uint16_t>(toPcm16(f)));
    return out;
}

bool writeWav16(const std::string& path, const std::vector<float>& s, int rate, int channels) {
    WavWriter w;
    if (!w.open(path, rate, channels)) return false;
    w.write(s.data(), s.size());
    return w.close();
}

bool WavWriter::open(const std::string& path, int sampleRate, int channels) {
    close();
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) return false;
    rate_ = sampleRate; channels_ = channels; bytes_ = 0; ok_ = true;
    const std::vector<uint8_t> h = wavHeader(0, rate_, channels_);
    ok_ = std::fwrite(h.data(), 1, h.size(), f_) == h.size();
    return ok_;
}

bool WavWriter::write(const float* s, size_t count) {
    if (!f_) return false;
    std::vector<uint8_t> buf;
    buf.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) put16(buf, static_cast<uint16_t>(toPcm16(s[i])));
    if (std::fwrite(buf.data(), 1, buf.size(), f_) != buf.size()) ok_ = false;
    bytes_ += buf.size();
    return ok_;
}

bool WavWriter::close() {
    if (!f_) return false;
    const std::vector<uint8_t> h = wavHeader(static_cast<uint32_t>(bytes_), rate_, channels_);
    std::fseek(f_, 0, SEEK_SET);
    if (std::fwrite(h.data(), 1, h.size(), f_) != h.size()) ok_ = false;
    std::fclose(f_);
    f_ = nullptr;
    return ok_;
}

}  // namespace ts::audio
