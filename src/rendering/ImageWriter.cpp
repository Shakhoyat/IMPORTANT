#include "rendering/ImageWriter.h"

#include <algorithm>
#include <fstream>

namespace ts {

namespace {

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()) ^ 0xFFFFFFFFu);
}

}  // namespace

bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb) {
    if (width <= 0 || height <= 0 || rgb.size() < static_cast<size_t>(width) * height * 3) return false;

    // Raw scanlines, each prefixed with filter type 0.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * width * 3,
                   rgb.begin() + static_cast<size_t>(y + 1) * width * 3);
    }

    // zlib stream made of stored blocks (max 65535 bytes each) + Adler-32.
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, static_cast<uint32_t>(width));
    put32(ihdr, static_cast<uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, RGB, deflate, no filter, no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(f);
}

}  // namespace ts
