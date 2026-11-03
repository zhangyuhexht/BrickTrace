# BrickTrace 环境搭建（RTX 5060 / Windows 11）

> 目标：一台能编译、能跑 26.3、能验证 RT/DLSS 的机器。
> 所有命令在 PowerShell 中执行。

---

## 1. 硬件与系统

| 项 | 要求 |
|---|---|
| OS | Windows 11 64-bit（22H2+） |
| GPU | NVIDIA RTX 5060 8GB（桌面/笔记本均可） |
| RAM | 32GB 建议（16GB 最低） |
| 磁盘 | 20GB 空闲 |
| 驱动 | 最新 NVIDIA Game Ready / Studio |
| 电源 | 笔记本插电，Windows 高性能模式 |

> 5060 只有 8GB VRAM，所有验收按 1080p / 8~12 区块设计。

---

## 2. 必装软件

| 软件 | 版本 | 用途 |
|---|---|---|
| NVIDIA Driver | 最新 | RT / DLSS |
| Vulkan SDK | 1.4.x（LunarG） | Vulkan + RT validation |
| Visual Studio 2022 Build Tools | v143 + Windows SDK | MSVC |
| CMake | 3.28+ | native 构建 |
| Git | 最新 | 版本管理 |
| JDK | 25（Temurin/Microsoft） | Minecraft 26.3 |
| RenderDoc | 最新 | 抓帧 |
| Nsight Graphics | 最新（可选） | GPU 调试 |

---

## 3. 安装命令（winget 示例）

```powershell
winget install --id Git.Git -e
winget install --id Kitware.CMake -e
winget install --id Microsoft.VisualStudio.2022.BuildTools -e
winget install --id EclipseAdoptium.Temurin.25.JDK -e
winget install --id LunarG.VulkanSDK -e
winget install --id RenderDoc.RenderDoc -e
```

安装 Build Tools 时勾选：

- MSVC v143
- Windows 10/11 SDK
- C++ CMake tools（可选）

安装后**重开终端**，确保 `VULKAN_SDK` 存在。

---

## 4. 验证

```powershell
nvidia-smi
vulkaninfo --summary
java -version
cmake --version
git --version
```

检查 RT 扩展：

```powershell
vulkaninfo | Select-String "VK_KHR_ray_tracing_pipeline|VK_KHR_acceleration_structure|VK_KHR_ray_query"
```

期望看到三项都出现。

一键检查：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-env.ps1
```

---

## 5. Minecraft 26.3 + Fabric

1. 官方启动器安装 **26.3**，至少启动一次。
2. 安装 **Fabric Loader 0.19.3+** 对应 26.3。
3. 下载 **Fabric API**（26.3 版本）放入 `mods`。
4. 确认启动器使用 **Java 25**。
5. 复制一份独立实例用于 RT 测试，避免污染主实例。

推荐目录结构：

```
instances/
├─ 26.3-vanilla/
├─ 26.3-fabric-dev/        # 开发用
└─ 26.3-lumen-baseline/    # 黑盒对照
```

---

## 6. Lumen 基线（可选）

- 文件：`mods/Lumen-0.1.3-alpha.jar`（ARR，闭源）
- 安装到 `26.3-lumen-baseline/mods`
- 需要 Fabric API、NVIDIA RTX、8GB+ VRAM
- **不要**把它提交到 git；只做行为对照

---

## 7. 构建验证

```powershell
git clone <repo>
cd BrickTrace
.\gradlew.bat build
```

native：

```powershell
cmake -S native -B native/build -G "Visual Studio 17 2022" -A x64
cmake --build native/build --config RelWithDebInfo
```

shader：

```powershell
glslangValidator --version
```

---

## 8. 常见问题

| 问题 | 处理 |
|---|---|
| `vulkaninfo` 找不到 | 重开终端；检查 `VULKAN_SDK`；用 `%VULKAN_SDK%\Bin\vulkaninfo.exe` |
| RT 扩展缺失 | 更新驱动；确认是 RTX 显卡；笔记本确认独显模式 |
| Java 版本不对 | 启动器实例设置里指定 JDK 25 |
| Fabric 崩溃 | 检查 Fabric Loader / Fabric API 版本是否匹配 26.3 |
| 编译找不到 MSVC | 用 “x64 Native Tools Command Prompt for VS 2022” |
| DLSS 不可用 | 确认 RTX 显卡 + 最新驱动 + NGX DLL 存在 |
| 8GB 显存不够 | 降区块到 8、关闭 HDR、DLSS Performance |

---

## 9. 开发机（1660S）能做什么

- Java / Mixin / 配置 / UI。
- 世界导出、材质烘焙、CPU DDA。
- 单元测试、golden image 的 CPU 参考。
- 不能验证 RT / DLSS / VRAM；不要在这台机器上判断性能。
