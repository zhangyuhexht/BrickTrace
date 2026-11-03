# BrickTrace 路线图

> 目标机器：RTX 5060 8GB
> 目标：1080p / 8~12 区块 / DLSS / 可玩
> 总周期：2~3 人 × 5~6 个月；单人 9~12 个月

---

## 1. 阶段总览

| 阶段 | 周期 | 交付 | 验收 | 机器 |
|---|---|---|---|---|
| **P0 离线原型** | 2 周 | 256³ 世界导出；CPU/GPU brick DDA 出图 | 几何正确、无漏面 | 1660S 可做 CPU；5060 做 GPU |
| **P1 设备与管线** | 2 周 | Fabric mod + 官方 VkDevice + RT 三角形 | RenderDoc 可见 RT pass | 5060 |
| **P2 地形阴影** | 4 周 | brick BLAS + TLAS + shadow ray | 1080p / 8 区块 / ≥30fps 无降噪 | 5060 |
| **P3 GI/反射** | 4 周 | G-buffer + RT reflection + 1~2 bounce + NRD | 无闪烁；水/玻璃正确 | 5060 |
| **P4 动态/实体** | 3 周 | 脏队列 + 实体 BLAS + 透明 | 方块更新 0 次 BLAS 重建 | 5060 |
| **P5 上采样/合成** | 3 周 | DLSS/FSR/XeSS + HDR + UI | 1080p 60（DLSS Performance） | 5060 |
| **P6 优化/发布** | 4 周 | 多 GPU 测试、文档、demo、MIT 发布 | 稳定、可复现 | 5060 + 其他 |

---

## 2. M1：前 14 天日计划

| 天 | 任务 | 产出 | 验收 |
|---|---|---|---|
| 1 | 环境检查、仓库初始化 | `check-env.ps1` 输出、git repo | RT 扩展可见 |
| 2 | Gradle + CMake + shader 编译跑通 | 空 mod + 空 native lib | `build` 成功 |
| 3 | Fabric mod 在 26.3 加载 | 日志打印版本 | 无崩溃 |
| 4 | Mixin 拿相机/可见 section | `RenderBridge` 日志 | 数据正确 |
| 5 | Native lib 加载、枚举设备 | `vkEnumeratePhysicalDevices` | 找到 5060 |
| 6 | **设备访问探针**：拿官方 `VkDevice` | 成功/失败报告 | 决策门 |
| 7 | RT 三角形（离屏） | 图像 readback | 三角形正确 |
| 8 | RT 三角形上屏 / present | 游戏内 overlay | 稳定 |
| 9 | 世界导出工具：256³ region | `.btworld` 文件 | 可复现 |
| 10 | CPU brick DDA 渲染 | PNG | 几何正确 |
| 11 | GPU compute DDA（可选） | GPU 图像 | 与 CPU 一致 |
| 12 | RT AABB 微基准 | 数字 | 与 compute DDA 对比 |
| 13 | BLAS/TLAS + shadow ray | 游戏内阴影 | 8 区块可用 |
| 14 | 基线与报告 | `docs/report-01.md` | Go/No-Go |

---

## 3. Go / No-Go

### M1 必须全部通过

1. `vulkaninfo` 能看到 RT 扩展。
2. Fabric mod 在 26.3 加载无崩溃。
3. 拿到官方 `VkDevice` / `VkQueue`，或明确记录替代方案。
4. RT 三角形稳定上屏，RenderDoc 能抓到。
5. 离线 brick DDA 图像正确。
6. 5060 上 RT 阴影原型：1080p、8 区块、无降噪 ≥ 30fps。

### 任何一条失败

- 停下来，写清楚原因、复现、已尝试方案。
- 不要偷偷换架构（例如直接退回 triangle BVH）。
- 由项目 owner 决定是否改路线。

---

## 4. 性能目标（RTX 5060 8GB）

| 场景 | 分辨率 | 区块 | 目标 |
|---|---|---|---|
| RT 阴影，无降噪 | 1080p | 8 | ≥ 60fps |
| RT 阴影 + 1 bounce GI + NRD | 1080p | 8 | ≥ 60fps |
| 同上 + DLSS Quality | 1080p | 12 | ≥ 60fps |
| 同上 + DLSS Performance | 1080p | 12 | ≥ 90fps |
| VRAM | - | 12 | < 7GB |

> 5060 是 8GB 卡，**不要用 1440p / 4K 做验收**。

---

## 5. 每阶段 Definition of Done

### P0

- 256³ 世界可导出、可重放。
- CPU DDA 与参考图一致。
- GPU DDA（如做）误差在容忍范围。

### P1

- 官方 device 或 fallback 明确。
- RT pipeline 创建/销毁无 validation error。
- 三角形在游戏内可见。

### P2

- BLAS 静态、TLAS 每帧。
- 阴影无 acne / peter-panning。
- 8 区块 1080p ≥ 30fps。

### P3

- 反射/GI 无严重噪点。
- 水/玻璃/树叶可接受。
- NRD 接入。

### P4

- 连续放置/破坏 1000 次方块无崩溃。
- 区块加载/卸载无泄漏。
- 0 次 BLAS 重建（用日志验证）。

### P5

- DLSS/FSR/XeSS 可切换。
- UI 无重影。
- HDR 可开关。

### P6

- 3 张不同 GPU 测试通过。
- 文档 + demo + 可复现 benchmark。
- MIT/Apache 发布。

---

## 6. 风险登记

| 风险 | 阶段 | 触发条件 | 对策 |
|---|---|---|---|
| 官方 VkDevice 拿不到 | P1 | 探针失败 | 独立 device + external memory；再不行 fork |
| AABB 性能差 | P2 | < 30fps | brick 尺寸调整；triangle fallback |
| 非方块几何 | P3 | 视觉错误 | 二期；先 cube |
| DLSS 许可 | P5 | 分发问题 | 用户自备 DLL；FSR/XeSS |
| 26.4 改动 | P6 | hook 失效 | 抽象层 + CI |
| 8GB VRAM 不够 | P5 | OOM | 降区块/降分辨率/压缩 G-buffer |

---

## 7. 对外发布节奏

- M1 后：技术博客《Static Brick BVH for Minecraft Ray Tracing》+ 三角形/阴影截图。
- P2 后：第一个可玩 demo（8 区块、阴影）。
- P3 后：GI/反射 demo。
- P6：开源 release + benchmark + 视频。
