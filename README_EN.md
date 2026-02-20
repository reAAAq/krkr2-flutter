<p align="center">
  <h1 align="center">KrKr2 Next</h1>
  <p align="center">Next-Generation KiriKiri2 Cross-Platform Emulator Built with Flutter</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/graphics-ANGLE-red" alt="ANGLE">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
</p>

---

**语言 / Language**: [中文](README.md) | English

> 🙏 This project is a refactor based on [krkr2](https://github.com/2468785842/krkr2). Thanks to the original author for the contribution.

## Overview

**krkr2-flutter** is a modernized rewrite of the KrKr2 emulator, designed to run visual novel games built with the **KiriKiri engine** (a.k.a. T Visual Presenter). The KiriKiri engine was originally designed in the early 2000s, with its architecture and performance strategies tailored to the hardware of that era — single-core CPUs, limited memory, and fixed-function GPUs. This project preserves the engine's core compatibility while comprehensively modernizing the rendering pipeline, SIMD instruction sets, memory management, and script virtual machine to fully leverage today's multi-core CPUs, wide SIMD instruction sets, and programmable GPUs. It replaces all non-core engine components with the Flutter framework, bridging the native C++ engine via FFI / Platform Channels. The graphics layer is powered by [ANGLE](https://chromium.googlesource.com/angle/angle), which provides a unified abstraction supporting modern graphics APIs such as Metal, Vulkan, and Direct3D 11, enabling high-performance rendering across macOS, Windows, Linux, Android and more.

## Current Progress

The project has completed a full migration from the legacy OpenGL rendering pipeline to modern graphics APIs. The engine rendering layer is now unified through ANGLE, which automatically selects the optimal graphics backend on each platform — **Metal** on macOS, **Direct3D 11** on Windows, and **Vulkan** on Linux. Engine render frames are delivered to Flutter via platform-native texture sharing mechanisms (IOSurface / D3D11 Texture / DMA-BUF) with zero-copy transfer. The emulator UI is being actively built on the Flutter framework.

The screenshot below shows the current running state on macOS with the Metal backend:

<p align="center">
  <img src="doc/1.png" alt="macOS Metal Backend Screenshot" width="800">
</p>

## Architecture

<p align="center">
  <img src="doc/architecture.png" alt="Architecture Diagram" width="700">
</p>

**Rendering Pipeline**: The engine performs offscreen rendering via ANGLE's EGL Pbuffer Surface (OpenGL ES 2.0). Rendered frames are delivered to the Flutter Texture Widget through platform-native texture sharing mechanisms (macOS → IOSurface, Windows → D3D11 Texture, Linux → DMA-BUF) with zero-copy transfer.

### Project Structure

```
krkr2/
├── apps/flutter_app/        # Flutter main app (Dart UI + debug console)
├── bridge/
│   ├── engine_api/          # C/C++ shared library, exports engine API (engine_create/tick/destroy, etc.)
│   └── flutter_engine_bridge/  # Flutter Plugin wrapping Platform Channel and Texture bridge
├── cpp/
│   ├── core/                # KiriKiri2 core engine source
│   ├── plugins/             # KiriKiri2 plugins
│   └── external/            # Third-party C++ libraries (ANGLE, etc.)
├── vcpkg/                   # vcpkg overlay ports & triplets
├── scripts/                 # Build helper scripts
└── tools/                   # Utilities (xp3 unpacker, etc.)
```

## Development Status

> ⚠️ This project is under active development. No stable release is available yet. macOS is the most advanced platform.

| Module | Status | Notes |
|--------|--------|-------|
| C++ Engine Core Build | ✅ Done | KiriKiri2 core engine compiles on all platforms |
| ANGLE Rendering Migration | ✅ Mostly Done | Replaced legacy Cocos2d-x + GLFW pipeline with EGL/GLES offscreen rendering |
| engine_api Bridge Layer | ✅ Done | Exports `engine_create` / `engine_tick` / `engine_destroy` C APIs |
| Flutter Plugin (macOS) | ✅ Mostly Done | Platform Channel communication, Texture bridge |
| macOS Texture Mode | ✅ Mostly Done | Zero-copy sharing of engine render frames to Flutter Texture Widget via IOSurface |
| Flutter Debug UI | ✅ Mostly Done | FPS control, engine lifecycle management, rendering status monitor |
| Input Event Forwarding | ✅ Mostly Done | Mouse / touch event coordinate mapping and forwarding to the engine |
| Windows | 📋 Planned | Plugin skeleton created, ANGLE D3D11 backend pending adaptation |
| Linux | 📋 Planned | Plugin skeleton created, ANGLE Vulkan/Desktop GL backend pending adaptation |
| Android | 📋 Planned | Legacy Android code pending migration |
| Game Compatibility Testing | 📋 Planned | Pending stable multi-platform rendering |

## Engine Performance Optimization

| Priority | Task | Status |
|----------|------|--------|
| P0 | Pixel Blend SIMD ([Highway](https://github.com/google/highway)) | 🔨 In Progress |
| P0 | Full GPU Compositing Pipeline | 📋 Planned |
| P0 | TJS2 VM Interpreter (computed goto) | 📋 Planned |
| P1 | Event System Refactor (lock-free queue) | 📋 Planned |
| P1 | Image Loading Thread Pool | 📋 Planned |
| P1 | tTJSVariant Fast Path | 📋 Planned |
| P2 | RefCount → Smart Pointers | 📋 Planned |
| P2 | Custom Containers → Modern | 📋 Planned |
| P2 | XP3 Archive zstd Support | 📋 Planned |

## License

This project is licensed under the MIT License. See [LICENSE](./LICENSE) for details.
