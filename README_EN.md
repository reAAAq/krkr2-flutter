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

**KrKr2 Next** is a modern, cross-platform runtime for the [KiriKiri2](https://en.wikipedia.org/wiki/KiriKiri) visual novel engine. It is fully compatible with original game scripts, uses modern graphics APIs for hardware-accelerated rendering, and includes numerous optimizations for both rendering performance and script execution. Built on Flutter for a unified cross-platform UI, it targets macOS · iOS · Windows · Linux · Android.

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

## Development Progress

> ⚠️ This project is under active development. No stable release is available yet. macOS is the most advanced platform.

| Module | Status | Notes |
|--------|--------|-------|
| C++ Engine Core Build | ✅ Done | KiriKiri2 core engine compiles on all platforms |
| ANGLE Rendering Migration | ✅ Mostly Done | Replaced legacy Cocos2d-x + GLFW pipeline with EGL/GLES offscreen rendering |
| engine_api Bridge Layer | ✅ Done | Exports `engine_create` / `engine_tick` / `engine_destroy` C APIs |
| Flutter Plugin | ✅ Mostly Done | Platform Channel communication, Texture bridge |
| Zero-Copy Texture Rendering | ✅ Mostly Done | Zero-copy engine render frame sharing to Flutter via platform-native texture mechanisms |
| Flutter Debug UI | ✅ Mostly Done | FPS control, engine lifecycle management, rendering status monitor |
| Input Event Forwarding | ✅ Mostly Done | Mouse / touch event coordinate mapping and forwarding to the engine |
| Engine Performance Optimization | 🔨 In Progress | SIMD pixel blending, GPU compositing pipeline, VM interpreter optimization, etc. |
| Original krkr2 Emulator Feature Porting | 📋 Planned | Gradually port original krkr2 emulator features to the new architecture |
| Game Compatibility Testing | 📋 Planned | Pending stable multi-platform rendering |

## Platform Support

| Platform | Status | Graphics Backend | Texture Sharing |
|----------|--------|-----------------|----------------|
| macOS | ✅ Mostly Done | Metal | IOSurface |
| iOS | 🔨 In Progress | Metal | IOSurface |
| Windows | 📋 Planned | Direct3D 11 | D3D11 Texture |
| Linux | 📋 Planned | Vulkan / Desktop GL | DMA-BUF |
| Android | 🔨 Pipeline Working, Optimizing | OpenGL ES / Vulkan | HardwareBuffer |

## Engine Performance Optimization

| Priority | Task | Status |
|----------|------|--------|
| P0 | Pixel Blend SIMD ([Highway](https://github.com/google/highway)) | ✅ Done |
| P0 | Full GPU Compositing Pipeline | 🔨 In Progress |
| P0 | TJS2 VM Interpreter (computed goto) | 📋 Planned |
| P1 | Event System Refactor (lock-free queue) | 📋 Planned |
| P1 | Image Loading Thread Pool | 📋 Planned |
| P1 | tTJSVariant Fast Path | 📋 Planned |
| P2 | RefCount → Smart Pointers | 📋 Planned |
| P2 | Custom Containers → Modern | 📋 Planned |
| P2 | XP3 Archive zstd Support | 📋 Planned |

## License

This project is licensed under the MIT License. See [LICENSE](./LICENSE) for details.
