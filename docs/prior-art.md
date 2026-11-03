# Prior Art 与新颖性声明

> 目的：避免“重复造轮子”和“以为自己是第一个”。写清楚别人做了什么、我们新在哪。

---

## 1. 现有方案

| 项目 | 版本/状态 | 许可 | 加速结构 | 集成方式 | 关键限制 |
|---|---|---|---|---|---|
| **Radiance** | 1.20.1 / 1.21.1 / 1.21.4；26.x 移植中 | GPL-3.0 | 三角形 BVH + KHR RT pipeline | 替换/接管世界渲染 | 26.3 尚未完成；方块变化重建 BVH |
| **Radiante** | 26.3 / 26.2 / 26.4 snapshot；Fabric/NeoForge/Forge | GPL-3.0（Radiance fork） | 三角形 BVH + KHR RT pipeline | 跑在官方 Vulkan device 上 | 单人维护；AMD/Intel 测试不足；FG 在 Forge 未测 |
| **Lumen (AetherRT)** | 26.3 Fabric；0.1.3-alpha | **ARR 闭源** | 未公开（Vulkan RT） | 官方 Vulkan 后端 | 闭源；NVIDIA only；Alpha |
| **RTX Remix 1.6** | 1.21.1 beta | NVIDIA 专有 | DXR + BVH | 外挂 | 版本锁死；不是 mod 生态 |
| **Bedrock RTX** | Bedrock | 专有 | DXR + BVH | 官方引擎 | 不同版本 |
| **SEUS PTGI / Continuum RT** | 光影包 | 各自许可 | 屏幕空间/体素混合 | 光影包 | 不是硬件 RT |
| **Rethinking Voxels** | Iris 光影 | 各自许可 | 体素光照 | 光影包 | 不是硬件 RT |

---

## 2. 我们的差异

### 2.1 核心 claim

> **BrickTrace 是一个开源的 Minecraft Java 26.3 硬件光追渲染器：它用静态 brick AABB BVH 让 RT 核心做粗粒度遍历，用 intersection shader 在 brick 内做体素 DDA，方块变化只更新体素 buffer，不重建任何加速结构。**

### 2.2 和 Radiance / Radiante 的区别

| 维度 | Radiance / Radiante | BrickTrace |
|---|---|---|
| 地形表示 | 三角形 | 体素 brick |
| BLAS 内容 | 三角形 | AABB primitive（4³ brick） |
| 求交 | 三角形求交 | RT 粗遍历 + DDA |
| 方块变化 | 重建/refit 受影响 BLAS | **只改 buffer，0 次 BLAS 操作** |
| 内存 | 三角形 BVH 较大 | brick 数据 + 小 BVH |
| 更新延迟 | 毫秒~几十毫秒 | 微秒~亚毫秒 |
| 新颖点 | 完整 path tracer | **静态 BVH + 动态 payload** |

### 2.3 和 Lumen 的区别

- Lumen 闭源（ARR），无法确认内部结构。
- 我们只做黑盒行为对照：画质、性能、兼容性。
- **不反编译、不抄代码、不复制资源。**

### 2.4 和 UE Lumen 的区别

- UE Lumen 是引擎内 GI，不是给外部游戏加 RT 的中间件。
- 我们不做 UE 客户端；原因见 `HANDOFF.md`。

---

## 3. 不声称的东西

- 不声称“第一个 Minecraft 光追”（Radiance/Bedrock/RTX Remix 都做过）。
- 不声称“第一个体素光追”（Teardown、学术工作、shader 都有）。
- 不声称“最快”（没有跨项目基准）。
- 只声称：**在 26.3 Java + 官方 Vulkan 上，用静态 brick BVH + 动态体素 payload 的开源实现**。

---

## 4. Prior-art 检查清单

在正式写代码前，至少检索：

- GitHub：`Minecraft ray tracing`、`Minecraft path tracer`、`MCVR`、`Radiance`、`Radiante`、`AetherRT`、`Lumen RT`
- GitHub：`voxel ray tracing`、`sparse voxel DAG`、`brick map`
- 学术：voxel ray tracing、sparse voxel hierarchy、DDA、ReSTIR、NRD
- 社区：r/technicalminecraft、r/minecraftshaders、Radiance/Radiante Discord
- 专利：体素加速结构、RT 更新策略（可选）

把结果补充到本文，避免重复。

---

## 5. 参考链接

- Radiance: https://github.com/Minecraft-Radiance/Radiance
- Radiante: https://github.com/Gabrieli2806/Radiante
- Lumen RT: https://www.curseforge.com/minecraft/mc-mods/lumenrt
- Vulkan RT: https://www.khronos.org/registry/vulkan/
- NRD: https://github.com/NVIDIA-RTX/NRD
- ReSTIR: https://github.com/DAVIS-ETC/restir
