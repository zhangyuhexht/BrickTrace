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
            final Path runDir = directory.resolve("run-" + ProcessHandle.current().pid() + "-"
                    + System.nanoTime());
            Files.createDirectories(runDir);
            final Path library = runDir.resolve(LIBRARY_NAME);
            extract("/natives/windows-x64/" + LIBRARY_NAME, library);

            shaderDir = runDir.resolve("shaders");
            Files.createDirectories(shaderDir);
            for (String shader : SHADERS) {
                extract("/shaders/" + shader, shaderDir.resolve(shader));
            }

            library.toFile().deleteOnExit();
            for (String shader : SHADERS) {
                shaderDir.resolve(shader).toFile().deleteOnExit();
            }
            runDir.toFile().deleteOnExit();

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

    public static native void nativeAttachGameDevice(long device, long physicalDevice, long queue,
                                                     int queueFamily);

    public static native boolean nativeRenderOnGameDevice(String outputPath, String shaderDir,
                                                         int width, int height);

    public static native long nativeCreateRenderer(String shaderDir, int width, int height);

    public static native boolean nativeUploadWorld(long handle, int[] origins, int[] voxels);

    public static native boolean nativeUploadDemoWorld(long handle);

    public static native boolean nativeRenderFrame(long handle, float x, float y, float z,
                                                   float yawRadians, float pitchRadians,
                                                   float fovDegrees);

    public static native boolean nativeCopyPixels(long handle, java.nio.ByteBuffer buffer);

    public static native void nativeDestroyRenderer(long handle);
}
