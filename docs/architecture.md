# BrickTrace 技术设计

> 目标：在 Minecraft Java 26.3 的官方 Vulkan 后端上，用静态 brick BVH + 动态体素 payload 实现硬件光追。
> 本文是实现的唯一技术基线；改动必须更新本文。

---

## 1. 目标与非目标

### 1.1 目标

- 26.3 + Fabric + Windows + NVIDIA RTX（先 5060 8GB）。
- 硬件 RT：阴影、反射、1~2 bounce GI。
- 1080p / 8~12 区块 / DLSS Performance 或 Quality 下可玩。
- 方块变化不重建 BLAS。
- 开源（GPL-3.0）。

### 1.2 非目标

- 不做完整客户端重写。
- 不做 Fabric 渲染 mod 兼容（Sodium/Iris）。
- 不做 macOS / 移动端 / 多版本。
- 不做商业发行。

---

## 2. 进程与线程

```
JVM
├─ Minecraft Render Thread
│   └─ RenderBridge：抓快照、发命令
├─ Minecraft Worker Threads（区块构建等）
└─ Native Render Thread
    ├─ 消费脏区块队列
    ├─ 更新 GPU buffer
    ├─ 构建 TLAS / 提交 RT pass
    └─ Present

可选：Native Upload Thread（staging ring 上传）
```

### 2.1 同步规则

- Java 渲染线程**不等待 GPU**。
- 快照双缓冲：`Snapshot[2]` + atomic index。
- 脏区块队列：MPSC 无锁队列；native 按预算消费（例如每帧最多 512 个 section 更新）。
- 每帧 2~3 个 `FrameContext`，各自有 command pool / fence / semaphore。
- 禁止在渲染路径分配 Java 对象；使用 direct buffer / 对象池。

---

## 3. 模块

| 模块 | 语言 | 职责 |
|---|---|---|
| `mod/` | Java 25 | Mixin、快照、配置、JNI 桥 |
| `native/core` | C++20 | Device、Swapchain、VMA、Descriptor、RenderGraph |
| `native/world` | C++20 | Brick pool、Material table、Section 管理 |
| `native/as` | C++20 | BLAS / TLAS 构建与更新 |
| `native/passes` | C++20 | Shadow / Reflection / GI / Denoise / Composite |
| `native/ngx` | C++20 | DLSS RR/SR/FG、Reflex |
| `shaders/` | GLSL | Raygen / Intersection / ClosestHit / AnyHit / Miss / Compute |
| `tools/` | C++/Python | 世界导出、材质烘焙、截图对比 |

---

## 4. 帧管线

| 阶段 | 队列 | 说明 |
|---|---|---|
| 1. Snapshot | CPU | Java 写入相机/可见 section/实体 |
| 2. Upload | Transfer | 脏 section → brick buffer；材质/纹理按需 |
| 3. TLAS Build | Compute/RT | 可见 section + 实体实例 |
| 4. G-buffer | Graphics | albedo/normal/depth/roughness/motion |
| 5. RT Shadow | Compute/RT | 太阳/天空/方块光阴影 |
| 6. RT Reflection / GI | Compute/RT | 1~2 bounce；ReSTIR 方块光 |
| 7. Denoise | Compute | NRD；有 G-buffer 后 DLSS RR |
| 8. Upscale | Compute | DLSS SR / FSR / XeSS |
| 9. Composite | Graphics | 与 UI 合成、tonemap、HDR |
| 10. Present | Graphics | swapchain；FG/Reflex 时接管 timing |

### 4.1 每帧顺序

1. `beginFrame()`：等待 frame fence，acquire image。
2. `consumeDirty()`：上传脏 section；更新 brick pool。
3. `updateTLAS()`：可见 section + 实体。
4. `render()`：按上表提交 pass。
5. `endFrame()`：submit，present，signal。

---

## 5. 世界表示

### 5.1 结构

```
Section 16³
├─ origin: ivec3
├─ 64 × Brick 4³
│  ├─ occupancy: uint64
│  └─ voxels[64]: uint32
├─ microMeshOffset / microMeshCount
└─ flags / biome
```

### 5.2 Voxel 布局

```
bits  0..15  materialId
bits 16..19  blockLight (0..15)
bits 20..23  skyLight   (0..15)
bits 24..31  flags (opaque / cutout / transparent / liquid / emissive)
```

- 一个 section 的 voxel 数据：`64 × 64 × 4B = 16KB`。
- 空 brick 的 `occupancy == 0`，DDA 直接跳过。
- `materialId == 0` 表示空气。

### 5.3 Material

```cpp
struct Material {
    uint32_t albedo;      // bindless texture index
    uint32_t normal;
    uint32_t roughness;
    uint32_t emissive;
    float baseRoughness;
    float metallic;
    uint32_t flags;
};
```

- 启动时从资源包烘焙：block state → material。
- 方块模型（cube）取平均 albedo / roughness；非 cube 走 micro-mesh。
- 支持 LabPBR（`_n` / `_s`）作为可选增强。

### 5.4 非方块几何

- 楼梯、台阶、栅栏、门、植物：烘焙成 per-section micro-mesh。
- micro-mesh 作为 triangle geometry 放在同一个 BLAS。
- v1 可以先只做 cube，非 cube 用近似 AABB 占位；P3 再补。

### 5.5 实体

- 每个实体模型一个 triangle BLAS；TLAS 实例带 transform。
- 骨骼动画：v1 用静态 pose 或代理盒；v2 再做 per-frame refit。
- 粒子：v1 用 billboard / 代理盒。

---

## 6. 加速结构

### 6.1 BLAS

- 每个 section 一个 BLAS。
- 几何：64 个 AABB primitive（4³ brick），不是三角形。
- 构建：`VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR`。
- 静态：方块变化不重建；section 加载时构建，卸载时销毁。
- 空 section 不建 BLAS。

```cpp
VkAccelerationStructureGeometryAabbsDataKHR aabbs{};
aabbs.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
aabbs.data = ...; // 64 × VkAabbPositionsKHR
```

### 6.2 TLAS

- 每帧重建（或可见集变化时重建）。
- 实例：
  - section 实例：`instanceCustomIndex = sectionSlot`，`primitiveID = brickIndex`。
  - 实体实例：`instanceCustomIndex = entitySlot`。
- 构建：`PREFER_FAST_BUILD`。

### 6.3 更新策略

| 事件 | 动作 |
|---|---|
| 方块变化 | 更新 voxel buffer；occupancy bitmask；**不动 BLAS** |
| section 加载 | 分配 brick pool；建 BLAS；加 TLAS 实例 |
| section 卸载 | 移除 TLAS 实例；释放 BLAS/brick |
| 实体移动 | 更新 TLAS instance transform |
| 实体动画 | v1 忽略；v2 refit BLAS |

### 6.4 需要的 Vulkan 扩展

- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_tracing_pipeline`
- `VK_KHR_ray_query`
- `VK_KHR_deferred_host_operations`
- `VK_KHR_buffer_device_address`
- `VK_EXT_descriptor_indexing`
- `VK_KHR_synchronization2`
- `VK_KHR_timeline_semaphore`
- 可选：`VK_KHR_opacity_micromap`、`VK_NV_ray_tracing_invocation_reorder`、`VK_KHR_fragment_shading_rate`

---

## 7. Intersection Shader

### 7.1 输入

- 世界空间 ray：`rayOrigin` / `rayDirection`。
- `gl_InstanceCustomIndexEXT` → section slot。
- `gl_PrimitiveID` → brick index。
- brick 数据：occupancy + voxels（SSBO / bindless）。

### 7.2 DDA 伪代码

```glsl
void main() {
    Section s = sections[gl_InstanceCustomIndexEXT];
    ivec3 brickOrigin = s.origin + brickOffset(gl_PrimitiveID);
    vec3 ro = rayOrigin;
    vec3 rd = rayDirection;

    ivec3 cell = ivec3(floor(ro)) - brickOrigin;
    ivec3 step = ivec3(sign(rd));
    vec3 inv = 1.0 / rd;
    vec3 tMax = (vec3(cell + max(step, 0)) - (ro - vec3(brickOrigin))) * inv;
    vec3 tDelta = abs(inv);

    for (int i = 0; i < 64; i++) {
        if (any(lessThan(cell, ivec3(0))) || any(greaterThanEqual(cell, ivec3(4))))
            break;

        uint v = voxelAt(s, gl_PrimitiveID, cell);
        if (v != 0u) {
            hitAttributeEXT = packHit(v, faceNormal);
            reportIntersectionEXT(tCurrent, HIT_KIND_VOXEL);
            return;
        }

        // Amanatides & Woo DDA advance
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            cell.x += step.x; tCurrent = tMax.x; tMax.x += tDelta.x;
        } else if (tMax.y < tMax.z) {
            cell.y += step.y; tCurrent = tMax.y; tMax.y += tDelta.y;
        } else {
            cell.z += step.z; tCurrent = tMax.z; tMax.z += tDelta.z;
        }
    }
}
```

### 7.3 命中信息

- `materialId`
- `blockLight` / `skyLight`
- 面法线（由 DDA 穿过的轴决定）
- 可选：局部坐标、mip / LOD

### 7.4 Any-hit

- 树叶/草/藤蔓：随机 alpha 或 OMM。
- 玻璃/水：v1 用 closest-hit + 透明材质；v2 再做 any-hit 折射。

---

## 8. 着色

### 8.1 G-buffer

- albedo（RGB）
- normal（RGB）
- roughness / metallic
- depth
- motion vector
- emissive

### 8.2 直接光

- 太阳方向 + 天空颜色。
- Shadow ray：`rayQuery` 或 RT pipeline。
- 方块光：emissive voxel + ReSTIR / light BVH。

### 8.3 GI

- v1：1~2 bounce，cosine-weighted。
- v2：SHaRC / ReSTIR PT。
- 方块光作为小发光体：重要性采样，避免噪声爆炸。

### 8.4 反射

- 低粗糙度：RT reflection ray。
- 高粗糙度：SSR / probe fallback。
- 水面：反射 + 折射 + 吸收。

### 8.5 透明

- 水、玻璃、冰：closest-hit + 透射。
- 彩色玻璃：吸收系数。
- 树叶：cutout + any-hit。

### 8.6 天空

- 物理天空模型（Preetham/Hosek）。
- 云 v1 用 billboard；v2 体积云。

---

## 9. 降噪与上采样

### 9.1 NRD

- REBLUR：diffuse GI / specular。
- RELAX：diffuse。
- SIGMA：shadow。
- 跨厂商 fallback。

### 9.2 DLSS RR

- 需要：color、depth、motion vector、albedo、normal、roughness、specular hit distance、diffuse hit distance。
- 替换 NRD；NVIDIA 专用。

### 9.3 DLSS SR / FSR / XeSS

- 需要：jitter、depth、motion vector、exposure。
- 相机 jitter：Halton 序列，投影矩阵偏移。
- 所有动态物体（实体、粒子、水、云）都要有 motion vector。

### 9.4 FG / Reflex

- FG 需要 swapchain 控制、present timing、UI 分离。
- Reflex 降低延迟；FG 必须配 Reflex。
- UI 不参与插值，避免重影。

---

## 10. 合成与 Present

### 10.1 v1

- Native 渲染 world 到 offscreen。
- Minecraft 在原 swapchain 上画 UI。
- 优点：兼容原版 UI/屏幕；缺点：FG 难做。

### 10.2 v2

- Native 接管 swapchain。
- Minecraft UI 渲染到单独纹理，native 合成。
- 优点：FG/HDR 完整控制；缺点：UI hook 复杂。

---

## 11. 内存

| 项 | 预算 |
|---|---|
| Brick pool | 16KB/section；10k sections ≈ 160MB |
| Material table | < 1MB |
| Textures | 1~2GB（资源包相关） |
| BLAS/TLAS | < 200MB |
| G-buffer + history | ~200MB |
| Staging ring | 256MB |
| 总计（1080p, 8~12 chunks） | **< 3GB**，目标 < 7GB 上限 |

- 使用 VMA 统一分配。
- Bindless descriptor + texture array。
- Brick pool 用 free list；定期 defrag。

---

## 12. Fallback / 决策门

| 问题 | 方案 A | 方案 B | 决策点 |
|---|---|---|---|
| 拿不到官方 `VkDevice` | hook 后端拿 native handle | 独立 device + external memory | M1 第 5 天 |
| AABB/intersection 性能不够 | brick 4³ → 8³ | 退回 triangle BLAS | M1 第 12 天 |
| 非方块几何太复杂 | 只做 cube | micro-mesh | P3 |
| DLSS 不可用 | NRD + FSR/XeSS | 仅 DLSS 用户 | P5 |
| UI 合成冲突 | v1 原版 UI | v2 接管 swapchain | P5 |

---

## 13. 待验证问题

1. 26.3 的 `GpuDevice` / Vulkan 后端是否暴露 `VkDevice` / `VkQueue` / `VkInstance`？
2. 26.3 的 `LevelRenderer` / `GameRenderer` hook 点在哪里？Mixin 能否稳定拿到相机和可见 section？
3. BlockState / light 数据在 26.3 的访问路径？
4. 原版 UI 与 native offscreen 的合成方式？
5. 26.4 snapshot 的渲染改动？
6. RTX 5060 8GB 上 1080p / 8~12 区块的真实预算？

> 这些问题必须在 M1 内给出答案，写进 `docs/report-01.md`。
