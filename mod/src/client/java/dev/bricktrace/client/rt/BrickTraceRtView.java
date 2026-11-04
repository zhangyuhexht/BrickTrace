package dev.bricktrace.client.rt;

import com.mojang.blaze3d.platform.NativeImage;
import dev.bricktrace.BrickTraceMod;
import dev.bricktrace.nativebridge.NativeBridge;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiGraphicsExtractor;
import net.minecraft.client.renderer.texture.DynamicTexture;
import net.minecraft.resources.Identifier;
import org.lwjgl.system.MemoryUtil;

import java.nio.ByteBuffer;

/** Renders a ray traced frame on Minecraft's Vulkan device and presents it as a full screen HUD layer. */
public final class BrickTraceRtView implements AutoCloseable {
    public static final int WIDTH = 960;
    public static final int HEIGHT = 540;
    private static final Identifier TEXTURE_ID =
            Identifier.fromNamespaceAndPath(BrickTraceMod.MOD_ID, "rt_frame");

    private long handle;
    private NativeImage image;
    private DynamicTexture texture;
    private ByteBuffer pixels;
    private boolean ready;
    private boolean failed;
    private boolean loggedCenterPixel;
    private boolean worldUploaded;

    public boolean initialize() {
        if (ready || failed) {
            return ready;
        }
        if (!BrickTraceMod.nativeAvailable() || BrickTraceDeviceHolder.device() == null) {
            return false;
        }

        handle = NativeBridge.nativeCreateRenderer(NativeBridge.shaderDir(), WIDTH, HEIGHT);
        if (handle == 0L) {
            BrickTraceMod.LOGGER.error("BrickTrace: native renderer creation failed");
            failed = true;
            return false;
        }

        image = new NativeImage(WIDTH, HEIGHT, true);
        texture = new DynamicTexture(() -> "bricktrace-rt", image);
        Minecraft.getInstance().getTextureManager().register(TEXTURE_ID, texture);
        pixels = MemoryUtil.memAlloc(WIDTH * HEIGHT * 4);
        ready = true;
        BrickTraceMod.LOGGER.info("BrickTrace: RT view ready ({}x{})", WIDTH, HEIGHT);
        return true;
    }

    /** Captures the loaded chunks around the player once and uploads them to the renderer. */
    public void ensureWorld(net.minecraft.client.multiplayer.ClientLevel level, int chunkX,
                            int chunkZ) {
        if (!ready || worldUploaded) {
            return;
        }
        final BrickTraceWorldCapture.Snapshot snapshot =
                BrickTraceWorldCapture.capture(level, chunkX, chunkZ);
        if (snapshot.sectionCount() > 0
                && NativeBridge.nativeUploadWorld(handle, snapshot.origins(), snapshot.voxels())) {
            worldUploaded = true;
            BrickTraceMod.LOGGER.info("BrickTrace: uploaded {} real sections from the client world",
                    snapshot.sectionCount());
            return;
        }
        worldUploaded = NativeBridge.nativeUploadDemoWorld(handle);
        BrickTraceMod.LOGGER.warn("BrickTrace: no real sections captured, using demo world ({})",
                worldUploaded);
    }

    /** Renders one ray traced frame. Called once per client frame while a world is loaded. */
    public void render(CameraPose pose) {
        if (!ready) {
            return;
        }
        if (!NativeBridge.nativeRenderFrame(handle, pose.x, pose.y, pose.z, pose.yawRadians,
                pose.pitchRadians, pose.fovDegrees)) {
            return;
        }
        if (!NativeBridge.nativeCopyPixels(handle, pixels)) {
            return;
        }
        MemoryUtil.memCopy(MemoryUtil.memAddress(pixels), image.getPointer(), (long) WIDTH * HEIGHT * 4);
        texture.upload();

        if (!loggedCenterPixel) {
            loggedCenterPixel = true;
            final int offset = ((HEIGHT / 2) * WIDTH + WIDTH / 2) * 4;
            BrickTraceMod.LOGGER.info("BrickTrace: RT center pixel rgba=({}, {}, {}, {})",
                    pixels.get(offset) & 0xff, pixels.get(offset + 1) & 0xff,
                    pixels.get(offset + 2) & 0xff, pixels.get(offset + 3) & 0xff);
        }
    }

    public void draw(GuiGraphicsExtractor graphics) {
        if (!ready || texture == null) {
            return;
        }
        final Minecraft minecraft = Minecraft.getInstance();
        graphics.blit(texture.getTextureView(), texture.getSampler(), 0, 0,
                minecraft.getWindow().getGuiScaledWidth(),
                minecraft.getWindow().getGuiScaledHeight(), 0.0f, 1.0f, 0.0f, 1.0f);
    }

    @Override
    public void close() {
        if (handle != 0L) {
            NativeBridge.nativeDestroyRenderer(handle);
            handle = 0L;
        }
        if (pixels != null) {
            MemoryUtil.memFree(pixels);
            pixels = null;
        }
        if (texture != null) {
            Minecraft.getInstance().getTextureManager().release(TEXTURE_ID);
            texture = null;
        }
        if (image != null) {
            image.close();
            image = null;
        }
        ready = false;
    }

    public record CameraPose(float x, float y, float z, float yawRadians, float pitchRadians,
                             float fovDegrees) {
    }
}
