# Report 01 — M1（Hello RT + Brick DDA）

> 复制本模板，填完后保存为 `docs/report-01.md`。

## 0. 摘要

| 项 | 结果 |
|---|---|
| 日期 | |
| 机器 / GPU | RTX 5060 8GB |
| 驱动 | |
| Commit | |
| 官方 VkDevice | ✅ / ❌ |
| RT 三角形 | ✅ / ❌ |
| Brick DDA | ✅ / ❌ |
| Go/No-Go | Go / No-Go |

一句话结论：

---

## 1. 环境

粘贴 `scripts/check-env.ps1` 的摘要（GPU、驱动、Vulkan、Java、CMake、MSVC）。

```
（粘贴）
```

---

## 2. 设备访问（最大风险）

- 官方 `VkDevice` / `VkQueue` / `VkInstance`：拿到 / 没拿到
- 获取方式：hook 后端 / 官方 API / 其他
- 失败时的替代方案：
- 相关日志/截图：

---

## 3. RT 三角形

- 使用的扩展：
- Pipeline 结构（raygen / miss / closest-hit）：
- 分辨率 / 是否上屏：
- RenderDoc capture 路径：
- 遇到的问题：

---

## 4. 离线 Brick DDA

- 世界导出：尺寸、方块数、文件大小
- CPU DDA：耗时、图像路径
- GPU DDA（如做）：耗时、与 CPU 差异
- 正确性：漏面 / 漏光 / 法线错误

---

## 5. 基准

| 场景 | 分辨率 | 区块 | 帧时间 | FPS | VRAM | 备注 |
|---|---|---|---|---|---|---|
| RT 三角形 | 1080p | - | | | | |
| Brick DDA（compute） | 1080p | 8 | | | | |
| RT AABB 阴影 | 1080p | 8 | | | | |
| 方块更新 | - | - | - | - | - | BLAS 重建次数 |

---

## 6. 阻塞点

| 问题 | 复现步骤 | 已尝试 | 需要谁支持 |
|---|---|---|---|
| | | | |

---

## 7. 决策

- [ ] Go：进入 P2（地形阴影）
- [ ] No-Go：先解决 ______
- [ ] 改路线：______（需项目 owner 确认）

---

## 8. 下一步（24 小时）

1.
2.
3.
