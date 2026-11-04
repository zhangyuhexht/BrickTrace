package dev.bricktrace.client.rt;

import net.fabricmc.loader.api.FabricLoader;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;

/** Persistent BrickTrace settings. */
public final class BrickTraceSettings {
    private static final Path FILE = FabricLoader.getInstance().getConfigDir()
            .resolve("bricktrace/settings.properties");

    public static boolean rayTracing = true;
    public static boolean shadows = true;
    public static int debugMode = 0;  // 0 shaded, 1 shadow term, 2 normals
    public static boolean autoTest = false;

    private BrickTraceSettings() {
    }

    public static synchronized void load() {
        if (!Files.exists(FILE)) {
            return;
        }
        final Properties properties = new Properties();
        try (InputStream in = Files.newInputStream(FILE)) {
            properties.load(in);
        } catch (IOException ignored) {
            return;
        }
        rayTracing = Boolean.parseBoolean(properties.getProperty("rayTracing", "true"));
        shadows = Boolean.parseBoolean(properties.getProperty("shadows", "true"));
        debugMode = Integer.parseInt(properties.getProperty("debugMode", "0"));
        autoTest = Boolean.parseBoolean(properties.getProperty("autoTest", "false"));
    }

    public static synchronized void save() {
        final Properties properties = new Properties();
        properties.setProperty("rayTracing", Boolean.toString(rayTracing));
        properties.setProperty("shadows", Boolean.toString(shadows));
        properties.setProperty("debugMode", Integer.toString(debugMode));
        properties.setProperty("autoTest", Boolean.toString(autoTest));
        try {
            Files.createDirectories(FILE.getParent());
            try (OutputStream out = Files.newOutputStream(FILE)) {
                properties.store(out, "BrickTrace settings");
            }
        } catch (IOException ignored) {
        }
    }
}
