#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bt {

// Writes an 8-bit RGB PNG. Uses stored (uncompressed) deflate blocks to avoid
// a zlib dependency; images are written once for verification, not per frame.
bool writePng(const std::string& path, const uint8_t* rgba, int width, int height);

}  // namespace bt
