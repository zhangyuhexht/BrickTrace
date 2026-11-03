#include "bricktrace/renderer/rt_renderer.hpp"
#include "bricktrace/util/png_writer.hpp"
#include "bricktrace/world/voxel_world.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    std::string shaderDir = "shaders";
    std::string outputDir = ".";
    int frames = 60;
    uint32_t width = 1920;
    uint32_t height = 1080;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shaders") == 0 && i + 1 < argc) {
            shaderDir = argv[++i];
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
    }

    const auto worldStart = std::chrono::steady_clock::now();
    const bt::WorldData world = bt::generateDemoWorld(256, 128, 256, 0x62726963u);
    const auto worldEnd = std::chrono::steady_clock::now();

    bool topMap = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--topmap") == 0) topMap = true;
    }
    if (topMap) {
        std::vector<uint8_t> image(static_cast<size_t>(world.size[0]) * world.size[2] * 4, 0);
        for (int z = 0; z < world.size[2]; ++z) {
            for (int x = 0; x < world.size[0]; ++x) {
                const int sx = x / bt::kSectionSize;
                const int sz = z / bt::kSectionSize;
                int topY = -1;
                uint32_t topMaterial = 0;
                for (int sy = world.sectionCount[1] - 1; sy >= 0 && topY < 0; --sy) {
                    const bt::SectionData& section =
                        world.sections[static_cast<size_t>(sectionIndex(world, sx, sy, sz))];
                    for (int ly = bt::kSectionSize - 1; ly >= 0; --ly) {
                        const uint32_t v = section.voxels[
                            ((ly * bt::kSectionSize) + (z % bt::kSectionSize)) * bt::kSectionSize +
                            (x % bt::kSectionSize)];
                        if ((v & bt::kMaterialMask) != 0) {
                            topY = section.origin[1] + ly;
                            topMaterial = v & bt::kMaterialMask;
                            break;
                        }
                    }
                }
                uint8_t r = 0, g = 0, b = 0;
                if (topMaterial == 1) { r = 132; g = 132; b = 138; }
                else if (topMaterial == 2) { r = 120; g = 84; b = 59; }
                else if (topMaterial == 3) { r = 82; g = 148; b = 66; }
                else if (topMaterial == 4) { r = 41; g = 97; b = 158; }
                const size_t offset = (static_cast<size_t>(z) * world.size[0] + x) * 4;
                image[offset + 0] = r;
                image[offset + 1] = g;
                image[offset + 2] = b;
                image[offset + 3] = 255;
            }
        }
        bt::writePng(outputDir + "/topmap_cpu.png", image.data(), world.size[0], world.size[2]);
        std::printf("wrote topmap_cpu.png\n");
        return 0;
    }

    bt::RendererConfig config;
    config.width = width;
    config.height = height;
    config.shaderDir = shaderDir;

    bt::RtRenderer renderer(config);
    if (!renderer.valid()) {
        std::printf("renderer init failed: %s\n", renderer.error().c_str());
        return 1;
    }

    std::printf("device: %s\n", renderer.deviceName().c_str());
    std::printf("world: %dx%dx%d, %zu sections, generated in %.1f ms\n", world.size[0],
                world.size[1], world.size[2], world.sections.size(),
                std::chrono::duration<double, std::milli>(worldEnd - worldStart).count());

    const auto uploadStart = std::chrono::steady_clock::now();
    if (!renderer.uploadWorld(world)) {
        std::printf("upload failed: %s\n", renderer.error().c_str());
        return 1;
    }
    const auto uploadEnd = std::chrono::steady_clock::now();
    const double uploadMs = std::chrono::duration<double, std::milli>(uploadEnd - uploadStart).count();
    std::printf("upload: %u sections, %u bricks, %.1f ms, BLAS+TLAS %.2f MiB\n",
                renderer.renderedSectionCount(), renderer.renderedBrickCount(), uploadMs,
                renderer.blasMemoryBytes() / (1024.0 * 1024.0));

    bt::Camera camera;
    camera.position[0] = 128.0f;
    camera.position[1] = 132.0f;
    camera.position[2] = 20.0f;
    camera.yaw = 0.0f;
    camera.pitch = -0.42f;
    camera.fovDegrees = 70.0f;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cam") == 0 && i + 3 < argc) {
            camera.position[0] = static_cast<float>(std::atof(argv[i + 1]));
            camera.position[1] = static_cast<float>(std::atof(argv[i + 2]));
            camera.position[2] = static_cast<float>(std::atof(argv[i + 3]));
            i += 3;
        } else if (std::strcmp(argv[i], "--pitch") == 0 && i + 1 < argc) {
            camera.pitch = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) {
            camera.yaw = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--fov") == 0 && i + 1 < argc) {
            camera.fovDegrees = static_cast<float>(std::atof(argv[++i]));
        }
    }

    // Warm up.
    for (int i = 0; i < 5; ++i) {
        if (!renderer.renderFrame(camera)) {
            std::printf("render failed: %s\n", renderer.error().c_str());
            return 1;
        }
    }

    double totalGpuMs = 0.0;
    const auto renderStart = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        camera.yaw = static_cast<float>(i) * 0.01f;
        if (!renderer.renderFrame(camera)) {
            std::printf("render failed: %s\n", renderer.error().c_str());
            return 1;
        }
        totalGpuMs += renderer.lastGpuMs();
    }
    const auto renderEnd = std::chrono::steady_clock::now();
    const double wallMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();

    const double avgGpuMs = totalGpuMs / frames;
    std::printf("render: %ux%u, %d frames\n", width, height, frames);
    std::printf("  gpu trace rays: %.3f ms (%.1f fps)\n", avgGpuMs, 1000.0 / avgGpuMs);
    std::printf("  wall per frame (incl. readback): %.3f ms (%.1f fps)\n", wallMs / frames,
                1000.0 / (wallMs / frames));

    const std::string pngPath = outputDir + "/bricktrace_frame.png";
    if (!renderer.savePng(pngPath)) {
        std::printf("failed to write %s\n", pngPath.c_str());
        return 1;
    }
    std::printf("wrote %s\n", pngPath.c_str());
    return 0;
}
