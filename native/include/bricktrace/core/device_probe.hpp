#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bt {

struct DeviceInfo {
    std::string name;
    uint32_t apiVersion = 0;
    uint32_t driverVersion = 0;
    bool accelerationStructure = false;
    bool rayTracingPipeline = false;
    bool rayQuery = false;
    uint64_t deviceLocalMemory = 0;
    uint64_t maxGeometryCount = 0;
    uint32_t shaderGroupHandleSize = 0;
    uint32_t maxRayRecursionDepth = 0;
};

// Enumerates physical devices, querying KHR ray tracing support per device.
std::vector<DeviceInfo> enumerateDevices();

}  // namespace bt
