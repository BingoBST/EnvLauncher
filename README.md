# EnvLauncher

EnvLauncher 是一个轻量、原生、高性能的 Windows C++20 桌面应用程序，专门用于为不同的桌面程序注入独立的、进程级的环境变量并启动它们。

典型应用场景：
- 为开发工具（如 IntelliJ IDEA、VS Code）单独配置 HTTP / HTTPS 代理（如 Clash 本地代理 `http://127.0.0.1:7897`），而无需在系统全局开启全局代理或修改 Windows 全局环境变量。
- 为不同项目或不同版本的运行时切换单独的 SDK 路径（如 `JAVA_HOME`, `NODE_OPTIONS`, `PYTHONPATH`）。
- 隔离不同应用的临时工作环境变量，避免系统级环境变量污染与冲突。

---

## 核心原理 (Core Principle)

> **EnvLauncher uses CreateProcessW with a custom Unicode environment block and does not modify system environment variables.**

- **完全隔离的环境注入**：EnvLauncher 在启动目标程序时，获取当前环境模板，在内存中与选定的环境配置（Environment Profile）以及应用专属的环境变量覆盖（App Overrides）进行合并与大小写不敏感（Case-Insensitive）排序，并原样保留 Windows 驱动器目录等特殊隐藏变量（如 `=C:`, `=D:`）。
- **零系统污染**：EnvLauncher 将构造好的连续 UTF-16 内存块通过 Win32 API `CreateProcessW` 的 `lpEnvironment` 参数以及 `CREATE_UNICODE_ENVIRONMENT` 标志直接传递给目标新进程。
- **不碰注册表与全局环境**：EnvLauncher 绝不调用 `setx`，绝不修改注册表 `Environment` 键，绝不调用 PowerShell 脚本包装，绝不调用 `system()`，也绝不修改宿主父进程自身的全局环境变量。启动的目标程序及其子进程拥有完全独立的运行环境。

---

## 构建要求 (Build Requirements)

- **操作系统**：Windows 10 (1809 及以上) 或 Windows 11 (x64)
- **编译工具链**：
  - **MinGW-w64 (GCC 13+ / 16+)** 或 **Visual Studio 2022 (MSVC 19.30+)**
  - 支持 **C++20** 标准
- **构建系统**：CMake 3.20 或更高版本
- **依赖组件**：
  - Windows SDK (Win32 API, Direct2D 1.1, DirectWrite, DWM, ComCtl32 v6, Shell32, Ole32)
  - `nlohmann/json` (已内嵌在 `third_party/nlohmann/json.hpp`，无需额外安装)

---

## 构建与测试命令 (Build Commands)

### 方式一：使用 CMake + MinGW-w64 / Ninja（推荐）

```powershell
# 1. 在项目根目录配置 CMake
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 2. 编译项目主程序与全部自动化测试套件
cmake --build build

# 3. 运行全套自动化单元测试 (Phase 1 ~ Phase 6)
ctest --test-dir build --output-on-failure
```

### 方式二：使用 Visual Studio 2022

```powershell
# 1. 生成 Visual Studio 解决方案
cmake -B build -G "Visual Studio 17 2022" -A x64

# 2. 编译 Release 版本
cmake --build build --config Release

# 3. 运行测试套件
ctest --test-dir build -C Release --output-on-failure
```

编译完成后，生成的可执行文件为：
- GUI 主程序：`build/EnvLauncher.exe`
- 自动化测试套件：`build/test_phase1.exe` ~ `build/test_phase6.exe`

---

## 配置文件位置 (Configuration Location)

EnvLauncher 的配置采用纯 JSON 格式持久化存储，无需注册表项：

- **配置文件路径**：`%LOCALAPPDATA%\EnvLauncher\config.json`
- **示例配置文件**：参考项目根目录下的 [config.example.json](config.example.json)。

### 配置结构示例

```json
{
  "version": 1,
  "environmentProfiles": [
    {
      "id": "prof-clash-7897",
      "name": "Clash 7897",
      "variables": {
        "HTTP_PROXY": "http://127.0.0.1:7897",
        "HTTPS_PROXY": "http://127.0.0.1:7897",
        "NO_PROXY": "localhost,127.0.0.1,::1"
      }
    }
  ],
  "applications": [
    {
      "id": "app-idea",
      "name": "IntelliJ IDEA",
      "executable": "D:\\Program Files\\IntelliJ IDEA 2026.2.3\\bin\\idea64.exe",
      "workingDirectory": "D:\\Program Files\\IntelliJ IDEA 2026.2.3\\bin",
      "arguments": "",
      "environmentProfileId": "prof-clash-7897",
      "environmentOverrides": {}
    }
  ]
}
```

---

## 界面与核心功能

1. **原生 Win32 + Direct2D / DirectWrite 渲染**：
   - 全硬件加速抗锯齿绘制，文字采用 DirectWrite 亚像素排版。
   - 深度集成 Windows DWM 沉浸式暗色标题栏（`DWMWA_USE_IMMERSIVE_DARK_MODE` 与 `DWMWA_CAPTION_COLOR`）。
   - 内置 PerMonitorV2 独立高 DPI 缩放支持与自适应动态重排。
2. **应用卡片与操作**：
   - **Launch**：一键注入环境变量并快速拉起目标应用。
   - **右键菜单**：
     - **Launch**：正常注入环境启动。
     - **Launch without profile**：一键脱离 Profile 与 Overrides，仅使用纯净父进程环境启动，方便排查应用网络或配置故障。
     - **Edit**：修改应用名称、可执行文件路径（支持 Windows 原生 `IFileOpenDialog` 文件选择器）、工作目录、启动参数及绑定环境。
     - **Delete**：安全移除应用卡片（附带安全确认弹窗，明确提示不影响磁盘原有程序）。
3. **环境方案管理 (Environment Profile Management)**：
   - 支持新增、编辑、删除共享的环境配置 Profile。
   - 内置引用安全校验（Reference Guard）：若某个 Profile 仍被应用卡片绑定，系统将阻止删除并明确给出依赖的应用名称。
4. **鲁棒性与容错保障**：
   - **D2D 显卡设备丢失自愈**：显卡驱动重置或休眠唤醒（`D2DERR_RECREATE_TARGET`）时自动销毁并重建设备资源。
   - **平滑自绘滚动条**：支持鼠标滚轮滚动、拖拽滑块（Thumb）、视口比例自适应与边界自动限制。
   - **损坏配置文件保护**：若 `config.json` 语法被外部破坏，EnvLauncher 绝不静默覆盖损毁文件，而是保留原文件并安全降级为空配置启动。
   - **文本超长省略**：DirectWrite 字符级省略（Ellipsis Trimming），保障超长应用标题整洁显示。

---

## 已知限制 (Known Limitations)

1. **UAC 管理员权限**：如果目标应用在其 Manifest 中要求 `requireAdministrator` 提权运行，通过非提权的 EnvLauncher 启动可能会触发系统 UAC 弹窗或受限；EnvLauncher 自身以普通标准权限（`asInvoker`）运行，遵循安全最小特权原则。
2. **非环境变量递归解析**：变量值按原始纯文本直接注入目标进程环境块，默认不递归展开 `%OTHER_VAR%` 变量（符合 Win32 API 原生标准，由目标进程按需解析）。
3. **独立进程生命周期**：目标应用启动后与 EnvLauncher 解除句柄绑定，各自拥有独立的进程空间；关闭 EnvLauncher 不会影响已启动的应用。
