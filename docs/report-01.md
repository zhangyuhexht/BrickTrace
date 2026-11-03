# Report 01 — M1（Hello RT + Brick DDA）

## 0. 摘要

| 项 | 结果 |
|---|---|
| 日期 | 2026-11-03 |
| 机器 / GPU | RTX 5060 Laptop GPU（8151 MiB），驱动 621.01 |
| 驱动 / Vulkan | Vulkan 实例 1.4.357，设备 API 1.4.357 |
| Commit | 见仓库 `main` 分支最新提交 |
| 官方 VkDevice | ❌ 未接入（见第 2 节） |
| RT 三角形 | ✅ 超出目标：RT 体素 brick DDA + 阴影，离屏与游戏内自检均出图 |
| Brick DDA | ✅ GPU 出图，几何正确 |
| 游戏内加载 | ✅ Fabric 26.3 + Mixin + JNI 全链路通过 |
| Go/No-Go | 有条件 Go（见第 7 节） |

一句话结论：**静态 brick BVH + 动态体素 payload 这条路在 5060 上成立**——1080p 全屏主射线 + 阴影射线 0.96 ms，947 个 section、50,217 个 brick，BLAS/TLAS 仅 3.5 MiB；剩下的关键风险是接入游戏官方 `VkDevice` 与上屏。

---

## 1. 环境

```
Windows 11 家庭版 26200
NVIDIA GeForce RTX 5060 Laptop GPU  8151 MiB  driver 621.01
Vulkan instance 1.4.357
VK_KHR_acceleration_structure / ray_tracing_pipeline / ray_query 仅 RTX 5060 暴露
RAM 31.7 GB
JDK 25.0.5 (Temurin)
CMake 4.4.2 / MSVC 14.51 (VS 2026 Insiders) / Git 2.54
```

工具链是组件化拼装的（LunarG 官方安装包在本网络下不可下载）：

| 组件 | 版本 | 来源 |
|---|---|---|
| Vulkan headers | 1.4.363 | KhronosGroup/Vulkan-Headers |
| `vulkan-1.lib` | 1.4.357 | 从系统 `vulkan-1.dll` 的导出表生成 |
| glslang | 16.6.0 | KhronosGroup/glslang release |
| Gradle | 9.8.2 | Huawei 镜像 |

**重要发现**：Vulkan 1.4 loader 的 `vulkan-1.dll` 只导出 core/WSI 入口（265 个），RT 入口必须运行时用 `vkGetDeviceProcAddr` 解析，不能静态链接。渲染器已按此实现。

---

## 2. 设备访问（最大风险）

- 独立 `VkDevice`：✅ 可用，RT 扩展与特性全部启用成功
- 官方 `VkDevice`（Minecraft 自己的 device）：❌ **尚未尝试接入**
- 获取方式规划：hook 26.3 Vulkan 后端的 device 创建点，或在 native 侧用 `VK_KHR_external_memory_win32` 共享资源
- 相关代码：`native/src/core/device_probe.cpp`、`native/src/renderer/rt_renderer.cpp`

这是目前最大的未决问题。M1 的其余部分不依赖它，因此先完成离线渲染与 mod 集成。

---

## 3. RT 管线

- 扩展：`VK_KHR_acceleration_structure`、`VK_KHR_ray_tracing_pipeline`、`VK_KHR_deferred_host_operations`、`VK_KHR_buffer_device_address`（1.2 core）
- Pipeline：raygen + 2 miss（天空 / 阴影）+ procedural hit group（closest hit + any hit + intersection）
- BLAS：每个非空 section 一个，primitive = 该 section 内非空 brick 的 4³ AABB，`PREFER_FAST_TRACE`
- TLAS：每个 section 一个实例，identity transform（AABB 已是世界坐标），`instanceCustomIndex` = section slot
- Intersection shader：Amanatides & Woo DDA，brick 内 4³，occupancy 位掩码提前退出，命中写 material / 光照 / 面法线
- 阴影：raygen 内第二条 `traceRay`，`TerminateOnFirstHit` + any-hit 置 0

实现过程中修掉的三个真实 bug（都记录在报告中，避免复现）：

1. **section → brick 体素抽取**：section 是 16³ 行主序，brick 的 64 个体素并不连续；必须按 `(y,z,x)` 子块重排，否则画面是随机噪点。
2. **primitive 序号 vs brick 序号**：GPU 侧的 occupancy / voxel 数组按“占用 brick 的顺序”紧凑存放，shader 却用原始 brick 编号索引，导致跨 section 读错数据。
3. **DDA 起点**：intersection shader 的 `gl_ObjectRayOriginEXT` 是射线原点而不是 brick 入射点，直接用会把射线在一个错误的 cell 上开始 DDA，产生大量错误法线。必须以 slab 求交得到入口点后再开始。

---

## 4. 离线 Brick DDA
- 世界：256×128×256（16×8×16 = 2048 sections），程序化地形，947 个非空 section，50,217 个 brick
- 生成耗时：14 ms；上传 + BLAS/TLAS 构建：203 ms
- 正确性检查：
  - CPU 侧 topmap（每列最高方块材质）全为草方块，与 GPU 俯视渲染一致
  - 俯视法线图几乎全为 +Y，只有台阶边缘出现侧向法线 → 无漏面、无法线错误
  - 透视渲染中地形连续、阴影方向与太阳一致
- 图像：`docs/images/rt-1080p.png`

---

### 4.5 游戏内验证

Fabric 客户端 GameTest 自动建世界（26.3 / loader 0.20.1 / Fabric API 0.165.2），完整日志：

```
[bricktrace] BrickTrace 0.1.0 initializing
[bricktrace] BrickTrace native device info:
NVIDIA GeForce RTX 5060 Laptop GPU api=1.4.357 AS=1 RT=1 RQ=1 vram=7.7GiB
[bricktrace] BrickTrace frame: camera=(0.50, -58.38, 0.50) rot=(0.0, 0.0) fov=70.0 visibleSections=53
[bricktrace] BrickTrace offscreen RT test frame -> ...\bricktrace_test_frame.png (ok)
```

- Mixin 成功注入 `LevelRenderer.render`，每帧路径上每秒输出相机、FOV、可见 section 数
- JNI 在游戏进程内创建独立 Vulkan device，渲染 1280×720 一帧并落盘（图见 `docs/images/rt-1080p.png` 同款场景）
- 注意：dev 客户端默认图形后端是 **OpenGL**。26.3 自带 Vulkan 后端（`com.mojang.renderpearl.backend.vulkan`，选项 `preferredGraphicsBackend`），已验证设为 `"vulkan"` 后游戏正常启动：

```
Using graphics backend Vulkan, using drivers: 1.4.357 NVIDIA 621.01
Using graphics device: NVIDIA GeForce RTX 5060 Laptop GPU (NVIDIA)
```

但 Minecraft 创建 device 时启用的扩展只有 swapchain / dynamic rendering / push descriptor 等，**不含任何 RT 扩展**。因此“复用官方 device”不能直接成立，需要在 device 创建路径注入 RT 扩展与 feature 链，或退回独立 device + external memory —— 这是下一阶段的第一个决策点。

官方 device 的访问入口已经确认存在：

```
com.mojang.renderpearl.backend.vulkan.VulkanDevice
  VkDevice vkDevice()
  VulkanQueue graphicsQueue()
  long vma()
com.mojang.renderpearl.backend.vulkan.VulkanPhysicalDevice
  VkPhysicalDevice vkPhysicalDevice()
```

---

## 5. 基准（RTX 5060 Laptop，1080p）

| 场景 | 分辨率 | sections / bricks | GPU 帧时间 | FPS | 显存（BLAS+TLAS） |
|---|---|---|---|---|---|
| 主射线 + 阴影 | 1920×1080 | 947 / 50217 | **0.96 ms** | ≈1045 | 3.5 MiB |
| 同上 720p（640×360 采样） | 640×360 | 947 / 50217 | 0.16 ms | ≈6300 | 3.5 MiB |

说明：

- 计时用 `vkCmdWriteTimestamp` 包住 `vkCmdTraceRaysKHR`，不含 readback
- 基准程序每帧把图像拷回 CPU（23.9 ms），这是测试工具行为，不是渲染开销
- 目标场景（8 区块）比这里的 16×16 区块更小，性能余量充足

---

## 6. 阻塞点

| 问题 | 复现 | 已尝试 | 需要 |
|---|---|---|---|
| 官方 `VkDevice` 未接入 | 游戏内 RT 上屏 | 暂无 | 26.3 Vulkan 后端 hook 点调研 |
| RT 图像上屏未实现 | — | — | swapchain / 纹理共享方案 |
| LunarG 官方 SDK 不可下载 | `sdk.lunarg.com` 5 KB/s、连接重置 | 组件化拼装、GitHub 代理 | 如需要完整 SDK，需可用代理 |
| VS 2026 Insiders 预览版 11-15 到期 | — | — | 届时安装 VS 2022 Build Tools |

---

## 7. 决策

- [x] 有条件 Go：brick BVH 方案性能与正确性成立，进入 P2
- [ ] No-Go
- [ ] 改路线

条件：官方 `VkDevice` 接入必须在 P2 前明确；否则按 HANDOFF 的降级方案走独立 device + 外部内存。

---

## 8. 下一步（24 小时）

1. 摸清 26.3 Vulkan 后端暴露 device 的入口，完成设备访问探针
2. 打通 RT 图像上屏（先做离屏纹理 + 原版 UI 叠加）
3. 用真实区块快照替换程序化世界，验证脏区块更新路径 0 次 BLAS 重建
