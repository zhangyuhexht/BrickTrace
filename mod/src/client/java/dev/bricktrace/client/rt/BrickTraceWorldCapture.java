package dev.bricktrace.client.rt;

import it.unimi.dsi.fastutil.ints.IntArrayList;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.ChunkAccess;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.chunk.LevelChunkSection;
import net.minecraft.world.level.chunk.status.ChunkStatus;

/** Converts the loaded client chunks around the player into BrickTrace's 16³ section voxel format. */
public final class BrickTraceWorldCapture {
    public static final int RADIUS_CHUNKS = 6;

    public record Snapshot(int[] origins, int[] voxels) {
        public int sectionCount() {
            return origins.length / 3;
        }
    }

    private BrickTraceWorldCapture() {
    }

    public static Snapshot capture(ClientLevel level, int centerChunkX, int centerChunkZ) {
        final IntArrayList origins = new IntArrayList();
        final IntArrayList voxels = new IntArrayList();
        final int minSectionY = level.getMinSectionY();

        for (int cz = centerChunkZ - RADIUS_CHUNKS; cz <= centerChunkZ + RADIUS_CHUNKS; ++cz) {
            for (int cx = centerChunkX - RADIUS_CHUNKS; cx <= centerChunkX + RADIUS_CHUNKS; ++cx) {
                final ChunkAccess access = level.getChunkSource()
                        .getChunk(cx, cz, ChunkStatus.FULL, false);
                if (!(access instanceof LevelChunk chunk)) {
                    continue;
                }

                final LevelChunkSection[] sections = chunk.getSections();
                for (int index = 0; index < sections.length; ++index) {
                    final LevelChunkSection section = sections[index];
                    if (section == null || section.hasOnlyAir()) {
                        continue;
                    }

                    origins.add(cx * 16);
                    origins.add((minSectionY + index) * 16);
                    origins.add(cz * 16);

                    for (int y = 0; y < 16; ++y) {
                        for (int z = 0; z < 16; ++z) {
                            for (int x = 0; x < 16; ++x) {
                                voxels.add(encode(section.getBlockState(x, y, z)));
                            }
                        }
                    }
                }
            }
        }

        return new Snapshot(origins.toIntArray(), voxels.toIntArray());
    }

    private static int encode(BlockState state) {
        if (state.isAir()) {
            return 0;
        }

        final String path = BuiltInRegistries.BLOCK.getKey(state.getBlock()).getPath();
        final int material;
        if (path.contains("water")) {
            material = 4;
        } else if (path.contains("grass_block") || path.equals("grass")) {
            material = 3;
        } else if (path.contains("dirt") || path.contains("podzol") || path.contains("mud")
                || path.contains("farmland")) {
            material = 2;
        } else if (path.contains("leaves")) {
            material = 6;
        } else if (path.contains("log") || path.contains("wood") || path.contains("planks")) {
            material = 7;
        } else if (path.contains("stone") || path.contains("deepslate") || path.contains("andesite")
                || path.contains("granite") || path.contains("diorite") || path.contains("tuff")
                || path.contains("calcite") || path.contains("cobble") || path.contains("ore")
                || path.contains("gravel") || path.contains("sand")) {
            material = 1;
        } else if (path.contains("glass") || path.contains("ice")) {
            material = 8;
        } else {
            material = 5;
        }

        final int skyLight = 15;
        int voxel = material | (skyLight << 20);
        if (material != 4 && material != 8 && material != 6) {
            voxel |= 1 << 24;
        }
        return voxel;
    }
}
