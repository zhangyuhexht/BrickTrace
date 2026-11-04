package dev.bricktrace.client.mixin;

import com.mojang.renderpearl.backend.vulkan.VulkanBackend;
import com.mojang.renderpearl.backend.vulkan.init.FeatureSet;
import dev.bricktrace.client.rt.BrickTraceFeatureSet;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyVariable;

@Mixin(VulkanBackend.class)
public abstract class VulkanBackendMixin {
    private static final String CREATE_DEVICE =
            "createDevice(Lcom/mojang/renderpearl/backend/vulkan/init/FeatureSet;"
                    + "Lcom/mojang/renderpearl/backend/vulkan/VulkanPhysicalDevice;)Lorg/lwjgl/vulkan/VkDevice;";

    @ModifyVariable(method = CREATE_DEVICE, at = @At("HEAD"), argsOnly = true)
    private static FeatureSet bricktrace$enableRayTracing(FeatureSet featureSet) {
        return featureSet.composite(BrickTraceFeatureSet.get());
    }
}
