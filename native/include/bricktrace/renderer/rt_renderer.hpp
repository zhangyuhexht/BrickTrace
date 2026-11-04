#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bricktrace/world/voxel_world.hpp"

namespace bt {

struct RendererConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    std::string shaderDir;
    int deviceIndex = -1;

    // When set, reuse an existing Vulkan device instead of creating one (Minecraft's device).
    uint64_t externalDevice = 0;
    uint64_t externalPhysicalDevice = 0;
    uint64_t externalQueue = 0;
    uint32_t externalQueueFamily = 0;
};

struct Camera {
    float position[3] = {128.0f, 96.0f, 128.0f};
    float yaw = 0.0f;    // radians, 0 looks toward +z
    float pitch = -0.35f;
    float fovDegrees = 70.0f;
};

class RtRenderer {
public:
    explicit RtRenderer(const RendererConfig& config);
    ~RtRenderer();

    RtRenderer(const RtRenderer&) = delete;
    RtRenderer& operator=(const RtRenderer&) = delete;

    bool valid() const;
    const std::string& error() const;
    const std::string& deviceName() const;

    bool uploadWorld(const WorldData& world);
    bool renderFrame(const Camera& camera);
    bool savePng(const std::string& path) const;

    double lastGpuMs() const;
    uint64_t blasMemoryBytes() const;
    uint32_t renderedSectionCount() const;
    uint32_t renderedBrickCount() const;

    const std::vector<uint8_t>& pixels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bt
