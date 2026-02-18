<p align="center">
  <h1 align="center">krkr2-flutter</h1>
  <p align="center">Next-Generation KiriKiri2 Cross-Platform Emulator Built with Flutter</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/graphics-Vulkan-red" alt="Vulkan">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
</p>

---

**语言 / Language**: [中文](README.md) | English

> 🙏 This project is a refactor based on [krkr2](https://github.com/2468785842/krkr2). Thanks to the original authors for their contributions.

## Overview

**KrKr2** is a cross-platform emulator designed to run games built with the **KiriKiri engine** (also known as T Visual Presenter), supporting Android, Windows, Linux, and macOS.

**krkr2-flutter** is the modernized rewrite of the KrKr2 emulator. It replaces all non-core engine components with the Flutter framework and introduces the Vulkan graphics API for high-performance rendering.

## Motivation

Over time, KrKr2's legacy tech stack has accumulated significant technical debt — most notably on the Android platform:

| Area | Issue |
|------|-------|
| **Platform Compatibility** | ABI conflicts between legacy dependencies and modern Android SDK / NDK versions; adaptation cost grows with each release |
| **Rendering Performance** | Traditional OpenGL ES pipeline fails to leverage tile-based architectures and parallel compute capabilities of modern mobile GPUs |
| **Engineering Efficiency** | Large per-platform code divergence with no unified UI abstraction; high cross-platform maintenance overhead |

## Architecture

```
┌─────────────────────────────────────────────┐
│              Flutter UI Layer               │
│     (Cross-platform UI · State · Routing)   │
├─────────────────────────────────────────────┤
│           Platform Channel / FFI            │
├──────────────────┬──────────────────────────┤
│  Vulkan Renderer │   KiriKiri2 Core Engine  │
│ (Graphics Pipeline) │ (Scripting · Game Logic) │
└──────────────────┴──────────────────────────┘
```

### Core Design

- **UI Layer**: Unified cross-platform user interface built with Flutter, replacing all platform-native UI implementations
- **Rendering Layer**: Vulkan graphics API integration for an efficient cross-platform GPU rendering pipeline
- **Engine Layer**: Retains the KiriKiri2 core engine (TJS2 interpreter, KAG parser, game compatibility logic), bridged to the Flutter layer via FFI

### Refactoring Scope

| Module | Strategy | Description |
|--------|----------|-------------|
| KiriKiri2 Core Engine | **Retained** | TJS2 interpreter, KAG parser, audio/video decoding — unchanged |
| User Interface | **Rewritten** | Rebuilt with Flutter, featuring Material Design |
| Rendering Pipeline | **Rewritten** | Migrated from OpenGL ES to Vulkan for improved graphics performance |
| Platform Adaptation | **Rewritten** | Unified platform abstraction via Flutter platform channels |

## Target Platforms

| Platform | Architecture | Status |
|----------|-------------|--------|
| Android | arm64-v8a / x86_64 | 🔄 In Development |
| Windows | x86_64 | 🔄 In Development |
| Linux | x86_64 | 🔄 In Development |
| macOS | arm64 | 🔄 In Development |

## Development Status

> ⚠️ This project is under active development. No stable release is available yet.

- [x] Technical planning & architecture design
- [ ] Flutter framework integration & base UI
- [ ] Vulkan rendering pipeline implementation
- [ ] KiriKiri2 engine FFI bridging
- [ ] Cross-platform build pipeline
- [ ] Game compatibility testing & performance tuning

## Build

Build documentation is being prepared. Stay tuned for updates.

## License

This project is licensed under the MIT License. See [LICENSE](./LICENSE) for details.
