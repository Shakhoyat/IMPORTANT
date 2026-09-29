#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ts {

// Writes an 8-bit RGB PNG (rows top to bottom). Uses uncompressed ("stored") deflate blocks,
// so no zlib dependency is needed. Returns false if the file cannot be written.
bool writePng(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb);

}  // namespace ts
