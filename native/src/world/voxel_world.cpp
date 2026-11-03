#include "bricktrace/world/voxel_world.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bt {
namespace {

constexpr uint32_t materialAir = 0;
constexpr uint32_t materialStone = 1;
constexpr uint32_t materialDirt = 2;
constexpr uint32_t materialGrass = 3;
constexpr uint32_t materialWater = 4;

uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(y) * 0xd8163841u;
    h ^= static_cast<uint32_t>(z) * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x9e3779b1u;
    h ^= h >> 15;
    return h;
}

float valueNoise(float x, float z, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uz = fz * fz * (3.0f - 2.0f * fz);

    auto corner = [&](int cx, int cz) {
        return static_cast<float>(hash3(cx, 0, cz, seed) & 0xffffu) / 65535.0f;
    };

    const float a = corner(ix, iz);
    const float b = corner(ix + 1, iz);
    const float c = corner(ix, iz + 1);
    const float d = corner(ix + 1, iz + 1);
    const float top = a + (b - a) * ux;
    const float bottom = c + (d - c) * ux;
    return top + (bottom - top) * uz;
}

}  // namespace

WorldData generateDemoWorld(int sizeX, int sizeY, int sizeZ, uint32_t seed) {
    WorldData world;
    world.size[0] = sizeX;
    world.size[1] = sizeY;
    world.size[2] = sizeZ;
    world.sectionCount[0] = sizeX / kSectionSize;
    world.sectionCount[1] = sizeY / kSectionSize;
    world.sectionCount[2] = sizeZ / kSectionSize;

    world.sections.resize(static_cast<size_t>(world.sectionCount[0]) * world.sectionCount[1] *
                          world.sectionCount[2]);

    const int seaLevel = sizeY / 4;
    const float baseHeight = static_cast<float>(sizeY) * 0.30f;

    std::vector<int> height(static_cast<size_t>(sizeX) * sizeZ);
    for (int z = 0; z < sizeZ; ++z) {
        for (int x = 0; x < sizeX; ++x) {
            const float fx = static_cast<float>(x) / 24.0f;
            const float fz = static_cast<float>(z) / 24.0f;
            const float n = valueNoise(fx, fz, seed) * 0.7f +
                            valueNoise(fx * 2.5f, fz * 2.5f, seed ^ 0x9e3779b9u) * 0.3f;
            height[static_cast<size_t>(z) * sizeX + x] = static_cast<int>(baseHeight + n * 16.0f);
        }
    }

    for (int sy = 0; sy < world.sectionCount[1]; ++sy) {
        for (int sz = 0; sz < world.sectionCount[2]; ++sz) {
            for (int sx = 0; sx < world.sectionCount[0]; ++sx) {
                SectionData& section = world.sections[static_cast<size_t>(sectionIndex(world, sx, sy, sz))];
                section.origin[0] = sx * kSectionSize;
                section.origin[1] = sy * kSectionSize;
                section.origin[2] = sz * kSectionSize;

                bool any = false;
                for (int ly = 0; ly < kSectionSize; ++ly) {
                    for (int lz = 0; lz < kSectionSize; ++lz) {
                        for (int lx = 0; lx < kSectionSize; ++lx) {
                            const int wx = section.origin[0] + lx;
                            const int wy = section.origin[1] + ly;
                            const int wz = section.origin[2] + lz;
                            const int h = height[static_cast<size_t>(wz) * sizeX + wx];

                            uint32_t material = materialAir;
                            if (wy > h) {
                                material = wy <= seaLevel ? materialWater : materialAir;
                            } else if (wy == h) {
                                material = h <= seaLevel ? materialDirt : materialGrass;
                            } else if (wy > h - 4) {
                                material = materialDirt;
                            } else {
                                material = materialStone;
                            }

                            uint32_t voxel = 0;
                            if (material != materialAir) {
                                const uint32_t sky = material == materialWater ? 8u
                                                    : (wy >= h ? 15u : 0u);
                                voxel = material | (sky << kSkyLightShift);
                                if (material != materialWater) {
                                    voxel |= kFlagOpaque;
                                }
                            }

                            section.voxels[(ly * kSectionSize + lz) * kSectionSize + lx] = voxel;
                            any = any || voxel != 0;
                        }
                    }
                }

                (void)any;
            }
        }
    }

    return world;
}

}  // namespace bt
