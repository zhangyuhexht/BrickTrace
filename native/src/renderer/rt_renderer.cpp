#include "bricktrace/renderer/rt_renderer.hpp"
#include "bricktrace/util/png_writer.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

// Vulkan 1.4 loaders only export core (and WSI) entry points from vulkan-1.dll;
// ray tracing entry points are resolved with vkGetDeviceProcAddr at runtime.
namespace {
PFN_vkCreateAccelerationStructureKHR g_createAccelerationStructure = nullptr;
PFN_vkDestroyAccelerationStructureKHR g_destroyAccelerationStructure = nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR g_getAccelerationStructureBuildSizes = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR g_cmdBuildAccelerationStructures = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR g_getAccelerationStructureDeviceAddress = nullptr;
PFN_vkCreateRayTracingPipelinesKHR g_createRayTracingPipelines = nullptr;
PFN_vkGetRayTracingShaderGroupHandlesKHR g_getRayTracingShaderGroupHandles = nullptr;
PFN_vkCmdTraceRaysKHR g_cmdTraceRays = nullptr;
}  // namespace

#define vkCreateAccelerationStructureKHR g_createAccelerationStructure
#define vkDestroyAccelerationStructureKHR g_destroyAccelerationStructure
#define vkGetAccelerationStructureBuildSizesKHR g_getAccelerationStructureBuildSizes
#define vkCmdBuildAccelerationStructuresKHR g_cmdBuildAccelerationStructures
#define vkGetAccelerationStructureDeviceAddressKHR g_getAccelerationStructureDeviceAddress
#define vkCreateRayTracingPipelinesKHR g_createRayTracingPipelines
#define vkGetRayTracingShaderGroupHandlesKHR g_getRayTracingShaderGroupHandles
#define vkCmdTraceRaysKHR g_cmdTraceRays

namespace bt {
namespace {

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
    VkDeviceAddress address = 0;
};

struct SectionGpu {
    int32_t origin[4];
    uint32_t mapBase;
    uint32_t occupancyBase;
    uint32_t voxelBase;
    uint32_t padding;
};

static_assert(sizeof(SectionGpu) == 32, "SectionGpu must match the shader layout");

inline VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> data(size);
    file.seekg(0);
    file.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

}  // namespace

struct RtRenderer::Impl {
    RendererConfig config;
    std::string error;
    std::string deviceName;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queryPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    Buffer readback;

    Buffer sectionBuffer;
    Buffer brickMapBuffer;
    Buffer occupancyBuffer;
    Buffer voxelBuffer;
    Buffer paramsBuffer;
    Buffer sbtBuffer;

    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    Buffer tlasBuffer;
    Buffer tlasScratch;
    Buffer instancesBuffer;

    std::vector<VkAccelerationStructureKHR> blasList;
    std::vector<Buffer> blasBuffers;
    std::vector<Buffer> blasScratchBuffers;
    std::vector<Buffer> aabbBuffers;

    std::vector<uint8_t> pixels;
    double lastGpuMs = 0.0;
    uint64_t blasBytes = 0;
    uint32_t sectionCount = 0;
    uint32_t brickCount = 0;
    uint32_t sbtRecordSize = 0;
    bool ownsDevice = true;
    bool ready = false;

    bool fail(const char* what, VkResult result) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s failed (VkResult %d)", what, static_cast<int>(result));
        error = buffer;
        return false;
    }

    bool failText(const std::string& text) {
        error = text;
        return false;
    }

    bool createInstance();
    bool pickDevice();
    bool createDevice();
    bool createCommands();
    bool createImage();
    bool createDescriptors();
    bool createPipeline();
    bool createQueryPool();
    bool uploadWorld(const WorldData& world);
    bool renderFrame(const Camera& camera);
    bool uploadBuffers(const std::vector<SectionGpu>& sections,
                       const std::vector<uint32_t>& brickMap,
                       const std::vector<uint64_t>& occupancy,
                       const std::vector<uint32_t>& voxels,
                       const std::vector<std::array<float, 6>>& brickBounds,
                       const std::vector<uint32_t>& sectionBrickBase);
    bool buildAccelerationStructures(const std::vector<std::array<float, 6>>& brickBounds,
                                     const std::vector<uint32_t>& brickCounts,
                                     const std::vector<uint32_t>& sectionBrickBase,
                                     uint32_t occupiedSections);
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      Buffer& out);
    void destroyBuffer(Buffer& buffer);
    bool copyToBuffer(Buffer& dst, const void* data, VkDeviceSize size);
    bool readShader(const char* name, std::vector<char>& out);
    bool submitAndWait(VkCommandBuffer cmd);
    VkCommandBuffer beginSingleTime();
    void endSingleTime(VkCommandBuffer cmd);
    void updateParams(const Camera& camera);
    bool init();
    void clearWorld();
};

bool RtRenderer::Impl::createInstance() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "BrickTrace";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "BrickTrace";
    app.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;

    const VkResult result = vkCreateInstance(&info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        return fail("vkCreateInstance", result);
    }
    return true;
}

bool RtRenderer::Impl::pickDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    auto supports = [](VkPhysicalDevice dev) {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, extensions.data());

        auto has = [&](const char* name) {
            for (const auto& e : extensions) {
                if (std::strcmp(e.extensionName, name) == 0) return true;
            }
            return false;
        };
        return has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
               has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
               has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    };

    int chosen = -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (!supports(devices[i])) continue;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[i], &props);
        if (config.deviceIndex >= 0) {
            if (static_cast<int>(i) == config.deviceIndex) {
                chosen = static_cast<int>(i);
                break;
            }
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            chosen = static_cast<int>(i);
            break;
        } else if (chosen < 0) {
            chosen = static_cast<int>(i);
        }
    }

    if (chosen < 0) {
        return failText("no Vulkan device with ray tracing support");
    }

    physicalDevice = devices[static_cast<size_t>(chosen)];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    deviceName = props.deviceName;

    rtProperties.pNext = &asProperties;
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &rtProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);
    return true;
}

bool RtRenderer::Impl::createDevice() {
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    bool found = false;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamily = i;
            found = true;
            break;
        }
    }
    if (!found) {
        return failText("no graphics queue family");
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extensions[] = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    };

    VkPhysicalDeviceVulkan12Features vulkan12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    vulkan12.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    rtFeatures.rayTracingPipeline = VK_TRUE;
    rtFeatures.pNext = &vulkan12;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    asFeatures.accelerationStructure = VK_TRUE;
    asFeatures.pNext = &rtFeatures;

    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.features.shaderInt64 = VK_TRUE;
    features.pNext = &asFeatures;

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 3;
    info.ppEnabledExtensionNames = extensions;
    info.pNext = &features;

    const VkResult result = vkCreateDevice(physicalDevice, &info, nullptr, &device);
    if (result != VK_SUCCESS) {
        return fail("vkCreateDevice", result);
    }
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    auto load = [this](const char* name) {
        return vkGetDeviceProcAddr(device, name);
    };
    g_createAccelerationStructure =
        reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(load("vkCreateAccelerationStructureKHR"));
    g_destroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        load("vkDestroyAccelerationStructureKHR"));
    g_getAccelerationStructureBuildSizes = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        load("vkGetAccelerationStructureBuildSizesKHR"));
    g_cmdBuildAccelerationStructures = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        load("vkCmdBuildAccelerationStructuresKHR"));
    g_getAccelerationStructureDeviceAddress =
        reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
            load("vkGetAccelerationStructureDeviceAddressKHR"));
    g_createRayTracingPipelines = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
        load("vkCreateRayTracingPipelinesKHR"));
    g_getRayTracingShaderGroupHandles =
        reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
            load("vkGetRayTracingShaderGroupHandlesKHR"));
    g_cmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(load("vkCmdTraceRaysKHR"));

    if (!g_createAccelerationStructure || !g_destroyAccelerationStructure ||
        !g_getAccelerationStructureBuildSizes || !g_cmdBuildAccelerationStructures ||
        !g_getAccelerationStructureDeviceAddress || !g_createRayTracingPipelines ||
        !g_getRayTracingShaderGroupHandles || !g_cmdTraceRays) {
        return failText("Vulkan loader does not expose ray tracing entry points");
    }
    return true;
}

bool RtRenderer::Impl::createCommands() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) return fail("vkCreateCommandPool", result);

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) return fail("vkAllocateCommandBuffers", result);

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    result = vkCreateFence(device, &fenceInfo, nullptr, &fence);
    if (result != VK_SUCCESS) return fail("vkCreateFence", result);
    return true;
}

bool RtRenderer::Impl::createQueryPool() {
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = 2;
    const VkResult result = vkCreateQueryPool(device, &info, nullptr, &queryPool);
    if (result != VK_SUCCESS) return fail("vkCreateQueryPool", result);
    return true;
}

bool RtRenderer::Impl::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags properties, Buffer& out) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(device, &info, nullptr, &out.buffer);
    if (result != VK_SUCCESS) return fail("vkCreateBuffer", result);

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, out.buffer, &requirements);

    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);

    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            typeIndex = i;
            break;
        }
    }
    if (typeIndex == UINT32_MAX) {
        return failText("no suitable memory type for buffer");
    }

    VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    const bool needsAddress = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = typeIndex;
    allocInfo.pNext = needsAddress ? &flagsInfo : nullptr;

    result = vkAllocateMemory(device, &allocInfo, nullptr, &out.memory);
    if (result != VK_SUCCESS) return fail("vkAllocateMemory", result);

    result = vkBindBufferMemory(device, out.buffer, out.memory, 0);
    if (result != VK_SUCCESS) return fail("vkBindBufferMemory", result);

    out.size = size;
    if (needsAddress) {
        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = out.buffer;
        out.address = vkGetBufferDeviceAddress(device, &addressInfo);
    }
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        if (vkMapMemory(device, out.memory, 0, size, 0, &out.mapped) != VK_SUCCESS) {
            out.mapped = nullptr;
        }
    }
    return true;
}

void RtRenderer::Impl::destroyBuffer(Buffer& buffer) {
    if (buffer.mapped != nullptr) {
        vkUnmapMemory(device, buffer.memory);
        buffer.mapped = nullptr;
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0;
    buffer.address = 0;
}

bool RtRenderer::Impl::copyToBuffer(Buffer& dst, const void* data, VkDeviceSize size) {
    if (size == 0) return true;
    if (dst.mapped != nullptr) {
        std::memcpy(dst.mapped, data, static_cast<size_t>(size));
        return true;
    }

    Buffer staging;
    if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging)) {
        return false;
    }
    std::memcpy(staging.mapped, data, static_cast<size_t>(size));

    VkCommandBuffer cmd = beginSingleTime();
    VkBufferCopy copy{};
    copy.size = size;
    vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &copy);
    endSingleTime(cmd);
    destroyBuffer(staging);
    return true;
}

VkCommandBuffer RtRenderer::Impl::beginSingleTime() {
    VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &info);
    return commandBuffer;
}

void RtRenderer::Impl::endSingleTime(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkResetFences(device, 1, &fence);
    vkQueueSubmit(queue, 1, &submit, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
}

bool RtRenderer::Impl::submitAndWait(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkResetFences(device, 1, &fence);
    VkResult result = vkQueueSubmit(queue, 1, &submit, fence);
    if (result != VK_SUCCESS) return fail("vkQueueSubmit", result);
    result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) return fail("vkWaitForFences", result);
    return true;
}

bool RtRenderer::Impl::createImage() {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R8G8B8A8_UNORM;
    info.extent = {config.width, config.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(device, &info, nullptr, &image);
    if (result != VK_SUCCESS) return fail("vkCreateImage", result);

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);

    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
    uint32_t typeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            typeIndex = i;
            break;
        }
    }
    if (typeIndex == UINT32_MAX) return failText("no memory type for output image");

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = typeIndex;
    result = vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory);
    if (result != VK_SUCCESS) return fail("vkAllocateMemory(image)", result);

    result = vkBindImageMemory(device, image, imageMemory, 0);
    if (result != VK_SUCCESS) return fail("vkBindImageMemory", result);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    result = vkCreateImageView(device, &viewInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) return fail("vkCreateImageView", result);

    VkCommandBuffer cmd = beginSingleTime();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    endSingleTime(cmd);

    pixels.assign(static_cast<size_t>(config.width) * config.height * 4, 0);
    if (!createBuffer(static_cast<VkDeviceSize>(pixels.size()),
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      readback)) {
        return false;
    }
    return true;
}

bool RtRenderer::Impl::createDescriptors() {
    VkDescriptorSetLayoutBinding bindings[7]{};
    for (uint32_t i = 0; i < 7; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                                 VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                 VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                                 VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    }
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 7;
    layoutInfo.pBindings = bindings;
    VkResult result = vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout);
    if (result != VK_SUCCESS) return fail("vkCreateDescriptorSetLayout", result);

    VkDescriptorPoolSize poolSizes[4] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 4;
    poolInfo.pPoolSizes = poolSizes;
    result = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
    if (result != VK_SUCCESS) return fail("vkCreateDescriptorPool", result);

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    result = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
    if (result != VK_SUCCESS) return fail("vkAllocateDescriptorSets", result);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS) return fail("vkCreatePipelineLayout", result);

    paramsBuffer = {};
    if (!createBuffer(128, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      paramsBuffer)) {
        return false;
    }
    return true;
}

bool RtRenderer::Impl::readShader(const char* name, std::vector<char>& out) {
    const std::string path = config.shaderDir + "/" + name;
    out = readFile(path);
    if (out.empty()) {
        return failText("cannot read shader: " + path);
    }
    return true;
}

bool RtRenderer::Impl::createPipeline() {
    const char* names[] = {"voxel.rgen.spv", "voxel.rmiss.spv", "shadow.rmiss.spv",
                           "voxel.rchit.spv", "shadow.rahit.spv", "voxel.rint.spv"};
    const VkShaderStageFlagBits stages[] = {
        VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
        VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        VK_SHADER_STAGE_ANY_HIT_BIT_KHR, VK_SHADER_STAGE_INTERSECTION_BIT_KHR};

    VkShaderModule modules[6]{};
    VkPipelineShaderStageCreateInfo stageInfos[6]{};
    for (int i = 0; i < 6; ++i) {
        std::vector<char> code;
        if (!readShader(names[i], code)) {
            for (int j = 0; j < i; ++j) vkDestroyShaderModule(device, modules[j], nullptr);
            return false;
        }
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        if (vkCreateShaderModule(device, &moduleInfo, nullptr, &modules[i]) != VK_SUCCESS) {
            for (int j = 0; j < i; ++j) vkDestroyShaderModule(device, modules[j], nullptr);
            return failText("vkCreateShaderModule failed for " + std::string(names[i]));
        }
        stageInfos[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfos[i].stage = stages[i];
        stageInfos[i].module = modules[i];
        stageInfos[i].pName = "main";
    }

    VkRayTracingShaderGroupCreateInfoKHR groups[4]{};
    for (auto& group : groups) {
        group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;
    }
    groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[0].generalShader = 0;
    groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[1].generalShader = 1;
    groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    groups[2].generalShader = 2;
    groups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    groups[3].closestHitShader = 3;
    groups[3].anyHitShader = 4;
    groups[3].intersectionShader = 5;

    VkRayTracingPipelineCreateInfoKHR pipelineInfo{
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    pipelineInfo.stageCount = 6;
    pipelineInfo.pStages = stageInfos;
    pipelineInfo.groupCount = 4;
    pipelineInfo.pGroups = groups;
    pipelineInfo.maxPipelineRayRecursionDepth = 1;
    pipelineInfo.layout = pipelineLayout;

    const VkResult result = vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                           1, &pipelineInfo, nullptr, &pipeline);
    for (auto module : modules) vkDestroyShaderModule(device, module, nullptr);
    if (result != VK_SUCCESS) return fail("vkCreateRayTracingPipelinesKHR", result);

    const uint32_t handleSize = rtProperties.shaderGroupHandleSize;
    const uint32_t baseAlignment = rtProperties.shaderGroupBaseAlignment;
    const uint32_t recordSize = static_cast<uint32_t>(alignUp(handleSize, baseAlignment));
    const uint32_t groupCount = 4;

    std::vector<uint8_t> handles(static_cast<size_t>(groupCount) * handleSize);
    if (vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, groupCount, handles.size(),
                                             handles.data()) != VK_SUCCESS) {
        return failText("vkGetRayTracingShaderGroupHandlesKHR failed");
    }

    const VkDeviceSize sbtSize = static_cast<VkDeviceSize>(recordSize) * (1 + 2 + 1);
    if (!createBuffer(sbtSize,
                      VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      sbtBuffer)) {
        return false;
    }

    auto* data = static_cast<uint8_t*>(sbtBuffer.mapped);
    std::memset(data, 0, static_cast<size_t>(sbtSize));
    for (uint32_t g = 0; g < groupCount; ++g) {
        std::memcpy(data + static_cast<size_t>(g) * recordSize, handles.data() + g * handleSize,
                    handleSize);
    }

    sbtRecordSize = recordSize;
    return true;
}

bool RtRenderer::Impl::init() {
    const bool external = config.externalDevice != 0;
    if (external) {
        device = reinterpret_cast<VkDevice>(static_cast<uintptr_t>(config.externalDevice));
        physicalDevice =
            reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(config.externalPhysicalDevice));
        queue = reinterpret_cast<VkQueue>(static_cast<uintptr_t>(config.externalQueue));
        queueFamily = config.externalQueueFamily;
        ownsDevice = false;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        deviceName = props.deviceName;

        rtProperties.pNext = &asProperties;
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props2.pNext = &rtProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        auto load = [this](const char* name) {
            return vkGetDeviceProcAddr(device, name);
        };
        g_createAccelerationStructure = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
            load("vkCreateAccelerationStructureKHR"));
        g_destroyAccelerationStructure = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
            load("vkDestroyAccelerationStructureKHR"));
        g_getAccelerationStructureBuildSizes =
            reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
                load("vkGetAccelerationStructureBuildSizesKHR"));
        g_cmdBuildAccelerationStructures =
            reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
                load("vkCmdBuildAccelerationStructuresKHR"));
        g_getAccelerationStructureDeviceAddress =
            reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
                load("vkGetAccelerationStructureDeviceAddressKHR"));
        g_createRayTracingPipelines = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
            load("vkCreateRayTracingPipelinesKHR"));
        g_getRayTracingShaderGroupHandles =
            reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
                load("vkGetRayTracingShaderGroupHandlesKHR"));
        g_cmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(load("vkCmdTraceRaysKHR"));

        if (!g_createAccelerationStructure || !g_cmdBuildAccelerationStructures ||
            !g_cmdTraceRays || !g_createRayTracingPipelines) {
            return failText("external device does not expose ray tracing entry points");
        }
    } else {
        if (!createInstance()) return false;
        if (!pickDevice()) return false;
        if (!createDevice()) return false;
    }

    if (!createCommands()) return false;
    if (!createImage()) return false;
    if (!createDescriptors()) return false;
    if (!createPipeline()) return false;
    if (!createQueryPool()) return false;
    ready = true;
    return true;
}

bool RtRenderer::Impl::uploadWorld(const WorldData& world) {
    if (!ready) return false;
    clearWorld();

    std::vector<SectionGpu> sections;
    std::vector<uint32_t> brickMap;
    std::vector<uint64_t> occupancy;
    std::vector<uint32_t> voxels;
    std::vector<std::array<float, 6>> brickBounds;
    std::vector<uint32_t> brickCounts;
    std::vector<SectionData> keptSections;

    for (const SectionData& section : world.sections) {
        // Section voxels are stored as (y, z, x); a brick needs its 4³ sub-block regrouped
        // into (y, z, x) order so the shader's brick-local DDA indexing matches.
        auto brickVoxel = [&section](int brick, int v) {
            const int bx = brick % kBricksPerAxis;
            const int by = (brick / kBricksPerAxis) % kBricksPerAxis;
            const int bz = brick / (kBricksPerAxis * kBricksPerAxis);
            const int lx = v % kBrickSize;
            const int lz = (v / kBrickSize) % kBrickSize;
            const int ly = v / (kBrickSize * kBrickSize);
            const int gx = bx * kBrickSize + lx;
            const int gy = by * kBrickSize + ly;
            const int gz = bz * kBrickSize + lz;
            return section.voxels[(gy * kSectionSize + gz) * kSectionSize + gx];
        };

        uint64_t occ[kBricksPerSection] = {};
        bool any = false;
        for (int brick = 0; brick < kBricksPerSection; ++brick) {
            for (int v = 0; v < kVoxelsPerBrick; ++v) {
                if ((brickVoxel(brick, v) & kMaterialMask) != 0) {
                    occ[brick] |= (1ull << v);
                    any = true;
                }
            }
        }
        if (!any) continue;

        SectionGpu gpu{};
        gpu.origin[0] = section.origin[0];
        gpu.origin[1] = section.origin[1];
        gpu.origin[2] = section.origin[2];
        gpu.mapBase = static_cast<uint32_t>(brickMap.size());
        gpu.occupancyBase = static_cast<uint32_t>(occupancy.size());
        gpu.voxelBase = static_cast<uint32_t>(voxels.size());

        uint32_t count = 0;
        for (int brick = 0; brick < kBricksPerSection; ++brick) {
            if (occ[brick] == 0) continue;
            brickMap.push_back(static_cast<uint32_t>(brick));
            occupancy.push_back(occ[brick]);
            for (int v = 0; v < kVoxelsPerBrick; ++v) {
                voxels.push_back(brickVoxel(brick, v));
            }

            const int bx = brick % kBricksPerAxis;
            const int by = (brick / kBricksPerAxis) % kBricksPerAxis;
            const int bz = brick / (kBricksPerAxis * kBricksPerAxis);
            const float x0 = static_cast<float>(section.origin[0] + bx * kBrickSize);
            const float y0 = static_cast<float>(section.origin[1] + by * kBrickSize);
            const float z0 = static_cast<float>(section.origin[2] + bz * kBrickSize);
            brickBounds.push_back({x0, y0, z0, x0 + kBrickSize, y0 + kBrickSize, z0 + kBrickSize});
            ++count;
        }

        sections.push_back(gpu);
        brickCounts.push_back(count);
        keptSections.push_back(section);
    }

    sectionCount = static_cast<uint32_t>(sections.size());
    brickCount = static_cast<uint32_t>(brickMap.size());
    if (sections.empty()) {
        return failText("world has no non-empty sections");
    }

    if (!createBuffer(sections.size() * sizeof(SectionGpu),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sectionBuffer) ||
        !createBuffer(brickMap.size() * sizeof(uint32_t),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, brickMapBuffer) ||
        !createBuffer(occupancy.size() * sizeof(uint64_t),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, occupancyBuffer) ||
        !createBuffer(voxels.size() * sizeof(uint32_t),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, voxelBuffer)) {
        return false;
    }

    if (!copyToBuffer(sectionBuffer, sections.data(), sections.size() * sizeof(SectionGpu)) ||
        !copyToBuffer(brickMapBuffer, brickMap.data(), brickMap.size() * sizeof(uint32_t)) ||
        !copyToBuffer(occupancyBuffer, occupancy.data(), occupancy.size() * sizeof(uint64_t)) ||
        !copyToBuffer(voxelBuffer, voxels.data(), voxels.size() * sizeof(uint32_t))) {
        return false;
    }

    return buildAccelerationStructures(brickBounds, brickCounts, {}, sectionCount);
}

bool RtRenderer::Impl::buildAccelerationStructures(
    const std::vector<std::array<float, 6>>& brickBounds,
    const std::vector<uint32_t>& brickCounts,
    const std::vector<uint32_t>& /*sectionBrickBase*/, uint32_t occupiedSections) {
    blasList.resize(occupiedSections, VK_NULL_HANDLE);
    blasBuffers.resize(occupiedSections);
    blasScratchBuffers.resize(occupiedSections);
    aabbBuffers.resize(occupiedSections);

    std::vector<VkAccelerationStructureKHR> built;
    built.reserve(occupiedSections);

    size_t boundOffset = 0;
    for (uint32_t slot = 0; slot < occupiedSections; ++slot) {
        const uint32_t count = brickCounts[slot];
        std::vector<VkAabbPositionsKHR> aabbs(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& b = brickBounds[boundOffset + i];
            aabbs[i].minX = b[0];
            aabbs[i].minY = b[1];
            aabbs[i].minZ = b[2];
            aabbs[i].maxX = b[3];
            aabbs[i].maxY = b[4];
            aabbs[i].maxZ = b[5];
        }
        boundOffset += count;

        if (!createBuffer(count * sizeof(VkAabbPositionsKHR),
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          aabbBuffers[slot])) {
            return false;
        }
        std::memcpy(aabbBuffers[slot].mapped, aabbs.data(), count * sizeof(VkAabbPositionsKHR));

        VkAccelerationStructureGeometryKHR geometry{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.aabbs.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.data.deviceAddress = aabbBuffers[slot].address;
        geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        uint32_t maxPrimitiveCount = count;
        VkAccelerationStructureBuildSizesInfoKHR sizes{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(
            device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &maxPrimitiveCount,
            &sizes);

        if (!createBuffer(sizes.accelerationStructureSize,
                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blasBuffers[slot])) {
            return false;
        }

        VkAccelerationStructureCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        createInfo.buffer = blasBuffers[slot].buffer;
        createInfo.size = sizes.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if (vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &blasList[slot]) !=
            VK_SUCCESS) {
            return failText("vkCreateAccelerationStructureKHR(BLAS) failed");
        }

        if (!createBuffer(sizes.buildScratchSize,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blasScratchBuffers[slot])) {
            return false;
        }

        built.push_back(blasList[slot]);
        blasBytes += sizes.accelerationStructureSize;
    }

    // Record every BLAS build into one submission; each slot has its own scratch buffer.
    VkCommandBuffer cmd = beginSingleTime();
    boundOffset = 0;
    for (uint32_t slot = 0; slot < occupiedSections; ++slot) {
        const uint32_t count = brickCounts[slot];

        VkAccelerationStructureGeometryKHR geometry{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.aabbs.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        geometry.geometry.aabbs.data.deviceAddress = aabbBuffers[slot].address;
        geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;
        buildInfo.dstAccelerationStructure = blasList[slot];
        buildInfo.scratchData.deviceAddress = blasScratchBuffers[slot].address;

        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = count;
        const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &ranges);
        boundOffset += count;
    }
    endSingleTime(cmd);

    // TLAS: one instance per section, identity transform, brick AABBs already in world space.
    std::vector<VkAccelerationStructureInstanceKHR> instances(occupiedSections);
    for (uint32_t slot = 0; slot < occupiedSections; ++slot) {
        auto& instance = instances[slot];
        std::memset(&instance, 0, sizeof(instance));
        instance.transform.matrix[0][0] = 1.0f;
        instance.transform.matrix[1][1] = 1.0f;
        instance.transform.matrix[2][2] = 1.0f;
        instance.instanceCustomIndex = slot;
        instance.mask = 0xff;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;

        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        addressInfo.accelerationStructure = blasList[slot];
        instance.accelerationStructureReference =
            vkGetAccelerationStructureDeviceAddressKHR(device, &addressInfo);
    }

    if (!createBuffer(instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      instancesBuffer)) {
        return false;
    }
    std::memcpy(instancesBuffer.mapped, instances.data(), instances.size() * sizeof(instances[0]));

    VkAccelerationStructureGeometryKHR tlasGeometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    tlasGeometry.geometry.instances.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlasGeometry.geometry.instances.data.deviceAddress = instancesBuffer.address;

    VkAccelerationStructureBuildGeometryInfoKHR tlasInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tlasInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    tlasInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasInfo.geometryCount = 1;
    tlasInfo.pGeometries = &tlasGeometry;

    uint32_t instanceCount = occupiedSections;
    VkAccelerationStructureBuildSizesInfoKHR tlasSizes{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &tlasInfo, &instanceCount, &tlasSizes);

    if (!createBuffer(tlasSizes.accelerationStructureSize,
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tlasBuffer) ||
        !createBuffer(tlasSizes.buildScratchSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tlasScratch)) {
        return false;
    }

    VkAccelerationStructureCreateInfoKHR tlasCreate{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    tlasCreate.buffer = tlasBuffer.buffer;
    tlasCreate.size = tlasSizes.accelerationStructureSize;
    tlasCreate.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (vkCreateAccelerationStructureKHR(device, &tlasCreate, nullptr, &tlas) != VK_SUCCESS) {
        return failText("vkCreateAccelerationStructureKHR(TLAS) failed");
    }

    tlasInfo.dstAccelerationStructure = tlas;
    tlasInfo.scratchData.deviceAddress = tlasScratch.address;
    VkAccelerationStructureBuildRangeInfoKHR tlasRange{};
    tlasRange.primitiveCount = instanceCount;
    const VkAccelerationStructureBuildRangeInfoKHR* tlasRanges = &tlasRange;

    cmd = beginSingleTime();
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlasInfo, &tlasRanges);
    endSingleTime(cmd);
    blasBytes += tlasSizes.accelerationStructureSize;

    VkWriteDescriptorSetAccelerationStructureKHR tlasWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    tlasWrite.accelerationStructureCount = 1;
    tlasWrite.pAccelerationStructures = &tlas;

    VkDescriptorBufferInfo sectionInfo{sectionBuffer.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo mapInfo{brickMapBuffer.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo occInfo{occupancyBuffer.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo voxelInfo{voxelBuffer.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo paramsInfo{paramsBuffer.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[7]{};
    for (uint32_t i = 0; i < 7; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
    }
    writes[0].pNext = &tlasWrite;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &sectionInfo;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &mapInfo;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].pBufferInfo = &occInfo;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].pBufferInfo = &voxelInfo;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].pImageInfo = &imageInfo;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[6].pBufferInfo = &paramsInfo;
    vkUpdateDescriptorSets(device, 7, writes, 0, nullptr);

    return true;
}

void RtRenderer::Impl::updateParams(const Camera& camera) {
    struct Params {
        float camPos[4];
        float camForward[4];
        float camRight[4];
        float camUp[4];
        float sunDir[4];
        float misc[4];
    } params{};

    const float cy = std::cos(camera.yaw);
    const float sy = std::sin(camera.yaw);
    const float cp = std::cos(camera.pitch);
    const float sp = std::sin(camera.pitch);

    const float forward[3] = {sy * cp, sp, cy * cp};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float right[3] = {forward[1] * up[2] - forward[2] * up[1],
                      forward[2] * up[0] - forward[0] * up[2],
                      forward[0] * up[1] - forward[1] * up[0]};
    const float rightLength = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    for (float& c : right) c /= (rightLength > 0.0f ? rightLength : 1.0f);
    const float up2[3] = {forward[1] * right[2] - forward[2] * right[1],
                          forward[2] * right[0] - forward[0] * right[2],
                          forward[0] * right[1] - forward[1] * right[0]};

    for (int i = 0; i < 3; ++i) {
        params.camPos[i] = camera.position[i];
        params.camForward[i] = forward[i];
        params.camRight[i] = right[i];
        params.camUp[i] = up2[i];
    }
    const float sun[3] = {0.35f, 0.82f, 0.45f};
    const float sunLength = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
    for (int i = 0; i < 3; ++i) params.sunDir[i] = sun[i] / sunLength;

    params.misc[0] = static_cast<float>(config.width);
    params.misc[1] = static_cast<float>(config.height);
    params.misc[2] = std::tan(camera.fovDegrees * 3.14159265358979f / 360.0f);
    params.misc[3] = 0.0f;

    std::memcpy(paramsBuffer.mapped, &params, sizeof(params));
}

bool RtRenderer::Impl::renderFrame(const Camera& camera) {
    if (!ready || tlas == VK_NULL_HANDLE) return false;

    updateParams(camera);

    vkResetFences(device, 1, &fence);
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &begin);

    vkCmdResetQueryPool(commandBuffer, queryPool, 0, 2);
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 0);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0,
                            1, &descriptorSet, 0, nullptr);

    VkStridedDeviceAddressRegionKHR raygen{sbtBuffer.address, sbtRecordSize, sbtRecordSize};
    VkStridedDeviceAddressRegionKHR miss{sbtBuffer.address + sbtRecordSize, sbtRecordSize,
                                         sbtRecordSize * 2};
    VkStridedDeviceAddressRegionKHR hit{sbtBuffer.address + sbtRecordSize * 3, sbtRecordSize,
                                        sbtRecordSize};
    VkStridedDeviceAddressRegionKHR callable{};
    vkCmdTraceRaysKHR(commandBuffer, &raygen, &miss, &hit, &callable, config.width, config.height,
                      1);
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 1);

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {config.width, config.height, 1};
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer, 1, &copy);

    VkImageMemoryBarrier back{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    back.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    back.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    back.image = image;
    back.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    back.subresourceRange.levelCount = 1;
    back.subresourceRange.layerCount = 1;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr, 1,
                         &back);

    if (!submitAndWait(commandBuffer)) return false;

    uint64_t timestamps[2] = {0, 0};
    if (vkGetQueryPoolResults(device, queryPool, 0, 2, sizeof(timestamps), timestamps,
                              sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        lastGpuMs = static_cast<double>(timestamps[1] - timestamps[0]) *
                    static_cast<double>(props.limits.timestampPeriod) / 1e6;
    }

    std::memcpy(pixels.data(), readback.mapped, pixels.size());
    return true;
}

void RtRenderer::Impl::clearWorld() {
    for (auto as : blasList) {
        if (as != VK_NULL_HANDLE) vkDestroyAccelerationStructureKHR(device, as, nullptr);
    }
    blasList.clear();

    if (tlas != VK_NULL_HANDLE) {
        vkDestroyAccelerationStructureKHR(device, tlas, nullptr);
        tlas = VK_NULL_HANDLE;
    }

    for (auto& buffer : blasBuffers) destroyBuffer(buffer);
    blasBuffers.clear();
    for (auto& buffer : blasScratchBuffers) destroyBuffer(buffer);
    blasScratchBuffers.clear();
    for (auto& buffer : aabbBuffers) destroyBuffer(buffer);
    aabbBuffers.clear();

    destroyBuffer(tlasBuffer);
    destroyBuffer(tlasScratch);
    destroyBuffer(instancesBuffer);
    destroyBuffer(sectionBuffer);
    destroyBuffer(brickMapBuffer);
    destroyBuffer(occupancyBuffer);
    destroyBuffer(voxelBuffer);

    blasBytes = 0;
    sectionCount = 0;
    brickCount = 0;
}

RtRenderer::RtRenderer(const RendererConfig& config) : impl_(std::make_unique<Impl>()) {
    impl_->config = config;
    if (!impl_->init()) {
        impl_->ready = false;
    }
}

RtRenderer::~RtRenderer() {
    if (!impl_ || impl_->device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(impl_->device);
    impl_->clearWorld();

    if (impl_->queryPool != VK_NULL_HANDLE) vkDestroyQueryPool(impl_->device, impl_->queryPool, nullptr);
    if (impl_->pipeline != VK_NULL_HANDLE) vkDestroyPipeline(impl_->device, impl_->pipeline, nullptr);
    if (impl_->sbtBuffer.buffer != VK_NULL_HANDLE) impl_->destroyBuffer(impl_->sbtBuffer);
    if (impl_->paramsBuffer.buffer != VK_NULL_HANDLE) impl_->destroyBuffer(impl_->paramsBuffer);
    if (impl_->readback.buffer != VK_NULL_HANDLE) impl_->destroyBuffer(impl_->readback);
    if (impl_->descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(impl_->device, impl_->descriptorPool, nullptr);
    if (impl_->pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(impl_->device, impl_->pipelineLayout, nullptr);
    if (impl_->descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(impl_->device, impl_->descriptorSetLayout, nullptr);
    if (impl_->imageView != VK_NULL_HANDLE) vkDestroyImageView(impl_->device, impl_->imageView, nullptr);
    if (impl_->image != VK_NULL_HANDLE) vkDestroyImage(impl_->device, impl_->image, nullptr);
    if (impl_->imageMemory != VK_NULL_HANDLE) vkFreeMemory(impl_->device, impl_->imageMemory, nullptr);
    if (impl_->fence != VK_NULL_HANDLE) vkDestroyFence(impl_->device, impl_->fence, nullptr);
    if (impl_->commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(impl_->device, impl_->commandPool, nullptr);
    if (impl_->ownsDevice) {
        if (impl_->device != VK_NULL_HANDLE) vkDestroyDevice(impl_->device, nullptr);
        if (impl_->instance != VK_NULL_HANDLE) vkDestroyInstance(impl_->instance, nullptr);
    }
}

bool RtRenderer::valid() const { return impl_ && impl_->ready; }
const std::string& RtRenderer::error() const { return impl_->error; }
const std::string& RtRenderer::deviceName() const { return impl_->deviceName; }
double RtRenderer::lastGpuMs() const { return impl_->lastGpuMs; }
uint64_t RtRenderer::blasMemoryBytes() const { return impl_->blasBytes; }
uint32_t RtRenderer::renderedSectionCount() const { return impl_->sectionCount; }
uint32_t RtRenderer::renderedBrickCount() const { return impl_->brickCount; }
const std::vector<uint8_t>& RtRenderer::pixels() const { return impl_->pixels; }

bool RtRenderer::uploadWorld(const WorldData& world) { return impl_->uploadWorld(world); }
bool RtRenderer::renderFrame(const Camera& camera) { return impl_->renderFrame(camera); }

bool RtRenderer::savePng(const std::string& path) const {
    return writePng(path, impl_->pixels.data(), static_cast<int>(impl_->config.width),
                    static_cast<int>(impl_->config.height));
}

}  // namespace bt
