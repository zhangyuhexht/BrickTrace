package dev.bricktrace.client.rt;

import com.mojang.renderpearl.backend.vulkan.VulkanPhysicalDevice;
import org.lwjgl.vulkan.VK10;
import org.lwjgl.vulkan.VkDevice;

/** Holds Minecraft's Vulkan device and graphics queue so native code can reuse them. */
public final class BrickTraceDeviceHolder {
    private static VkDevice device;
    private static VulkanPhysicalDevice physicalDevice;
    private static long queue;
    private static int queueFamily;

    private BrickTraceDeviceHolder() {
    }

    public static synchronized void set(VkDevice value, VulkanPhysicalDevice physical, long vkQueue,
                                        int family) {
        device = value;
        physicalDevice = physical;
        queue = vkQueue;
        queueFamily = family;
    }

    public static synchronized VkDevice device() {
        return device;
    }

    public static synchronized VulkanPhysicalDevice physicalDevice() {
        return physicalDevice;
    }

    public static synchronized long queue() {
        return queue;
    }

    public static synchronized int queueFamily() {
        return queueFamily;
    }

    public static synchronized boolean rayTracingEntryPointsPresent() {
        return device != null && VK10.vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR") != 0L;
    }
}
