package dev.bricktrace.client;

import dev.bricktrace.BrickTraceMod;
import dev.bricktrace.BrickTraceVersion;
import dev.bricktrace.nativebridge.NativeBridge;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.loader.api.FabricLoader;

import java.nio.file.Path;

public final class BrickTraceClient implements ClientModInitializer {
    private static int ticks;
    private static boolean testFrameDone;

    @Override
    public void onInitializeClient() {
        BrickTraceMod.LOGGER.info("BrickTrace {} initializing (client)", BrickTraceVersion.VERSION);

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            if (client.level == null || client.player == null) {
                return;
            }
            ticks++;
            if (!testFrameDone && ticks == 100 && BrickTraceMod.nativeAvailable()) {
                testFrameDone = true;
                final Path output = FabricLoader.getInstance().getGameDir()
                        .resolve("bricktrace_test_frame.png");
                final boolean ok = NativeBridge.nativeRenderTestFrame(output.toString(),
                        NativeBridge.shaderDir(), 1280, 720);
                BrickTraceMod.LOGGER.info("BrickTrace offscreen RT test frame -> {} ({})",
                        output.toAbsolutePath(), ok ? "ok" : "failed");
            }
        });
    }
}
