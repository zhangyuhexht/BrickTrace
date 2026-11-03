#include "bricktrace/core/device_probe.hpp"

#include <vulkan/vulkan.h>

#include <cstring>

namespace bt {
namespace {

bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

class Instance {
public:
    Instance() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "BrickTrace";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;

        if (vkCreateInstance(&info, nullptr, &handle_) != VK_SUCCESS) {
            handle_ = VK_NULL_HANDLE;
        }
    }

    ~Instance() {
        if (handle_ != VK_NULL_HANDLE) {
            vkDestroyInstance(handle_, nullptr);
        }
    }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    VkInstance get() const { return handle_; }

private:
    VkInstance handle_ = VK_NULL_HANDLE;
};

}  // namespace

std::vector<DeviceInfo> enumerateDevices() {
    std::vector<DeviceInfo> result;

    Instance instance;
    if (instance.get() == VK_NULL_HANDLE) {
        return result;
    }

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance.get(), &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance.get(), &count, devices.data());

    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

        DeviceInfo info;
        info.name = props.deviceName;
        info.apiVersion = props.apiVersion;
        info.driverVersion = props.driverVersion;
        info.accelerationStructure =
            hasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        info.rayTracingPipeline =
            hasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        info.rayQuery = hasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME);

        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(device, &memory);
        for (uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
            if (memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                info.deviceLocalMemory += memory.memoryHeaps[heap].size;
            }
        }

        if (info.accelerationStructure && info.rayTracingPipeline) {
            VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
            rtProps.pNext = &asProps;

            VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            props2.pNext = &rtProps;
            vkGetPhysicalDeviceProperties2(device, &props2);

            info.maxGeometryCount = asProps.maxGeometryCount;
            info.shaderGroupHandleSize = rtProps.shaderGroupHandleSize;
            info.maxRayRecursionDepth = rtProps.maxRayRecursionDepth;
        }

        result.push_back(std::move(info));
    }

    return result;
}

}  // namespace bt
