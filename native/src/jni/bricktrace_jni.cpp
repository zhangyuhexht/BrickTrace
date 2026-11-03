#include <jni.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <string>
#include <vector>

#include "bricktrace/core/device_probe.hpp"
#include "bricktrace/renderer/rt_renderer.hpp"
#include "bricktrace/world/voxel_world.hpp"

namespace {

std::string toUtf(JNIEnv* env, jstring value) {
    if (value == nullptr) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result = chars != nullptr ? chars : "";
    if (chars != nullptr) env->ReleaseStringUTFChars(value, chars);
    return result;
}

jstring fromUtf(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeDeviceInfo(
    JNIEnv* env, jclass) {
    const auto devices = bt::enumerateDevices();
    if (devices.empty()) {
        return fromUtf(env, "no Vulkan device");
    }

    std::string result;
    char line[512];
    for (const auto& device : devices) {
        std::snprintf(line, sizeof(line),
                      "%s api=%u.%u.%u AS=%d RT=%d RQ=%d vram=%.1fGiB\n", device.name.c_str(),
                      VK_VERSION_MAJOR(device.apiVersion), VK_VERSION_MINOR(device.apiVersion),
                      VK_VERSION_PATCH(device.apiVersion), (int)device.accelerationStructure,
                      (int)device.rayTracingPipeline, (int)device.rayQuery,
                      device.deviceLocalMemory / 1073741824.0);
        result += line;
    }
    return fromUtf(env, result);
}

JNIEXPORT jboolean JNICALL
Java_dev_bricktrace_nativebridge_NativeBridge_nativeRenderTestFrame(JNIEnv* env, jclass,
                                                                     jstring outputPath,
                                                                     jstring shaderDir,
                                                                     jint width, jint height) {
    const std::string path = toUtf(env, outputPath);
    const bt::WorldData world = bt::generateDemoWorld(256, 128, 256, 0x62726963u);

    bt::RendererConfig config;
    config.width = static_cast<uint32_t>(width);
    config.height = static_cast<uint32_t>(height);
    config.shaderDir = toUtf(env, shaderDir);

    bt::RtRenderer renderer(config);
    if (!renderer.valid()) {
        return JNI_FALSE;
    }
    if (!renderer.uploadWorld(world)) {
        return JNI_FALSE;
    }

    bt::Camera camera;
    camera.position[0] = 128.0f;
    camera.position[1] = 96.0f;
    camera.position[2] = 40.0f;
    camera.pitch = -0.22f;

    for (int i = 0; i < 3; ++i) {
        if (!renderer.renderFrame(camera)) {
            return JNI_FALSE;
        }
    }
    return renderer.savePng(path) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
