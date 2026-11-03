# BrickTrace 交接文档

> 项目：给 Minecraft Java Edition 26.3 做开源硬件光追
> 目标机器：Windows 11 + RTX 5060 8GB + 最新 NVIDIA 驱动
> 当前机器：GTX 1660 Super（不能测 RT / DLSS，只做 Java / Mixin / 离线工具）
> 文档版本：2026-11-03

---

## 0. TL;DR

- **做什么**：Fabric 注入 + 原生 C++ Vulkan RT 渲染器，在 26.3 官方 Vulkan 后端上跑硬件光追。
- **核心新颖点**：**静态 brick BVH + 动态体素 payload**。RT 核心遍历 4³ brick 的 AABB BVH，intersection shader 在 brick 内做 DDA；方块变化只更新体素 buffer，不重建 BLAS。
- **不做什么**：不重写客户端、不用 Unity/Unreal/Godot、不追求 Fabric 渲染 mod 兼容、不做商业产品。
- **许可**：代码计划 MIT / Apache-2.0。Lumen 是 ARR 闭源，只能黑盒对照；Radiance/Radiante 是 GPL，不能抄代码。
- **第一个里程碑（14 天）**：M1「Hello RT + Brick DDA」——Fabric mod 能加载、拿到 Vulkan 设备、RT 三角形上屏、离线 brick DDA 出图、在 5060 上跑出第一组基准。
- **Go/No-Go**：拿不到官方 `VkDevice`、RT 管线跑不通、brick DDA 性能低于目标 70%，就先停下来解决，不进 P2。

---

## 1. 项目定义

### 1.1 目标

在 **Minecraft Java 26.3 + Fabric** 上，提供一个开源、可复现、有技术新颖性的硬件光追方案。

### 1.2 技术路线（已定）

- 目标版本：**26.3**（协议 777，Java 25，官方 Vulkan 后端）。
- 注入方式：**Fabric Mixin**，只做薄桥接，不碰游戏逻辑。
- 渲染器：**原生 C++20 + Vulkan 1.3 + KHR ray tracing**，独立渲染线程。
- 世界表示：**16³ section → 4³ brick → voxel**。
- 加速结构：**静态 brick AABB BLAS + 每帧 TLAS**。
- 求交：**RT 核心粗遍历 + intersection shader 体素 DDA**。
- 更新：**方块变化只改 voxel buffer，不重建 BLAS**。
- 光照：hybrid 光栅 G-buffer + RT 阴影 / 反射 / 1~2 bounce GI。
- 降噪：NRD；有 G-buffer 后接 DLSS RR。
- 上采样：DLSS SR / FSR / XeSS。
- 合成：v1 保留原版 UI；v2 再接管 swapchain 做 FG。

### 1.3 非目标（明确不做）

- 不做完整 UE / Unity 客户端。
- 不重写世界生成、实体 AI、网络协议。
- 不保证 Sodium / Iris / 光影包兼容。
- 不做 macOS / 移动端。
- 不做商业发行。

---

## 2. 机器分工

| 机器 | 用途 | 能验证 | 不能验证 |
|---|---|---|---|
| GTX 1660 Super | Java / Mixin / 世界导出 / CPU 工具 / 构建脚本 | Fabric 加载、数据提取、离线 DDA、单元测试 | RT、DLSS、FG、VRAM 预算 |
| RTX 5060 8GB | 原生渲染器 / RT / DLSS / 性能 | Vulkan RT、DLSS RR/SR/FG、8GB VRAM 预算 | 多 GPU 矩阵（后续） |

**原则**：1660S 上能写的代码尽量写完，5060 只做必须 GPU 验证的部分，减少来回搬机器。

---

## 3. 竞争与许可（必须知道）

| 项目 | 状态 | 许可 | 关系 |
|---|---|---|---|
| **Radiance** | 1.20.1 / 1.21.1 / 1.21.4，26.x 移植中 | GPL-3.0 | 上游参考；不能抄进 MIT 项目 |
| **Radiante** | 26.3 / 26.2 / 26.4 snapshot，Fabric/NeoForge/Forge | GPL-3.0（Radiance fork） | 直接竞品；不能抄 |
| **Lumen (AetherRT)** | 26.3 Fabric，0.1.3-alpha | **ARR 闭源** | 黑盒基线；不能抄、不能反编译 |
| **RTX Remix 1.6** | 1.21.1 beta | NVIDIA 专有 | 不是 26.3 方案 |
| **Bedrock RTX** | Bedrock | 专有 | 不同版本 |

**结论**：我们只做**架构级参考 + 黑盒行为对照**。代码必须 clean-room。

---

## 4. 技术架构（摘要）

```
Minecraft 26.3 JVM
├─ Fabric Mixin（LevelRenderer / GameRenderer / Window）
├─ WorldSnapshot（chunk / block / light / entity / camera）
├─ 双缓冲快照 + 脏区块队列
└─ JNI
    └─ Native C++ Renderer
        ├─ Vulkan 1.3 + KHR RT + NGX
        ├─ WorldDB：brick pool / material table
        ├─ ASManager：静态 BLAS + 每帧 TLAS
        ├─ RT passes：shadow / reflection / GI
        ├─ NRD / DLSS RR
        ├─ DLSS SR / FSR / XeSS
        └─ Composite / Present
```

详细设计见 `docs/architecture.md`。

---

## 5. 第一个里程碑：M1（14 天）

### 5.1 交付物

1. 仓库骨架可构建：Gradle mod + CMake native + shader 编译。
2. Fabric mod 在 26.3 加载，Mixin 能拿到相机和可见 section。
3. Native lib 能加载，枚举 Vulkan 设备与 RT 扩展。
4. **RT 三角形**能在游戏内/离屏渲染并 present。
5. 离线工具：导出 256³ 世界，CPU brick DDA 渲染出 PNG。
6. 5060 上的第一组基准：compute DDA vs RT AABB。
7. `docs/report-01.md`：结果、截图、数字、阻塞点。

### 5.2 验收标准

- `vulkaninfo` 能看到 `VK_KHR_ray_tracing_pipeline` / `VK_KHR_acceleration_structure`。
- Fabric mod 加载无崩溃，日志能打印相机矩阵和 section 数量。
- 拿到官方 `VkDevice` / `VkQueue`（拿不到要记录原因和替代方案）。
- RT 三角形稳定上屏，RenderDoc 能抓到 RT pass。
- 离线 brick DDA 图像几何正确、无漏光/漏面。
- 5060 上 RT 阴影原型：1080p、8 区块、无降噪 ≥ 30fps。

### 5.3 明确不做（M1 阶段）

- 不做 GI、反射、DLSS、HDR、实体、透明。
- 不做性能优化，只做基线测量。
- 不做多版本兼容。

详细日计划见 `docs/roadmap.md`。

---

## 6. 环境

目标机器需要：

- Windows 11 64-bit
- NVIDIA RTX 5060 8GB，最新 Game Ready / Studio 驱动
- Vulkan SDK 1.4.x（含 RT validation）
- Visual Studio 2022 Build Tools（MSVC v143 + Windows SDK）
- CMake 3.28+
- Git
- JDK 25（Temurin / Microsoft）
- Minecraft 26.3 + Fabric Loader 0.19.3+ + Fabric API
- RenderDoc / Nsight Graphics（可选但强烈建议）

安装与验证步骤见 `docs/environment.md`。
一键检查脚本：`scripts/check-env.ps1`。

---

## 7. 仓库结构

```
BrickTrace/
├─ mod/                 # Fabric mod（Java 25，Gradle）
├─ native/              # C++20，CMake
│  ├─ core/             # device / resource / render graph
│  ├─ world/            # brick / material / AS
│  ├─ passes/           # shadow / reflection / GI / composite
│  ├─ ngx/              # DLSS / Reflex
│  └─ third_party/      # VMA / NRD / shaderc
├─ shaders/             # GLSL → SPIR-V
├─ tools/               # world exporter / material baker
├─ tests/               # golden images / benchmark scenes
├─ docs/                # 架构 / 路线 / 环境 / prior art
└─ scripts/             # check-env.ps1 等
```

---

## 8. 汇报格式

每次汇报必须包含：

1. **做了什么**：对应哪个里程碑/任务。
2. **结果**：截图 / RenderDoc capture / 日志。
3. **数字**：GPU、驱动、分辨率、区块数、帧时间、VRAM、BLAS 构建时间。
4. **失败/阻塞**：具体错误、复现步骤、已尝试方案。
5. **下一步**：24 小时内要做什么。
6. **提交**：commit hash（如果有）。

---

## 9. 关键风险

| 风险 | 阶段 | 对策 |
|---|---|---|
| 拿不到官方 `VkDevice` | M1 | 独立 device + external memory；再不行 fork 渲染器 |
| RTX 5060 只有 8GB VRAM | 全程 | 1080p / 8~12 区块 / DLSS Performance；VRAM 预算 < 7GB |
| AABB + intersection 在 5060 上性能不够 | M1/P2 | brick 尺寸 4³↔8³ 可调；退回 triangle BLAS |
| 非方块几何复杂 | P3 | v1 只做 cube；micro-mesh 二期 |
| DLSS 许可/分发 | P5 | 用户自备 DLL；FSR/XeSS fallback |
| 26.4 版本变动 | P6 | hook 抽象层；CI 多版本 |

---

## 10. 文件清单

| 文件 | 内容 |
|---|---|
| `HANDOFF.md` | 本文件，交接总览 |
| `AGENT_PROMPT.md` | 给另一台机器 agent 的启动提示词 |
| `docs/architecture.md` | 详细技术设计 |
| `docs/roadmap.md` | P0~P6 路线 + 14 天日计划 |
| `docs/environment.md` | 环境安装与验证 |
| `docs/prior-art.md` | 竞品与新颖性声明 |
| `scripts/check-env.ps1` | 一键环境检查 |
| `mods/Lumen-0.1.3-alpha.jar` | 黑盒基线（ARR，勿提交到仓库） |

---

## 11. 一句话

**先把「官方 VkDevice + RT 三角形 + brick DDA」这三件事在 5060 上跑通，再谈 GI 和 DLSS。**
