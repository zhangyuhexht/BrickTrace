package dev.bricktrace.client.mixin;

import dev.bricktrace.BrickTraceMod;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(LevelRenderer.class)
public abstract class LevelRendererMixin {
    private static long bricktrace$lastLog;

    @Inject(method = "render", at = @At("HEAD"))
    private void bricktrace$captureFrame(CallbackInfo ci) {
        final long now = System.currentTimeMillis();
        if (now - bricktrace$lastLog < 1000L) {
            return;
        }
        bricktrace$lastLog = now;

        final Camera camera = Minecraft.getInstance().gameRenderer.mainCamera();
        if (camera == null || !camera.isInitialized()) {
            return;
        }
        final Vec3 position = camera.position();
        final int visibleSections = ((LevelRenderer) (Object) this).visibleSections().size();
        BrickTraceMod.LOGGER.info(
                "BrickTrace frame: camera=({}, {}, {}) rot=({}, {}) fov={} visibleSections={}",
                String.format("%.2f", position.x), String.format("%.2f", position.y),
                String.format("%.2f", position.z), String.format("%.1f", camera.xRot()),
                String.format("%.1f", camera.yRot()), String.format("%.1f", camera.getFov()),
                visibleSections);
    }
}
