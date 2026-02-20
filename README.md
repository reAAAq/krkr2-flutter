<p align="center">
  <h1 align="center">KrKr2 Next</h1>
  <p align="center">基于 Flutter 重构的下一代 KiriKiri2 跨平台模拟器</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/graphics-ANGLE-red" alt="ANGLE">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
</p>

---

**语言 / Language**: 中文 | [English](README_EN.md)

> 🙏 本项目基于 [krkr2](https://github.com/2468785842/krkr2) 重构，感谢原作者的贡献。
>
> 📦 新仓库地址：[KrKr2-Next](https://github.com/reAAAq/KrKr2-Next)

## 简介

**krkr2-flutter** 是 KrKr2 模拟器的现代化重构版本，用于运行基于 **KiriKiri 引擎**（又称 T Visual Presenter）开发的视觉小说游戏。使用 Flutter 框架替换所有非核心引擎组件，通过 FFI/Platform Channel 桥接原生 C++ 引擎，并借助 [ANGLE](https://chromium.googlesource.com/angle/angle) 图形抽象层支持 Metal、Vulkan、Direct3D 11 等现代图形 API，实现跨 macOS、Windows、Linux、Android 等平台的高性能渲染。

## 当前进展

项目已完成从传统 OpenGL 渲染管线到现代图形 API 的全面迁移。引擎渲染层通过 ANGLE 统一抽象，在各平台自动选择最优图形后端——macOS 使用 **Metal**、Windows 使用 **Direct3D 11**、Linux 使用 **Vulkan**，并通过平台原生纹理共享机制（IOSurface / D3D11 Texture / DMA-BUF）实现引擎渲染帧到 Flutter 的零拷贝传递。模拟器 UI 正在基于 Flutter 框架持续构建中。

下图为当前在 macOS 上通过 Metal 后端运行的实际效果：

<p align="center">
  <img src="doc/1.png" alt="macOS Metal 后端运行截图" width="800">
</p>

## 技术架构

<p align="center">
  <img src="doc/architecture.png" alt="技术架构图" width="700">
</p>

**渲染管线**：引擎通过 ANGLE 的 EGL Pbuffer Surface 进行离屏渲染（OpenGL ES 2.0），渲染结果通过平台原生纹理共享机制（macOS → IOSurface、Windows → D3D11 Texture、Linux → DMA-BUF）零拷贝传递给 Flutter Texture Widget 显示。

### 项目目录结构

```
krkr2/
├── apps/flutter_app/        # Flutter 主应用 (Dart UI + 调试控制台)
├── bridge/
│   ├── engine_api/          # C/C++ 共享库，导出引擎 API (engine_create/tick/destroy 等)
│   └── flutter_engine_bridge/  # Flutter Plugin，封装 Platform Channel 和 Texture 桥接
├── cpp/
│   ├── core/                # KiriKiri2 核心引擎源码
│   ├── plugins/             # KiriKiri2 插件
│   └── external/            # 第三方 C++ 库 (ANGLE 等)
├── vcpkg/                   # vcpkg overlay ports & triplets
├── scripts/                 # 构建辅助脚本
└── tools/                   # 辅助工具 (xp3 解包等)
```

## 开发状态

> ⚠️ 本项目处于活跃开发阶段，尚未发布稳定版本。macOS 平台进度领先。

| 模块 | 状态 | 说明 |
|------|------|------|
| C++ 引擎核心编译 | ✅ 完成 | KiriKiri2 核心引擎全平台可编译 |
| ANGLE 渲染层迁移 | ✅ 基本完成 | 替代原 Cocos2d-x + GLFW 渲染管线，使用 EGL/GLES 离屏渲染 |
| engine_api 桥接层 | ✅ 完成 | 导出 `engine_create` / `engine_tick` / `engine_destroy` 等 C API |
| Flutter Plugin (macOS) | ✅ 基本完成 | Platform Channel 通信、Texture 纹理桥接 |
| macOS Texture 模式 | ✅ 基本完成 | 通过 IOSurface 零拷贝共享引擎渲染帧到 Flutter Texture Widget |
| Flutter 调试 UI | ✅ 基本完成 | FPS 控制、引擎生命周期管理、渲染状态监控 |
| 输入事件转发 | ✅ 基本完成 | 鼠标 / 触控事件坐标映射转发到引擎 |
| Windows 平台 | 📋 计划中 | Plugin 骨架已创建，ANGLE D3D11 后端待适配 |
| Linux 平台 | 📋 计划中 | Plugin 骨架已创建，ANGLE Vulkan/Desktop GL 后端待适配 |
| Android 平台 | 📋 计划中 | 原有 Android 代码待迁移整合 |
| 游戏兼容性测试 | 📋 计划中 | 待多平台渲染稳定后进行 |

## 许可证

本项目基于 MIT 许可证开源，详见 [LICENSE](./LICENSE)。
