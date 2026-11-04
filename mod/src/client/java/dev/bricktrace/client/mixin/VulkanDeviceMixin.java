package dev.bricktrace.client.mixin;

import com.mojang.renderpearl.backend.vulkan.VulkanDevice;
import com.mojang.renderpearl.backend.vulkan.VulkanInstance;
import com.mojang.renderpearl.backend.vulkan.VulkanPhysicalDevice;
import com.mojang.renderpearl.backend.vulkan.checkpoints.CheckpointExtension;
import com.mojang.renderpearl.backend.vulkan.init.FeatureSet;
import dev.bricktrace.BrickTraceMod;
import dev.bricktrace.client.rt.BrickTraceDeviceHolder;
import org.lwjgl.vulkan.VkDevice;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(VulkanDevice.class)
public abstract class VulkanDeviceMixin {
    @Inject(method = "<init>", at = @At("RETURN"))
    private void bricktrace$captureDevice(VulkanInstance instance, VulkanPhysicalDevice physicalDevice,
                                          FeatureSet features, VkDevice device, long vma,
                                          CheckpointExtension checkpoints, CallbackInfo ci) {
        final VulkanDevice self = (VulkanDevice) (Object) this;
        BrickTraceDeviceHolder.set(device, physicalDevice, self.graphicsQueue().vkQueue().address(),
                self.graphicsQueue().queueFamilyIndex());
        BrickTraceMod.LOGGER.info(
                "BrickTrace: captured Minecraft VkDevice ({}), queue family {} rt={}",
                physicalDevice.deviceName(), self.graphicsQueue().queueFamilyIndex(),
                BrickTraceDeviceHolder.rayTracingEntryPointsPresent());
    }
}
