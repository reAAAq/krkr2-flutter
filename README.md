<p align="center">
  <h1 align="center">krkr2-flutter</h1>
  <p align="center">基于 Flutter 重构的下一代 KiriKiri2 跨平台模拟器</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
</p>

---

**语言 / Language**: 中文 | [English](README_EN.md)

> 🙏 本项目基于 [krkr2](https://github.com/2468785842/krkr2) 重构，感谢原作者的贡献。

## 简介

**krkr2-flutter** 是 KrKr2 模拟器的现代化重构版本，用于运行基于 **KiriKiri 引擎**（又称 T Visual Presenter）开发的视觉小说游戏。使用 Flutter 框架替换所有非核心引擎组件，通过 FFI/Platform Channel 桥接原生 C++ 引擎，支持 macOS、Windows、Linux、Android 等平台。

## 技术架构

```
┌──────────────────────────────────────────────────┐
│               Flutter UI Layer                   │
│       (apps/flutter_app — 跨平台 UI · 控制面板)    │
├──────────────────────────────────────────────────┤
│       flutter_engine_bridge (Flutter Plugin)     │
│   (bridge/ — Platform Channel · NativeView 嵌入)  │
├──────────────────────────────────────────────────┤
│            engine_api (C Shared Library)          │
│   (bridge/engine_api — FFI 导出 · 生命周期管理)     │
├────────────────────┬─────────────────────────────┤
│  Cocos2d-x / GLFW  │   KiriKiri2 Core Engine     │
│  (OpenGL 渲染管线)   │  (TJS2 · KAG · 音视频解码)   │
└────────────────────┴─────────────────────────────┘
```

### 项目目录结构

```
krkr2/
├── apps/flutter_app/        # Flutter 主应用 (Dart UI + 调试控制台)
├── bridge/
│   ├── engine_api/          # C/C++ 共享库，导出引擎 API (engine_create/tick/destroy 等)
│   └── flutter_engine_bridge/  # Flutter Plugin，封装 Platform Channel 和 NativeView
├── cpp/
│   ├── core/                # KiriKiri2 核心引擎源码
│   ├── plugins/             # KiriKiri2 插件
│   └── external/            # 第三方 C++ 库
├── platforms/               # 各平台入口 (独立可执行文件，非 Flutter 模式)
├── vcpkg/                   # vcpkg overlay ports & triplets
├── scripts/                 # 构建辅助脚本
├── tests/                   # 单元测试 & 兼容性样本
└── tools/                   # 辅助工具 (xp3 解包等)
```

## 🔥 近期任务

> **使用 ANGLE 替代 Cocos2d-x 渲染层**：当前引擎渲染管线依赖 Cocos2d-x + GLFW，计划引入 [ANGLE](https://github.com/google/angle) (Almost Native Graphics Layer Engine) 将其完全替代。ANGLE 由 Google 维护，可将 OpenGL ES 2.0 调用透明翻译为各平台原生图形 API（macOS → Metal、Windows → D3D11、Linux → Vulkan/Desktop GL），现有引擎渲染代码几乎无需改动。引擎将通过 EGL Pbuffer Surface 实现离屏渲染，渲染结果通过 GPU 纹理共享（IOSurface 等）传给 Flutter，实现零拷贝高性能显示。这将大幅简化架构、统一跨平台图形接口，并为 Texture 模式和多平台适配扫清障碍。

## 开发状态

> ⚠️ 本项目处于活跃开发阶段，尚未发布稳定版本。macOS 平台进度领先。

| 模块 | 状态 | 说明 |
|------|------|------|
| C++ 引擎核心编译 | ✅ 完成 | KiriKiri2 核心 + Cocos2d-x + GLFW 全平台可编译 |
| engine_api 桥接层 | ✅ 完成 | 导出 `engine_create` / `engine_tick` / `engine_destroy` 等 C API |
| Flutter Plugin (macOS) | ✅ 基本完成 | Platform Channel 通信、NativeView 嵌入（将 GLFW NSView reparent 到 Flutter 容器） |
| Flutter 调试 UI | ✅ 基本完成 | 支持 nativeView / texture / software 三种渲染模式切换、FPS 控制、引擎生命周期管理 |
| macOS nativeView 模式 | 🔧 调试中 | 引擎画面已可显示，触控事件已转发；Retina 适配 / viewport 偏移仍在修复 |
| macOS texture 模式 | 📋 计划中 | 通过 Texture Widget 将引擎帧拷贝到 Flutter |
| Windows / Linux 平台 | 📋 计划中 | Plugin 骨架已创建，待适配 |
| Android 平台 | 📋 计划中 | 原有 Android 代码待迁移整合 |
| 游戏兼容性测试 | 📋 计划中 | 待引擎渲染完全稳定后进行 |

## 前置依赖

| 工具 | 版本要求 | 说明 |
|------|---------|------|
| **CMake** | ≥ 3.28 | C++ 构建系统 |
| **Ninja** | 最新 | CMake 生成器 |
| **vcpkg** | 最新 | C++ 包管理器，需设置 `VCPKG_ROOT` 环境变量 |
| **Flutter** | ≥ 3.11 (Dart ≥ 3.11) | Flutter SDK |
| **Xcode** | ≥ 15 (macOS) | macOS 编译需要 |
| **ccache** | 可选 | 加速 C++ 重复编译 |

## 构建

### 1. 克隆仓库

```bash
git clone --recursive https://github.com/reAAAq/krkr2-flutter.git
cd krkr2-flutter
```

### 2. 安装 vcpkg 依赖

确保 `VCPKG_ROOT` 环境变量指向 vcpkg 安装目录（本项目在 `.devtools/vcpkg` 下自带了一份）：

```bash
export VCPKG_ROOT=$(pwd)/.devtools/vcpkg

# 首次需要 bootstrap vcpkg
$VCPKG_ROOT/bootstrap-vcpkg.sh
```

### 3. 构建 C++ 引擎

项目使用 CMake Presets 管理构建配置：

**macOS (Debug)：**

```bash
# 配置
cmake --preset "MacOS Debug Config"

# 编译
cmake --build --preset "MacOS Debug Build"
```

**macOS (Release)：**

```bash
cmake --preset "MacOS Release Config"
cmake --build --preset "MacOS Release Build"
```

**Windows：**

```bash
cmake --preset "Windows Debug Config"
cmake --build --preset "Windows Debug Build"
```

**Linux：**

```bash
cmake --preset "Linux Debug Config"
cmake --build --preset "Linux Debug Build"
```

> 编译产物输出到 `out/<platform>/<config>/` 目录，包含引擎可执行文件和 `libengine_api` 共享库。

### 4. 运行 Flutter 应用

Flutter 应用位于 `apps/flutter_app`，它依赖上一步编译的 `libengine_api` 共享库。

```bash
cd apps/flutter_app

# 获取 Dart 依赖
flutter pub get

# macOS 运行
flutter run -d macos

# Linux 运行
flutter run -d linux

# Windows 运行
flutter run -d windows
```

> **注意：** 首次在 macOS 运行前需要执行 `pod install`：
> ```bash
> cd apps/flutter_app/macos && pod install && cd -
> ```

### 5. 快捷脚本

```bash
# Linux 一键构建
./scripts/build-linux.sh

# Windows 一键构建
scripts\build-windows.bat
```

## 可用的 CMake Presets

| Preset 名称 | 类型 | 平台 | 输出目录 |
|-------------|------|------|---------|
| `MacOS Debug Config` / `MacOS Debug Build` | Debug | macOS | `out/macos/debug/` |
| `MacOS Release Config` / `MacOS Release Build` | Release | macOS | `out/macos/release/` |
| `Windows Debug Config` / `Windows Debug Build` | Debug | Windows | `out/windows/debug/` |
| `Windows Release Config` / `Windows Release Build` | Release | Windows | `out/windows/release/` |
| `Linux Debug Config` / `Linux Debug Build` | Debug | Linux | `out/linux/debug/` |
| `Linux Release Config` / `Linux Release Build` | Release | Linux | `out/linux/release/` |

## 许可证

本项目基于 MIT 许可证开源，详见 [LICENSE](./LICENSE)。
