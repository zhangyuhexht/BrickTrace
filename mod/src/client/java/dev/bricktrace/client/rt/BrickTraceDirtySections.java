package dev.bricktrace.client.rt;

import it.unimi.dsi.fastutil.longs.LongOpenHashSet;

/** Section coordinates whose block data changed and must be re-uploaded to the renderer. */
public final class BrickTraceDirtySections {
    private static final LongOpenHashSet DIRTY = new LongOpenHashSet();

    private BrickTraceDirtySections() {
    }

    public static synchronized void mark(int sectionX, int sectionY, int sectionZ) {
        DIRTY.add(pack(sectionX, sectionY, sectionZ));
    }

    public static synchronized long[] drain(int max) {
        final int count = Math.min(max, DIRTY.size());
        if (count == 0) {
            return new long[0];
        }
        final long[] keys = new long[count];
        final var iterator = DIRTY.iterator();
        for (int i = 0; i < count; ++i) {
            keys[i] = iterator.nextLong();
        }
        for (long key : keys) {
            DIRTY.remove(key);
        }
        return keys;
    }

    public static int x(long key) {
        return (int) (key >> 42);
    }

    public static int y(long key) {
        return (int) ((key << 22) >> 44);
    }

    public static int z(long key) {
        return (int) ((key << 44) >> 44);
    }

    private static long pack(int x, int y, int z) {
        return ((long) (x & 0x3FFFFF) << 42) | ((long) (y & 0xFFFFF) << 22) | (z & 0x3FFFFF);
    }
}
