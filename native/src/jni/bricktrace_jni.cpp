#include <jni.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
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

struct GameDevice {
    uint64_t device = 0;
    uint64_t physicalDevice = 0;
    uint64_t queue = 0;
    uint32_t queueFamily = 0;
};

GameDevice gameDevice;

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

JNIEXPORT void JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeAttachGameDevice(
    JNIEnv*, jclass, jlong device, jlong physicalDevice, jlong queue, jint queueFamily) {
    gameDevice.device = static_cast<uint64_t>(device);
    gameDevice.physicalDevice = static_cast<uint64_t>(physicalDevice);
    gameDevice.queue = static_cast<uint64_t>(queue);
    gameDevice.queueFamily = static_cast<uint32_t>(queueFamily);
}

JNIEXPORT jboolean JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeRenderOnGameDevice(
    JNIEnv* env, jclass, jstring outputPath, jstring shaderDir, jint width, jint height) {
    if (gameDevice.device == 0) {
        return JNI_FALSE;
    }
    const std::string path = toUtf(env, outputPath);
    const bt::WorldData world = bt::generateDemoWorld(256, 128, 256, 0x62726963u);

    bt::RendererConfig config;
    config.width = static_cast<uint32_t>(width);
    config.height = static_cast<uint32_t>(height);
    config.shaderDir = toUtf(env, shaderDir);
    config.externalDevice = gameDevice.device;
    config.externalPhysicalDevice = gameDevice.physicalDevice;
    config.externalQueue = gameDevice.queue;
    config.externalQueueFamily = gameDevice.queueFamily;

    bt::RtRenderer renderer(config);
    if (!renderer.valid()) {
        std::fprintf(stderr, "BrickTrace: external device renderer failed: %s\n",
                     renderer.error().c_str());
        return JNI_FALSE;
    }
    if (!renderer.uploadWorld(world)) {
        std::fprintf(stderr, "BrickTrace: upload on game device failed: %s\n",
                     renderer.error().c_str());
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

JNIEXPORT jlong JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeCreateRenderer(
    JNIEnv* env, jclass, jstring shaderDir, jint width, jint height) {
    bt::RendererConfig config;
    config.width = static_cast<uint32_t>(width);
    config.height = static_cast<uint32_t>(height);
    config.shaderDir = toUtf(env, shaderDir);
    if (gameDevice.device != 0) {
        config.externalDevice = gameDevice.device;
        config.externalPhysicalDevice = gameDevice.physicalDevice;
        config.externalQueue = gameDevice.queue;
        config.externalQueueFamily = gameDevice.queueFamily;
    }

    auto* renderer = new bt::RtRenderer(config);
    if (!renderer->valid()) {
        std::fprintf(stderr, "BrickTrace: renderer init failed: %s\n", renderer->error().c_str());
        delete renderer;
        return 0;
    }
    return reinterpret_cast<jlong>(renderer);
}

JNIEXPORT jboolean JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeUploadDemoWorld(
    JNIEnv*, jclass, jlong handle) {
    auto* renderer = reinterpret_cast<bt::RtRenderer*>(handle);
    if (renderer == nullptr) return JNI_FALSE;
    const bt::WorldData world = bt::generateDemoWorld(256, 128, 256, 0x62726963u);
    return renderer->uploadWorld(world) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeRenderFrame(
    JNIEnv*, jclass, jlong handle, jfloat x, jfloat y, jfloat z, jfloat yawRadians,
    jfloat pitchRadians, jfloat fovDegrees) {
    auto* renderer = reinterpret_cast<bt::RtRenderer*>(handle);
    if (renderer == nullptr) return JNI_FALSE;
    bt::Camera camera;
    camera.position[0] = x;
    camera.position[1] = y;
    camera.position[2] = z;
    camera.yaw = yawRadians;
    camera.pitch = pitchRadians;
    camera.fovDegrees = fovDegrees;
    return renderer->renderFrame(camera) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeCopyPixels(
    JNIEnv* env, jclass, jlong handle, jobject buffer) {
    auto* renderer = reinterpret_cast<bt::RtRenderer*>(handle);
    if (renderer == nullptr || buffer == nullptr) return JNI_FALSE;
    void* destination = env->GetDirectBufferAddress(buffer);
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    const auto& pixels = renderer->pixels();
    if (destination == nullptr || capacity < static_cast<jlong>(pixels.size())) {
        return JNI_FALSE;
    }
    std::memcpy(destination, pixels.data(), pixels.size());
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_dev_bricktrace_nativebridge_NativeBridge_nativeDestroyRenderer(
    JNIEnv*, jclass, jlong handle) {
    delete reinterpret_cast<bt::RtRenderer*>(handle);
}

}  // extern "C"
