#pragma once

#include <cstdint>
#include <vector>

namespace bt {

constexpr int kSectionSize = 16;
constexpr int kBrickSize = 4;
constexpr int kBricksPerAxis = kSectionSize / kBrickSize;   // 4
constexpr int kBricksPerSection = kBricksPerAxis * kBricksPerAxis * kBricksPerAxis;  // 64
constexpr int kVoxelsPerBrick = kBrickSize * kBrickSize * kBrickSize;                // 64
constexpr int kVoxelsPerSection = kSectionSize * kSectionSize * kSectionSize;        // 4096

// Voxel layout: bits 0..15 material, 16..19 block light, 20..23 sky light, 24..31 flags.
constexpr uint32_t kMaterialMask = 0xffffu;
constexpr uint32_t kBlockLightShift = 16;
constexpr uint32_t kSkyLightShift = 20;
constexpr uint32_t kFlagOpaque = 1u << 24;

struct SectionData {
    int32_t origin[3] = {0, 0, 0};
    uint32_t brickBase = 0;       // index into the brick arrays
    uint64_t occupancy[kBricksPerSection] = {};
    uint32_t voxels[kVoxelsPerSection] = {};
};

struct WorldData {
    std::vector<SectionData> sections;
    int32_t minSection[3] = {0, 0, 0};
    int32_t sectionCount[3] = {0, 0, 0};
    int32_t size[3] = {0, 0, 0};  // in blocks
};

// Deterministic procedural terrain: grass surface, dirt, stone, water at sea level.
WorldData generateDemoWorld(int sizeX, int sizeY, int sizeZ, uint32_t seed);

// 16³ section index helpers.
inline int sectionIndex(const WorldData& world, int sx, int sy, int sz) {
    return (sy * world.sectionCount[2] + sz) * world.sectionCount[0] + sx;
}

}  // namespace bt
