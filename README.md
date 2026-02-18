<p align="center">
  <h1 align="center">krkr2-flutter</h1>
  <p align="center">基于 Flutter 重构的下一代 KiriKiri2 跨平台模拟器</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/graphics-Vulkan-red" alt="Vulkan">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
</p>

---

**语言 / Language**: 中文 | [English](README_EN.md)

> 🙏 本项目基于 [krkr2](https://github.com/2468785842/krkr2) 重构，感谢原作者的贡献。

## 简介

**KrKr2** 是一款跨平台模拟器，用于运行基于 **KiriKiri 引擎**（又称 T Visual Presenter）开发的游戏，支持 Android、Windows、Linux 和 macOS 平台。

**krkr2-flutter** 是 KrKr2 模拟器的现代化重构版本，旨在使用 Flutter 框架替换所有非核心引擎组件，并引入 Vulkan 图形 API 以提供高性能渲染支持。

## 重构背景

KrKr2 模拟器原有技术栈在长期维护过程中暴露出多项技术债务，在 Android 平台上尤为突出：

| 问题领域 | 具体表现 |
|---------|---------|
| **平台兼容性** | 底层依赖与新版 Android SDK / NDK 存在 ABI 兼容性冲突，适配成本逐版递增 |
| **渲染性能** | 传统 OpenGL ES 管线无法有效利用现代移动 GPU 的 Tile-Based 架构与并行计算能力 |
| **工程效率** | 各平台代码分支差异大，缺少统一的 UI 层抽象，跨平台维护开销高 |

## 技术架构

```
┌─────────────────────────────────────────────┐
│              Flutter UI Layer               │
│         (跨平台 UI · 状态管理 · 路由)         │
├─────────────────────────────────────────────┤
│           Platform Channel / FFI            │
├──────────────────┬──────────────────────────┤
│  Vulkan Renderer │   KiriKiri2 Core Engine  │
│  (图形渲染管线)   │   (脚本解释 · 游戏逻辑)    │
└──────────────────┴──────────────────────────┘
```

### 核心设计

- **UI 层**：采用 Flutter 构建全平台统一的用户界面，取代原有各平台原生 UI 实现
- **渲染层**：集成 Vulkan 图形 API，实现高效的跨平台 GPU 渲染管线
- **引擎层**：保留 KiriKiri2 核心引擎（TJS2 解释器、KAG 解析、游戏兼容性逻辑），通过 FFI 与 Flutter 层桥接

### 重构范围

| 模块 | 策略 | 说明 |
|------|------|------|
| KiriKiri2 核心引擎 | **保留** | TJS2 解释器、KAG 解析、音视频解码等核心逻辑不变 |
| 用户界面 | **重构** | 使用 Flutter 统一重写，支持 Material Design |
| 渲染管线 | **重构** | 从 OpenGL ES 迁移至 Vulkan，提升图形性能 |
| 平台适配层 | **重构** | 依托 Flutter 平台通道机制，统一平台差异处理 |

## 目标平台

| 平台 | 架构 | 状态 |
|------|------|------|
| Android | arm64-v8a / x86_64 | 🔄 开发中 |
| Windows | x86_64 | 🔄 开发中 |
| Linux | x86_64 | 🔄 开发中 |
| macOS | arm64 | 🔄 开发中 |

## 开发状态

> ⚠️ 本项目目前处于活跃开发阶段，尚未发布稳定版本。

- [x] 技术选型与架构设计
- [ ] Flutter 框架集成与基础 UI 搭建
- [ ] Vulkan 渲染管线实现
- [ ] KiriKiri2 引擎 FFI 桥接
- [ ] 全平台构建流程打通
- [ ] 游戏兼容性测试与性能调优

## 构建

构建文档正在编写中，请关注后续更新。

## 许可证

本项目基于 MIT 许可证开源，详见 [LICENSE](./LICENSE)。
