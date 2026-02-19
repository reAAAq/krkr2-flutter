<p align="center">
  <h1 align="center">krkr2-flutter</h1>
  <p align="center">Next-Generation KiriKiri2 Cross-Platform Emulator Built with Flutter</p>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/status-In%20Development-orange" alt="Status">
  <img src="https://img.shields.io/badge/engine-KiriKiri2-blue" alt="Engine">
  <img src="https://img.shields.io/badge/framework-Flutter-02569B" alt="Flutter">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
</p>

---

**语言 / Language**: [中文](README.md) | English

> 🙏 This project is a refactor based on [krkr2](https://github.com/2468785842/krkr2). Thanks to the original author for the contribution.

## Overview

**krkr2-flutter** is a modernized rewrite of the KrKr2 emulator, designed to run visual novel games built with the **KiriKiri engine** (a.k.a. T Visual Presenter). It replaces all non-core engine components with the Flutter framework, bridging the native C++ engine via FFI / Platform Channels, and targets macOS, Windows, Linux, Android and more.

## 🔥 Upcoming Task

> **Replace Cocos2d-x with ANGLE**: The current engine rendering pipeline relies on Cocos2d-x + GLFW. We plan to replace it entirely with [ANGLE](https://github.com/google/angle) (Almost Native Graphics Layer Engine), maintained by Google. ANGLE transparently translates OpenGL ES 2.0 calls to native graphics APIs on each platform (macOS → Metal, Windows → D3D11, Linux → Vulkan/Desktop GL), so the existing engine rendering code requires virtually no changes. The engine will use EGL Pbuffer Surfaces for offscreen rendering, and frames will be shared with Flutter via GPU texture sharing (IOSurface, etc.) for zero-copy high-performance display. This will greatly simplify the architecture, unify the cross-platform graphics interface, and pave the way for Texture mode and multi-platform adaptation.

## Architecture

```
┌──────────────────────────────────────────────────┐
│               Flutter UI Layer                   │
│    (apps/flutter_app — Cross-platform UI/Console)│
├──────────────────────────────────────────────────┤
│       flutter_engine_bridge (Flutter Plugin)     │
│  (bridge/ — Platform Channel · NativeView embed) │
├──────────────────────────────────────────────────┤
│            engine_api (C Shared Library)          │
│    (bridge/engine_api — FFI exports · Lifecycle)  │
├────────────────────┬─────────────────────────────┤
│  Cocos2d-x / GLFW  │   KiriKiri2 Core Engine     │
│ (OpenGL Rendering)  │  (TJS2 · KAG · AV Decode)  │
└────────────────────┴─────────────────────────────┘
```

### Project Structure

```
krkr2/
├── apps/flutter_app/        # Flutter main app (Dart UI + debug console)
├── bridge/
│   ├── engine_api/          # C/C++ shared library, exports engine API (engine_create/tick/destroy, etc.)
│   └── flutter_engine_bridge/  # Flutter Plugin wrapping Platform Channel and NativeView
├── cpp/
│   ├── core/                # KiriKiri2 core engine source
│   ├── plugins/             # KiriKiri2 plugins
│   └── external/            # Third-party C++ libraries
├── platforms/               # Platform entry points (standalone executables, non-Flutter mode)
├── vcpkg/                   # vcpkg overlay ports & triplets
├── scripts/                 # Build helper scripts
├── tests/                   # Unit tests & compatibility samples
└── tools/                   # Utilities (xp3 unpacker, etc.)
```

## Development Status

> ⚠️ This project is under active development. No stable release is available yet. macOS is the most advanced platform.

| Module | Status | Notes |
|--------|--------|-------|
| C++ Engine Core Build | ✅ Done | KiriKiri2 core + Cocos2d-x + GLFW compiles on all platforms |
| engine_api Bridge Layer | ✅ Done | Exports `engine_create` / `engine_tick` / `engine_destroy` C APIs |
| Flutter Plugin (macOS) | ✅ Mostly Done | Platform Channel communication, NativeView embedding (reparent GLFW NSView into Flutter container) |
| Flutter Debug UI | ✅ Mostly Done | Supports nativeView / texture / software render mode switching, FPS control, engine lifecycle management |
| macOS nativeView Mode | 🔧 In Progress | Engine display working, touch events forwarded; Retina scaling / viewport offset still being fixed |
| macOS Texture Mode | 📋 Planned | Copy engine frames to Flutter via Texture Widget |
| Windows / Linux | 📋 Planned | Plugin skeleton created, pending adaptation |
| Android | 📋 Planned | Legacy Android code pending migration |
| Game Compatibility Testing | 📋 Planned | Pending stable engine rendering |

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| **CMake** | ≥ 3.28 | C++ build system |
| **Ninja** | Latest | CMake generator |
| **vcpkg** | Latest | C++ package manager; set `VCPKG_ROOT` environment variable |
| **Flutter** | ≥ 3.11 (Dart ≥ 3.11) | Flutter SDK |
| **Xcode** | ≥ 15 (macOS) | Required for macOS builds |
| **ccache** | Optional | Speeds up repeated C++ compilation |

## Build

### 1. Clone the Repository

```bash
git clone --recursive https://github.com/reAAAq/krkr2-flutter.git
cd krkr2-flutter
```

### 2. Set Up vcpkg

Make sure the `VCPKG_ROOT` environment variable points to your vcpkg installation (the project ships one under `.devtools/vcpkg`):

```bash
export VCPKG_ROOT=$(pwd)/.devtools/vcpkg

# Bootstrap vcpkg on first use
$VCPKG_ROOT/bootstrap-vcpkg.sh
```

### 3. Build the C++ Engine

The project uses CMake Presets to manage build configurations:

**macOS (Debug):**

```bash
# Configure
cmake --preset "MacOS Debug Config"

# Build
cmake --build --preset "MacOS Debug Build"
```

**macOS (Release):**

```bash
cmake --preset "MacOS Release Config"
cmake --build --preset "MacOS Release Build"
```

**Windows:**

```bash
cmake --preset "Windows Debug Config"
cmake --build --preset "Windows Debug Build"
```

**Linux:**

```bash
cmake --preset "Linux Debug Config"
cmake --build --preset "Linux Debug Build"
```

> Build artifacts are output to `out/<platform>/<config>/`, including the engine executable and `libengine_api` shared library.

### 4. Run the Flutter App

The Flutter app is located in `apps/flutter_app` and depends on the `libengine_api` shared library built in the previous step.

```bash
cd apps/flutter_app

# Fetch Dart dependencies
flutter pub get

# Run on macOS
flutter run -d macos

# Run on Linux
flutter run -d linux

# Run on Windows
flutter run -d windows
```

> **Note:** Before running on macOS for the first time, run `pod install`:
> ```bash
> cd apps/flutter_app/macos && pod install && cd -
> ```

### 5. Helper Scripts

```bash
# Linux one-click build
./scripts/build-linux.sh

# Windows one-click build
scripts\build-windows.bat
```

## Available CMake Presets

| Preset Name | Type | Platform | Output Directory |
|-------------|------|----------|-----------------|
| `MacOS Debug Config` / `MacOS Debug Build` | Debug | macOS | `out/macos/debug/` |
| `MacOS Release Config` / `MacOS Release Build` | Release | macOS | `out/macos/release/` |
| `Windows Debug Config` / `Windows Debug Build` | Debug | Windows | `out/windows/debug/` |
| `Windows Release Config` / `Windows Release Build` | Release | Windows | `out/windows/release/` |
| `Linux Debug Config` / `Linux Debug Build` | Debug | Linux | `out/linux/debug/` |
| `Linux Release Config` / `Linux Release Build` | Release | Linux | `out/linux/release/` |

## License

This project is licensed under the MIT License. See [LICENSE](./LICENSE) for details.
