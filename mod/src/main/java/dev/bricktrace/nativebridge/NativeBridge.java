package dev.bricktrace.nativebridge;

import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

public final class NativeBridge {
    private static final String LIBRARY_NAME = "bricktrace_jni.dll";
    private static final String[] SHADERS = {
            "voxel.rgen.spv",
            "voxel.rmiss.spv",
            "shadow.rmiss.spv",
            "voxel.rchit.spv",
            "shadow.rahit.spv",
            "voxel.rint.spv",
    };

    private static boolean loaded;
    private static Throwable loadError;
    private static Path shaderDir;

    private NativeBridge() {
    }

    public static synchronized boolean load(Path directory) {
        if (loaded) {
            return true;
        }
        if (loadError != null) {
            return false;
        }
        try {
            Files.createDirectories(directory);
            final Path library = directory.resolve(LIBRARY_NAME);
            extract("/natives/windows-x64/" + LIBRARY_NAME, library);

            shaderDir = directory.resolve("shaders");
            Files.createDirectories(shaderDir);
            for (String shader : SHADERS) {
                extract("/shaders/" + shader, shaderDir.resolve(shader));
            }

            System.load(library.toAbsolutePath().toString());
            loaded = true;
            return true;
        } catch (Throwable error) {
            loadError = error;
            return false;
        }
    }

    private static void extract(String resource, Path target) throws Exception {
        try (InputStream in = NativeBridge.class.getResourceAsStream(resource)) {
            if (in == null) {
                throw new IllegalStateException("missing bundled resource " + resource);
            }
            Files.copy(in, target, StandardCopyOption.REPLACE_EXISTING);
        }
    }

    public static Throwable loadError() {
        return loadError;
    }

    public static String shaderDir() {
        return shaderDir != null ? shaderDir.toAbsolutePath().toString() : "";
    }

    public static native String nativeDeviceInfo();

    public static native boolean nativeRenderTestFrame(String outputPath, String shaderDir,
                                                       int width, int height);
}
