package dev.bricktrace.client;

import dev.bricktrace.BrickTraceMod;
import dev.bricktrace.BrickTraceVersion;
import dev.bricktrace.client.rt.BrickTraceDirtySections;
import dev.bricktrace.client.rt.BrickTraceRtView;
import dev.bricktrace.client.rt.BrickTraceSettings;
import dev.bricktrace.client.rt.BrickTraceSettingsScreen;
import dev.bricktrace.client.rt.BrickTraceWorldCapture;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElementRegistry;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import net.minecraft.core.BlockPos;
import net.minecraft.resources.Identifier;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.chunk.ChunkAccess;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.level.chunk.status.ChunkStatus;

public final class BrickTraceClient implements ClientModInitializer {
    private static final Identifier RT_LAYER =
            Identifier.fromNamespaceAndPath(BrickTraceMod.MOD_ID, "rt_layer");
    private static BrickTraceRtView view;
    private static int worldTicks;
    private static boolean screenshotDone;
    private static boolean afterScreenshotDone;
    private static boolean editDone;
    private static boolean f9WasDown;
    private static BlockPos pillarPos;

    @Override
    public void onInitializeClient() {
        BrickTraceMod.LOGGER.info("BrickTrace {} initializing (client)", BrickTraceVersion.VERSION);
        BrickTraceSettings.load();

        view = new BrickTraceRtView();
        HudElementRegistry.addFirst(RT_LAYER, (graphics, deltaTracker) -> {
            final Minecraft minecraft = Minecraft.getInstance();
            if (minecraft.level == null || minecraft.player == null || !view.initialize()) {
                return;
            }
            view.ensureWorld(minecraft.level,
                    minecraft.player.chunkPosition().getMinBlockX() >> 4,
                    minecraft.player.chunkPosition().getMinBlockZ() >> 4);

            final net.minecraft.client.Camera camera = minecraft.gameRenderer.mainCamera();
            if (camera == null || !camera.isInitialized()) {
                return;
            }
            final net.minecraft.world.phys.Vec3 position = camera.position();
            view.render(new BrickTraceRtView.CameraPose((float) position.x, (float) position.y,
                    (float) position.z, (float) Math.toRadians(-camera.yRot()),
                    (float) Math.toRadians(-camera.xRot()), camera.getFov()));
            view.draw(graphics);
        });

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            final boolean f9Down =
                    com.mojang.blaze3d.platform.InputConstants.isKeyDown(
                            com.mojang.blaze3d.platform.InputConstants.KEY_F9);
            if (f9Down && !f9WasDown && client.level != null) {
                client.setScreenAndShow(new BrickTraceSettingsScreen());
            }
            f9WasDown = f9Down;

            if (client.level == null || client.player == null) {
                return;
            }
            processDirtySections(client);
            worldTicks++;

            final boolean dev = BrickTraceSettings.autoTest;
            if (dev && !editDone && worldTicks == 160) {
                editDone = true;
                // Place a small stone pillar in front of the player to exercise the update path.
                final BlockPos target = BlockPos.containing(client.player.getEyePosition()
                        .add(client.player.getLookAngle().scale(8.0)));
                pillarPos = target;
                for (int y = 0; y < 3; ++y) {
                    client.level.setBlockAndUpdate(target.above(y), Blocks.STONE.defaultBlockState());
                }
                BrickTraceMod.LOGGER.info("BrickTrace: placed test blocks at {}", target);
            }
            if (dev && worldTicks == 260 && pillarPos != null) {
                for (int y = 0; y < 3; ++y) {
                    client.level.setBlockAndUpdate(pillarPos.above(y), Blocks.AIR.defaultBlockState());
                }
                BrickTraceMod.LOGGER.info("BrickTrace: removed test blocks at {}", pillarPos);
            }
            if (dev && worldTicks == 320 && pillarPos != null) {
                Screenshot.grab(client, true);
                BrickTraceMod.LOGGER.info("BrickTrace: after-removal screenshot at tick {}", worldTicks);
            }
            if (dev && worldTicks == 460) {
                client.player.connection.sendCommand("tp @s 120 80 120");
                BrickTraceMod.LOGGER.info("BrickTrace: teleport command sent for re-capture test");
            }
            if (dev && worldTicks == 600) {
                Screenshot.grab(client, true);
                BrickTraceMod.LOGGER.info("BrickTrace: post-teleport screenshot at tick {}", worldTicks);
            }
            if (dev && worldTicks >= 340 && worldTicks <= 420 && worldTicks % 4 == 0) {
                final int index = (worldTicks - 340) / 4;
                final BlockPos base = client.player.blockPosition()
                        .offset((index % 5) - 2, 1 + index / 5, 5);
                client.level.setBlockAndUpdate(base, Blocks.STONE.defaultBlockState());
            }
            if (dev && !screenshotDone && worldTicks == 140) {
                screenshotDone = true;
                Screenshot.grab(client, true);
                BrickTraceMod.LOGGER.info("BrickTrace: before-edit screenshot at tick {}", worldTicks);
            }
            if (dev && !afterScreenshotDone && worldTicks == 220) {
                afterScreenshotDone = true;
                Screenshot.grab(client, true);
                BrickTraceMod.LOGGER.info("BrickTrace: after-edit screenshot at tick {}", worldTicks);
            }
        });
    }

    private static void processDirtySections(Minecraft client) {
        final long[] dirty = BrickTraceDirtySections.drain(16);
        for (long key : dirty) {
            final int sectionX = BrickTraceDirtySections.x(key);
            final int sectionY = BrickTraceDirtySections.y(key);
            final int sectionZ = BrickTraceDirtySections.z(key);

            final ChunkAccess access = client.level.getChunkSource()
                    .getChunk(sectionX, sectionZ, ChunkStatus.FULL, false);
            if (!(access instanceof LevelChunk chunk)) {
                continue;
            }
            final int index = sectionY - client.level.getMinSectionY();
            final LevelChunkSection[] sections = chunk.getSections();
            if (index < 0 || index >= sections.length || sections[index] == null) {
                continue;
            }

            final int[] voxels = BrickTraceWorldCapture.captureSection(sections[index]);
            if (view.updateSection(sectionX * 16, sectionY * 16, sectionZ * 16, voxels)) {
                BrickTraceMod.LOGGER.debug("BrickTrace: section update at {} {} {}", sectionX,
                        sectionY, sectionZ);
            }
        }
    }
}
