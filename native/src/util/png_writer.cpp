#include "bricktrace/util/png_writer.hpp"

#include <cstdio>
#include <cstring>

namespace bt {
namespace {

uint32_t crc32(const uint8_t* data, size_t length) {
    static uint32_t table[256];
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        initialized = true;
    }

    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

void putU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void writeChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    putU32(out, static_cast<uint32_t>(data.size()));
    const size_t typeOffset = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uint32_t crc = crc32(out.data() + typeOffset, 4 + data.size());
    putU32(out, crc);
}

}  // namespace

bool writePng(const std::string& path, const uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    // Raw scanlines with filter byte 0.
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (1 + static_cast<size_t>(width) * 3));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        for (int x = 0; x < width; ++x) {
            const uint8_t* px = rgba + (static_cast<size_t>(y) * width + x) * 4;
            raw.push_back(px[0]);
            raw.push_back(px[1]);
            raw.push_back(px[2]);
        }
    }

    // zlib stream: header + stored deflate blocks + adler32.
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78);
    zlib.push_back(0x01);

    constexpr size_t kMaxBlock = 65535;
    for (size_t offset = 0; offset < raw.size(); offset += kMaxBlock) {
        const size_t chunk = (raw.size() - offset) < kMaxBlock ? (raw.size() - offset) : kMaxBlock;
        zlib.push_back(offset + chunk == raw.size() ? 0x01 : 0x00);
        zlib.push_back(static_cast<uint8_t>(chunk & 0xff));
        zlib.push_back(static_cast<uint8_t>(chunk >> 8));
        zlib.push_back(static_cast<uint8_t>((chunk ^ 0xffffu) & 0xff));
        zlib.push_back(static_cast<uint8_t>((chunk ^ 0xffffu) >> 8));
        zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + chunk);
    }

    uint32_t a = 1;
    uint32_t b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    putU32(zlib, (b << 16) | a);

    std::vector<uint8_t> png;
    const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    png.insert(png.end(), signature, signature + 8);

    std::vector<uint8_t> ihdr;
    putU32(ihdr, static_cast<uint32_t>(width));
    putU32(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // color type: truecolor
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    writeChunk(png, "IHDR", ihdr);
    writeChunk(png, "IDAT", zlib);
    writeChunk(png, "IEND", {});

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    const size_t written = std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);
    return written == png.size();
}

}  // namespace bt
