package dev.bricktrace.client;

import dev.bricktrace.BrickTraceMod;
import dev.bricktrace.BrickTraceVersion;
import dev.bricktrace.client.rt.BrickTraceRtView;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import net.minecraft.resources.Identifier;

public final class BrickTraceClient implements ClientModInitializer {
    private static final Identifier RT_LAYER =
            Identifier.fromNamespaceAndPath(BrickTraceMod.MOD_ID, "rt_layer");
    private static int worldTicks;
    private static boolean screenshotDone;

    @Override
    public void onInitializeClient() {
        BrickTraceMod.LOGGER.info("BrickTrace {} initializing (client)", BrickTraceVersion.VERSION);

        final BrickTraceRtView view = new BrickTraceRtView();
        HudElementRegistry.addFirst(RT_LAYER, (graphics, deltaTracker) -> {
            final Minecraft minecraft = Minecraft.getInstance();
            if (minecraft.level == null || !view.initialize()) {
                return;
            }
            view.render(new BrickTraceRtView.CameraPose(128.0f, 96.0f, 40.0f, 0.0f, -0.22f, 70.0f));
            view.draw(graphics);
        });

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            if (client.level == null) {
                return;
            }
            worldTicks++;
            if (!screenshotDone && worldTicks == 120
                    && FabricLoader.getInstance().isDevelopmentEnvironment()) {
                screenshotDone = true;
                Screenshot.grab(client, true);
                BrickTraceMod.LOGGER.info("BrickTrace: screenshot taken at tick {}", worldTicks);
            }
        });
    }
}
