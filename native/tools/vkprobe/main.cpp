#include "bricktrace/core/device_probe.hpp"

#include <vulkan/vulkan.h>

#include <cstdio>

int main() {
    const auto devices = bt::enumerateDevices();
    if (devices.empty()) {
        std::printf("no Vulkan device found\n");
        return 1;
    }

    std::printf("BrickTrace device probe: %zu device(s)\n", devices.size());
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& d = devices[i];
        std::printf("[%zu] %s\n", i, d.name.c_str());
        std::printf("    api=%u.%u.%u driver=%u.%u.%u\n",
                    VK_VERSION_MAJOR(d.apiVersion), VK_VERSION_MINOR(d.apiVersion),
                    VK_VERSION_PATCH(d.apiVersion),
                    (d.driverVersion >> 22) & 0x3ff, (d.driverVersion >> 12) & 0x3ff,
                    d.driverVersion & 0xfff);
        std::printf("    acceleration_structure=%d ray_tracing_pipeline=%d ray_query=%d\n",
                    (int)d.accelerationStructure, (int)d.rayTracingPipeline, (int)d.rayQuery);
        std::printf("    device_local_memory=%.1f GiB\n", d.deviceLocalMemory / 1073741824.0);
        if (d.accelerationStructure && d.rayTracingPipeline) {
            std::printf("    max_geometry_count=%llu shader_group_handle_size=%u "
                        "max_ray_recursion_depth=%u\n",
                        (unsigned long long)d.maxGeometryCount, d.shaderGroupHandleSize,
                        d.maxRayRecursionDepth);
        }
    }

    return 0;
}
