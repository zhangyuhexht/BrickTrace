package dev.bricktrace.client.rt;

import com.mojang.renderpearl.backend.vulkan.VulkanFeatureSets;
import com.mojang.renderpearl.backend.vulkan.init.FeatureSet;
import com.mojang.renderpearl.backend.vulkan.init.VulkanFeature;
import com.mojang.renderpearl.backend.vulkan.init.VulkanPNextStruct;
import org.lwjgl.vulkan.VkPhysicalDeviceAccelerationStructureFeaturesKHR;
import org.lwjgl.vulkan.VkPhysicalDeviceRayTracingPipelineFeaturesKHR;

import java.util.Set;

/**
 * Ray tracing features that must be enabled when Minecraft creates its Vulkan device.
 * They are composited into the game's own FeatureSet, so BrickTrace can create BLAS/TLAS,
 * ray tracing pipelines and device-address buffers on the exact same VkDevice.
 */
public final class BrickTraceFeatureSet {
    private static FeatureSet instance;

    private BrickTraceFeatureSet() {
    }

    public static synchronized FeatureSet get() {
        if (instance == null) {
            final VulkanPNextStruct accelerationStructure =
                    new VulkanPNextStruct(VkPhysicalDeviceAccelerationStructureFeaturesKHR.class);
            final VulkanPNextStruct rayTracingPipeline =
                    new VulkanPNextStruct(VkPhysicalDeviceRayTracingPipelineFeaturesKHR.class);

            instance = new FeatureSet(
                    "BrickTrace ray tracing",
                    Set.of(
                            "VK_KHR_acceleration_structure",
                            "VK_KHR_ray_tracing_pipeline",
                            "VK_KHR_deferred_host_operations"),
                    Set.of(
                            new VulkanFeature(accelerationStructure, "accelerationStructure"),
                            new VulkanFeature(rayTracingPipeline, "rayTracingPipeline"),
                            new VulkanFeature(VulkanFeatureSets.VK12_FEATURES_STRUCT, "bufferDeviceAddress"),
                            new VulkanFeature(VulkanFeatureSets.VK10_FEATURES_STRUCT, "shaderInt64")));
        }
        return instance;
    }
}
