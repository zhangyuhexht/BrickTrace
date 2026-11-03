package dev.bricktrace;

import dev.bricktrace.nativebridge.NativeBridge;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.loader.api.FabricLoader;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.nio.file.Path;

public final class BrickTraceMod implements ModInitializer {
    public static final String MOD_ID = "bricktrace";
    public static final Logger LOGGER = LoggerFactory.getLogger(MOD_ID);

    private static boolean nativeAvailable;

    public static boolean nativeAvailable() {
        return nativeAvailable;
    }

    @Override
    public void onInitialize() {
        LOGGER.info("BrickTrace {} initializing", BrickTraceVersion.VERSION);

        final Path nativesDir = FabricLoader.getInstance().getConfigDir().resolve("bricktrace/natives");
        nativeAvailable = NativeBridge.load(nativesDir);
        if (nativeAvailable) {
            LOGGER.info("BrickTrace native device info:\n{}", NativeBridge.nativeDeviceInfo().trim());
        } else {
            LOGGER.error("BrickTrace native library unavailable", NativeBridge.loadError());
        }
    }
}
