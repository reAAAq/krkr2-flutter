import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

import '../engine/engine_bridge.dart';

class EngineInputEventType {
  static const int pointerDown = 1;
  static const int pointerMove = 2;
  static const int pointerUp = 3;
  static const int pointerScroll = 4;
  static const int keyDown = 5;
  static const int keyUp = 6;
  static const int textInput = 7;
  static const int back = 8;
}

/// Rendering mode for the engine surface.
/// 引擎画面的呈现模式。
enum EngineSurfaceMode {
  /// GPU zero-copy mode (macOS: IOSurface, Android: SurfaceTexture).
  /// GPU 零拷贝模式（macOS 使用 IOSurface，Android 使用 SurfaceTexture）。
  gpuZeroCopy,

  /// Flutter Texture with RGBA pixel upload (cross-platform, slower).
  /// 通过 Flutter Texture 上传 RGBA 像素（跨平台，但更慢）。
  texture,

  /// Pure software decoding via RawImage (slowest, always works).
  /// 通过 RawImage 纯软件显示（最慢，但兼容性最好）。
  software,
}

class EngineSurface extends StatefulWidget {
  const EngineSurface({
    super.key,
    required this.bridge,
    required this.active,
    this.surfaceMode = EngineSurfaceMode.gpuZeroCopy,
    this.externalTickDriven = false,
    this.onLog,
    this.onError,
  });

  final EngineBridge bridge;
  final bool active;
  final EngineSurfaceMode surfaceMode;

  /// When true, the internal frame polling timer is disabled.
  /// 为 true 时禁用内部帧轮询计时器。
  /// The parent widget must call [EngineSurfaceState.pollFrame()] after each
  /// engine tick to eliminate the dual-timer phase mismatch.
  /// 父组件需要在每次 engine tick 后主动调用
  /// [EngineSurfaceState.pollFrame()]，以消除双计时器的相位错位。
  final bool externalTickDriven;
  final ValueChanged<String>? onLog;
  final ValueChanged<String>? onError;

  @override
  EngineSurfaceState createState() => EngineSurfaceState();
}

class EngineSurfaceState extends State<EngineSurface> {
  // Keep the same bit layout as C++ TVP_SS_* so modifiers can be forwarded
  // directly into engine_api.
  // 与 C++ 侧 TVP_SS_* 保持同一位图布局，便于直接透传到 engine_api。
  static const int _modifierShift = 1 << 0;
  static const int _modifierAlt = 1 << 1;
  static const int _modifierCtrl = 1 << 2;
  static const int _modifierLeft = 1 << 3;
  static const int _modifierRight = 1 << 4;
  static const int _modifierMiddle = 1 << 5;

  // Windows virtual-key values used by the engine's input layer.
  // 引擎输入层当前按 Windows VK 语义消费 key_code，这里统一做宿主侧映射。
  static final Map<LogicalKeyboardKey, int> _logicalToVirtualKey =
      <LogicalKeyboardKey, int>{
        LogicalKeyboardKey.backspace: 0x08,
        LogicalKeyboardKey.tab: 0x09,
        LogicalKeyboardKey.enter: 0x0D,
        LogicalKeyboardKey.numpadEnter: 0x0D,
        LogicalKeyboardKey.shift: 0x10,
        LogicalKeyboardKey.shiftLeft: 0xA0,
        LogicalKeyboardKey.shiftRight: 0xA1,
        LogicalKeyboardKey.control: 0x11,
        LogicalKeyboardKey.controlLeft: 0xA2,
        LogicalKeyboardKey.controlRight: 0xA3,
        LogicalKeyboardKey.alt: 0x12,
        LogicalKeyboardKey.altLeft: 0xA4,
        LogicalKeyboardKey.altRight: 0xA5,
        LogicalKeyboardKey.pause: 0x13,
        LogicalKeyboardKey.capsLock: 0x14,
        LogicalKeyboardKey.escape: 0x1B,
        LogicalKeyboardKey.goBack: 0x1B,
        LogicalKeyboardKey.space: 0x20,
        LogicalKeyboardKey.pageUp: 0x21,
        LogicalKeyboardKey.pageDown: 0x22,
        LogicalKeyboardKey.end: 0x23,
        LogicalKeyboardKey.home: 0x24,
        LogicalKeyboardKey.arrowLeft: 0x25,
        LogicalKeyboardKey.arrowUp: 0x26,
        LogicalKeyboardKey.arrowRight: 0x27,
        LogicalKeyboardKey.arrowDown: 0x28,
        LogicalKeyboardKey.select: 0x29,
        LogicalKeyboardKey.printScreen: 0x2C,
        LogicalKeyboardKey.insert: 0x2D,
        LogicalKeyboardKey.delete: 0x2E,
        LogicalKeyboardKey.metaLeft: 0x5B,
        LogicalKeyboardKey.metaRight: 0x5C,
        LogicalKeyboardKey.contextMenu: 0x5D,
        LogicalKeyboardKey.numpad0: 0x60,
        LogicalKeyboardKey.numpad1: 0x61,
        LogicalKeyboardKey.numpad2: 0x62,
        LogicalKeyboardKey.numpad3: 0x63,
        LogicalKeyboardKey.numpad4: 0x64,
        LogicalKeyboardKey.numpad5: 0x65,
        LogicalKeyboardKey.numpad6: 0x66,
        LogicalKeyboardKey.numpad7: 0x67,
        LogicalKeyboardKey.numpad8: 0x68,
        LogicalKeyboardKey.numpad9: 0x69,
        LogicalKeyboardKey.numpadMultiply: 0x6A,
        LogicalKeyboardKey.numpadAdd: 0x6B,
        LogicalKeyboardKey.numpadSubtract: 0x6D,
        LogicalKeyboardKey.numpadDecimal: 0x6E,
        LogicalKeyboardKey.numpadDivide: 0x6F,
        LogicalKeyboardKey.f1: 0x70,
        LogicalKeyboardKey.f2: 0x71,
        LogicalKeyboardKey.f3: 0x72,
        LogicalKeyboardKey.f4: 0x73,
        LogicalKeyboardKey.f5: 0x74,
        LogicalKeyboardKey.f6: 0x75,
        LogicalKeyboardKey.f7: 0x76,
        LogicalKeyboardKey.f8: 0x77,
        LogicalKeyboardKey.f9: 0x78,
        LogicalKeyboardKey.f10: 0x79,
        LogicalKeyboardKey.f11: 0x7A,
        LogicalKeyboardKey.f12: 0x7B,
        LogicalKeyboardKey.f13: 0x7C,
        LogicalKeyboardKey.f14: 0x7D,
        LogicalKeyboardKey.f15: 0x7E,
        LogicalKeyboardKey.f16: 0x7F,
        LogicalKeyboardKey.f17: 0x80,
        LogicalKeyboardKey.f18: 0x81,
        LogicalKeyboardKey.f19: 0x82,
        LogicalKeyboardKey.f20: 0x83,
        LogicalKeyboardKey.f21: 0x84,
        LogicalKeyboardKey.f22: 0x85,
        LogicalKeyboardKey.f23: 0x86,
        LogicalKeyboardKey.f24: 0x87,
        LogicalKeyboardKey.numLock: 0x90,
        LogicalKeyboardKey.scrollLock: 0x91,
      };

  final FocusNode _focusNode = FocusNode(debugLabel: 'engine-surface-focus');
  bool _vsyncScheduled = false;
  bool _frameInFlight = false;
  bool _textureInitInFlight = false;
  ui.Image? _frameImage;
  int _lastFrameSerial = -1;
  int _surfaceWidth = 0;
  int _surfaceHeight = 0;
  int _frameWidth = 0;
  int _frameHeight = 0;
  double _devicePixelRatio = 1.0;

  // Legacy texture mode / 旧版纹理路径
  int? _textureId;

  // IOSurface zero-copy mode (macOS) / macOS 的 IOSurface 零拷贝路径
  int? _ioSurfaceTextureId;
  // ignore: unused_field
  int? _ioSurfaceId;
  bool _ioSurfaceInitInFlight = false;

  // SurfaceTexture zero-copy mode (Android) / Android 的 SurfaceTexture 零拷贝路径
  int? _surfaceTextureId;
  bool _surfaceTextureInitInFlight = false;
  Size _lastRequestedLogicalSize = Size.zero;
  double _lastRequestedDpr = 1.0;
  EngineInputEventData? _pendingPointerMoveEvent;
  bool _pointerMoveFlushScheduled = false;

  /// Current present path for the debug panel.
  /// 当前实际使用的呈现路径，供调试面板展示。
  String get debugPresentPath {
    if (_ioSurfaceTextureId != null) {
      return 'zero-copy:IOSurface';
    }
    if (_surfaceTextureId != null) {
      return 'zero-copy:SurfaceTexture';
    }
    if (_textureId != null) {
      return 'texture-upload';
    }
    if (_frameImage != null) {
      return 'software-image';
    }
    return 'pending';
  }

  /// Surface/frame size summary for debug display.
  /// 当前 surface 与 frame 的尺寸摘要，便于观察缩放链路是否正确。
  String get debugSizeSummary {
    final String surface = _surfaceWidth > 0 && _surfaceHeight > 0
        ? '${_surfaceWidth}x$_surfaceHeight'
        : 'pending';
    final String frame = _frameWidth > 0 && _frameHeight > 0
        ? '${_frameWidth}x$_frameHeight'
        : 'pending';
    return 'surface=$surface frame=$frame '
        'dpr=${_devicePixelRatio.toStringAsFixed(2)}';
  }

  @override
  void initState() {
    super.initState();
    _reconcilePolling();
  }

  @override
  void didUpdateWidget(covariant EngineSurface oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.bridge != widget.bridge) {
      unawaited(_disposeAllTextures());
    }
    if (oldWidget.surfaceMode != widget.surfaceMode) {
      unawaited(_disposeAllTextures());
    }
    if (oldWidget.active != widget.active ||
        oldWidget.bridge != widget.bridge ||
        oldWidget.surfaceMode != widget.surfaceMode ||
        oldWidget.externalTickDriven != widget.externalTickDriven) {
      _reconcilePolling();
    }
  }

  @override
  void dispose() {
    // _vsyncScheduled will simply be ignored once disposed.
    // 组件销毁后，即使还有计划中的 vsync 回调，也会被安全忽略。
    _vsyncScheduled = false;
    _frameImage?.dispose();
    unawaited(_disposeAllTextures());
    _focusNode.dispose();
    super.dispose();
  }

  Future<void> _disposeAllTextures() async {
    await _disposeTexture();
    await _disposeIOSurfaceTexture();
    await _disposeSurfaceTexture();
  }

  void _reconcilePolling() {
    if (!widget.active || widget.externalTickDriven) {
      _vsyncScheduled = false;
      return;
    }

    _scheduleVsyncPoll();
    unawaited(_pollFrame());
  }

  /// Schedule a single vsync-aligned frame callback.
  /// 安排一次与 vsync 对齐的帧回调。
  /// This replaces Timer.periodic and aligns with Flutter's display refresh.
  /// 这会替代 Timer.periodic，并与 Flutter 的屏幕刷新节奏保持同步。
  void _scheduleVsyncPoll() {
    if (_vsyncScheduled || !widget.active || widget.externalTickDriven) {
      return;
    }
    _vsyncScheduled = true;
    SchedulerBinding.instance.scheduleFrameCallback((_) {
      _vsyncScheduled = false;
      if (!mounted || !widget.active || widget.externalTickDriven) {
        return;
      }
      unawaited(_pollFrame());
      _scheduleVsyncPoll();
    });
  }

  /// Public entry point for the parent tick loop to drive frame polling.
  /// 对父级 tick 循环公开的帧轮询入口。
  /// Call this immediately after [EngineBridge.engineTick] completes
  /// to eliminate the dual-timer phase mismatch.
  /// 在 [EngineBridge.engineTick] 完成后立即调用，以消除双计时器相位偏差。
  ///
  /// When [rendered] is provided (non-null), the IOSurface path uses it
  /// directly instead of calling [engineGetFrameRenderedFlag] again.
  /// 当 [rendered] 非空时，IOSurface 路径会直接使用这个值，
  /// 而不是再次调用 [engineGetFrameRenderedFlag]。
  /// This avoids the double-read problem where the flag is reset on the
  /// first read and the second read always sees false.
  /// 这样可以避免标志位第一次读取后被清零，第二次读取永远为 false 的问题。
  Future<void> pollFrame({bool? rendered}) =>
      _pollFrame(externalRendered: rendered);

  Future<void> _ensureSurfaceSize(Size size, double devicePixelRatio) async {
    if (!widget.active) {
      return;
    }
    _devicePixelRatio = devicePixelRatio <= 0 ? 1.0 : devicePixelRatio;
    final int width = (size.width * _devicePixelRatio).round().clamp(1, 16384);
    final int height = (size.height * _devicePixelRatio).round().clamp(
      1,
      16384,
    );

    if (width == _surfaceWidth && height == _surfaceHeight) {
      await _ensureRenderTarget();
      return;
    }

    _surfaceWidth = width;
    _surfaceHeight = height;
    final int result = await widget.bridge.engineSetSurfaceSize(
      width: width,
      height: height,
    );
    if (result != 0) {
      _reportError(
        'engine_set_surface_size failed: result=$result, error=${widget.bridge.engineGetLastError()}',
      );
      return;
    }
    widget.onLog?.call(
      'surface resized: ${width}x$height (dpr=${_devicePixelRatio.toStringAsFixed(2)})',
    );
    await _ensureRenderTarget();
  }

  Future<void> _ensureRenderTarget() async {
    switch (widget.surfaceMode) {
      case EngineSurfaceMode.gpuZeroCopy:
        if (Platform.isAndroid) {
          await _ensureSurfaceTexture();
        } else {
          await _ensureIOSurfaceTexture();
        }
        break;
      case EngineSurfaceMode.texture:
        await _ensureTexture();
        break;
      case EngineSurfaceMode.software:
        // No texture needed.
        // 纯软件路径下不需要额外纹理。
        break;
    }
  }

  // --- IOSurface zero-copy mode / IOSurface 零拷贝模式 ---

  Future<void> _ensureIOSurfaceTexture() async {
    if (!widget.active ||
        _ioSurfaceInitInFlight ||
        _surfaceWidth <= 0 ||
        _surfaceHeight <= 0) {
      return;
    }

    // Check if we need to resize.
    // 检查是否需要 resize。
    if (_ioSurfaceTextureId != null) {
      if (_frameWidth == _surfaceWidth && _frameHeight == _surfaceHeight) {
        return; // Already at correct size
      }
      // Resize needed / 需要调整尺寸
      _ioSurfaceInitInFlight = true;
      try {
        final result = await widget.bridge.resizeIOSurfaceTexture(
          textureId: _ioSurfaceTextureId!,
          width: _surfaceWidth,
          height: _surfaceHeight,
        );
        if (result != null && mounted) {
          final int newIOSurfaceId = result['ioSurfaceID'] as int;
          // Tell the engine about the new IOSurface.
          // 把新的 IOSurface 信息同步给引擎侧。
          final int setResult = await widget.bridge
              .engineSetRenderTargetIOSurface(
                iosurfaceId: newIOSurfaceId,
                width: _surfaceWidth,
                height: _surfaceHeight,
              );
          if (setResult == 0) {
            _ioSurfaceId = newIOSurfaceId;
            _frameWidth = _surfaceWidth;
            _frameHeight = _surfaceHeight;
            widget.onLog?.call(
              'IOSurface resized: ${_surfaceWidth}x$_surfaceHeight (iosurface=$newIOSurfaceId)',
            );
          } else {
            _reportError(
              'engine_set_render_target_iosurface failed after resize: $setResult',
            );
          }
        }
      } finally {
        _ioSurfaceInitInFlight = false;
      }
      return;
    }

    _ioSurfaceInitInFlight = true;
    try {
      final result = await widget.bridge.createIOSurfaceTexture(
        width: _surfaceWidth,
        height: _surfaceHeight,
      );

      if (!mounted) return;
      if (result == null) {
        widget.onLog?.call(
          'IOSurface texture unavailable, falling back to legacy texture mode',
        );
        // Fall back to legacy texture mode.
        // 回退到旧版纹理上传路径。
        await _ensureTexture();
        return;
      }

      final int textureId = result['textureId'] as int;
      final int ioSurfaceId = result['ioSurfaceID'] as int;

      // Tell the C++ engine to render to this IOSurface.
      // 通知 C++ 引擎把这个 IOSurface 作为渲染目标。
      final int setResult = await widget.bridge.engineSetRenderTargetIOSurface(
        iosurfaceId: ioSurfaceId,
        width: _surfaceWidth,
        height: _surfaceHeight,
      );

      if (setResult != 0) {
        _reportError(
          'engine_set_render_target_iosurface failed: $setResult, '
          'error=${widget.bridge.engineGetLastError()}',
        );
        // Clean up and fall back.
        // 清理失败资源，然后回退到旧路径。
        await widget.bridge.disposeIOSurfaceTexture(textureId: textureId);
        await _ensureTexture();
        return;
      }

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _ioSurfaceTextureId = textureId;
        _ioSurfaceId = ioSurfaceId;
        _textureId = null; // Dispose legacy texture if any
        _frameImage = null;
        _frameWidth = _surfaceWidth;
        _frameHeight = _surfaceHeight;
      });
      previousImage?.dispose();
      widget.onLog?.call(
        'IOSurface zero-copy mode enabled (textureId=$textureId, iosurface=$ioSurfaceId)',
      );
    } finally {
      _ioSurfaceInitInFlight = false;
    }
  }

  Future<void> _disposeIOSurfaceTexture() async {
    final int? textureId = _ioSurfaceTextureId;
    if (textureId == null) return;
    _ioSurfaceTextureId = null;
    _ioSurfaceId = null;
    // Detach from engine.
    // 先从引擎侧解除绑定，再释放纹理。
    try {
      await widget.bridge.engineSetRenderTargetIOSurface(
        iosurfaceId: 0,
        width: 0,
        height: 0,
      );
    } catch (_) {}
    await widget.bridge.disposeIOSurfaceTexture(textureId: textureId);
  }

  // --- SurfaceTexture zero-copy mode (Android) / Android SurfaceTexture 零拷贝模式 ---

  Future<void> _ensureSurfaceTexture() async {
    if (!widget.active ||
        _surfaceTextureInitInFlight ||
        _surfaceWidth <= 0 ||
        _surfaceHeight <= 0) {
      return;
    }

    // Check if we need to resize.
    // 检查是否需要 resize。
    if (_surfaceTextureId != null) {
      if (_frameWidth == _surfaceWidth && _frameHeight == _surfaceHeight) {
        return; // Already at correct size
      }
      // Resize needed / 需要调整尺寸
      _surfaceTextureInitInFlight = true;
      try {
        final result = await widget.bridge.resizeSurfaceTexture(
          textureId: _surfaceTextureId!,
          width: _surfaceWidth,
          height: _surfaceHeight,
        );
        if (result != null && mounted) {
          _frameWidth = _surfaceWidth;
          _frameHeight = _surfaceHeight;
          widget.onLog?.call(
            'SurfaceTexture resized: ${_surfaceWidth}x$_surfaceHeight',
          );
        }
      } finally {
        _surfaceTextureInitInFlight = false;
      }
      return;
    }

    _surfaceTextureInitInFlight = true;
    try {
      final result = await widget.bridge.createSurfaceTexture(
        width: _surfaceWidth,
        height: _surfaceHeight,
      );

      if (!mounted) return;
      if (result == null) {
        widget.onLog?.call(
          'SurfaceTexture unavailable, falling back to legacy texture mode',
        );
        await _ensureTexture();
        return;
      }

      final int textureId = result['textureId'] as int;

      // The SurfaceTexture/Surface is already passed to C++ via JNI
      // in the Kotlin plugin's createSurfaceTexture method.
      // SurfaceTexture / Surface 已经在 Kotlin 插件的 createSurfaceTexture
      // 流程中通过 JNI 传给 C++。
      // The C++ engine_tick() will auto-detect the pending ANativeWindow
      // and attach it as the EGL WindowSurface render target.
      // C++ 的 engine_tick() 会自动探测待绑定的 ANativeWindow，
      // 并把它接成 EGL WindowSurface 渲染目标。

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _surfaceTextureId = textureId;
        _textureId = null; // Dispose legacy texture if any
        _frameImage = null;
        _frameWidth = _surfaceWidth;
        _frameHeight = _surfaceHeight;
      });
      previousImage?.dispose();
      widget.onLog?.call(
        'SurfaceTexture zero-copy mode enabled (textureId=$textureId)',
      );
    } finally {
      _surfaceTextureInitInFlight = false;
    }
  }

  Future<void> _disposeSurfaceTexture() async {
    final int? textureId = _surfaceTextureId;
    if (textureId == null) return;
    _surfaceTextureId = null;
    await widget.bridge.disposeSurfaceTexture(textureId: textureId);
  }

  // --- Legacy texture mode / 旧版纹理模式 ---

  Future<void> _ensureTexture() async {
    if (!widget.active || _textureInitInFlight || _textureId != null) {
      return;
    }

    _textureInitInFlight = true;
    try {
      final int? textureId = await widget.bridge.createTexture(
        width: _surfaceWidth > 0 ? _surfaceWidth : 1,
        height: _surfaceHeight > 0 ? _surfaceHeight : 1,
      );

      if (!mounted) {
        if (textureId != null) {
          await widget.bridge.disposeTexture(textureId: textureId);
        }
        return;
      }
      if (textureId == null) {
        widget.onLog?.call('texture unavailable, fallback to software mode');
        return;
      }

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _textureId = textureId;
        _frameImage = null;
      });
      previousImage?.dispose();
      widget.onLog?.call('texture mode enabled (id=$textureId)');
    } finally {
      _textureInitInFlight = false;
    }
  }

  Future<void> _disposeTexture() async {
    final int? textureId = _textureId;
    if (textureId == null) {
      return;
    }
    _textureId = null;
    await widget.bridge.disposeTexture(textureId: textureId);
  }

  // --- Frame polling / 帧轮询 ---
  Future<void> _pollFrame({bool? externalRendered}) async {
    if (!widget.active || _frameInFlight) {
      return;
    }

    _frameInFlight = true;
    try {
      // IOSurface / SurfaceTexture zero-copy mode: just notify Flutter.
      // IOSurface / SurfaceTexture 零拷贝模式下只需通知 Flutter，无需传输像素。
      if (_ioSurfaceTextureId != null || _surfaceTextureId != null) {
        // When the caller already checked the flag (external tick-driven
        // mode), use that value directly to avoid the double-read problem.
        // 如果外部 tick 循环已经检查过 rendered 标记，就直接复用该值，
        // 避免重复读取。
        // engineGetFrameRenderedFlag resets the flag on read, so a second
        // read would always return false.
        // 因为 engineGetFrameRenderedFlag 会在读取时清零，第二次读取一定为 false。
        final bool rendered =
            externalRendered ??
            await widget.bridge.engineGetFrameRenderedFlag();
        if (rendered && mounted) {
          final int activeZeroCopyTextureId =
              _ioSurfaceTextureId ?? _surfaceTextureId!;
          await widget.bridge.notifyFrameAvailable(
            textureId: activeZeroCopyTextureId,
          );
        }
        return;
      }

      // Legacy path: read pixels.
      // 旧路径会主动读回像素。
      final EngineFrameData? frameData = await widget.bridge.engineReadFrame();
      if (frameData == null) {
        return;
      }
      final EngineFrameInfo frameInfo = frameData.info;
      final Uint8List rgbaData = frameData.pixels;

      if (frameInfo.width <= 0 || frameInfo.height <= 0) {
        return;
      }
      if (frameInfo.frameSerial == _lastFrameSerial) {
        return;
      }

      final int expectedMinLength = frameInfo.strideBytes * frameInfo.height;
      if (expectedMinLength <= 0 || rgbaData.length < expectedMinLength) {
        _reportError(
          'engine_read_frame_rgba returned insufficient data: '
          'len=${rgbaData.length}, required=$expectedMinLength',
        );
        return;
      }

      final int? textureId = _textureId;
      if (textureId != null) {
        final bool updated = await widget.bridge.updateTextureRgba(
          textureId: textureId,
          rgba: rgbaData,
          width: frameInfo.width,
          height: frameInfo.height,
          rowBytes: frameInfo.strideBytes,
        );
        if (!updated) {
          _reportError(
            'updateTextureRgba failed, fallback to software mode: '
            '${widget.bridge.engineGetLastError()}',
          );
          await _disposeTexture();
        } else if (mounted) {
          setState(() {
            _frameWidth = frameInfo.width;
            _frameHeight = frameInfo.height;
            _lastFrameSerial = frameInfo.frameSerial;
          });
          return;
        }
      }

      final ui.Image nextImage = await _decodeRgbaFrame(
        rgbaData,
        width: frameInfo.width,
        height: frameInfo.height,
        rowBytes: frameInfo.strideBytes,
      );

      if (!mounted) {
        nextImage.dispose();
        return;
      }

      final ui.Image? previousImage = _frameImage;
      setState(() {
        _frameImage = nextImage;
        _frameWidth = frameInfo.width;
        _frameHeight = frameInfo.height;
        _lastFrameSerial = frameInfo.frameSerial;
      });
      previousImage?.dispose();
    } catch (error) {
      _reportError('surface poll failed: $error');
    } finally {
      _frameInFlight = false;
    }
  }

  Future<ui.Image> _decodeRgbaFrame(
    Uint8List pixels, {
    required int width,
    required int height,
    required int rowBytes,
  }) {
    final Completer<ui.Image> completer = Completer<ui.Image>();
    ui.decodeImageFromPixels(pixels, width, height, ui.PixelFormat.rgba8888, (
      ui.Image image,
    ) {
      completer.complete(image);
    }, rowBytes: rowBytes);
    return completer.future;
  }

  void _reportError(String message) {
    widget.onError?.call(message);
  }

  Widget _buildTextureView(int textureId) {
    // Use physical pixel dimensions, but convert them to logical pixels
    // for layout calculations.
    // 使用物理像素尺寸，但为了布局与宽高比计算会换算成逻辑像素。
    // The Texture widget renders at full physical resolution regardless of
    // the SizedBox logical size, so this only affects aspect ratio calculation.
    // Texture 组件始终按物理分辨率绘制，因此这里只影响宽高比计算。
    final double dpr = _devicePixelRatio > 0 ? _devicePixelRatio : 1.0;
    final int physW = _frameWidth > 0
        ? _frameWidth
        : (_surfaceWidth > 0 ? _surfaceWidth : 1);
    final int physH = _frameHeight > 0
        ? _frameHeight
        : (_surfaceHeight > 0 ? _surfaceHeight : 1);
    final double logicalW = physW / dpr;
    final double logicalH = physH / dpr;
    return FittedBox(
      fit: BoxFit.contain,
      child: SizedBox(
        width: logicalW,
        height: logicalH,
        child: Texture(textureId: textureId, filterQuality: FilterQuality.none),
      ),
    );
  }

  /// Convert Flutter's button bitmask to engine button index.
  /// 把 Flutter 的鼠标按键位图转换成引擎侧按钮编号。
  /// Flutter: kPrimaryButton=1, kSecondaryButton=2, kMiddleMouseButton=4
  /// Flutter：kPrimaryButton=1，kSecondaryButton=2，kMiddleMouseButton=4
  /// Engine:  0=left, 1=right, 2=middle
  /// 引擎：0=左键，1=右键，2=中键
  static int _flutterButtonsToEngineButton(int buttons) {
    if (buttons & kSecondaryButton != 0) return 1; // right
    if (buttons & kMiddleMouseButton != 0) return 2; // middle
    return 0; // left (default)
  }

  /// Build a TVP_SS_* compatible modifier bitmask from Flutter input state.
  /// 汇总 Flutter 当前键盘/鼠标状态，生成与 TVP_SS_* 对齐的修饰键位图。
  int _buildModifierMask({int buttons = 0}) {
    final keyboard = HardwareKeyboard.instance;
    int modifiers = 0;

    if (keyboard.isShiftPressed) {
      modifiers |= _modifierShift;
    }
    if (keyboard.isAltPressed) {
      modifiers |= _modifierAlt;
    }
    if (keyboard.isControlPressed) {
      modifiers |= _modifierCtrl;
    }
    if ((buttons & kPrimaryButton) != 0) {
      modifiers |= _modifierLeft;
    }
    if ((buttons & kSecondaryButton) != 0) {
      modifiers |= _modifierRight;
    }
    if ((buttons & kMiddleMouseButton) != 0) {
      modifiers |= _modifierMiddle;
    }

    return modifiers;
  }

  /// Compress Flutter text input into a single Unicode code point.
  /// 将 Flutter 的字符输入压缩成单个 Unicode code point。
  ///
  /// Only a single BMP scalar is accepted here to avoid forwarding
  /// combined characters, emoji, or control characters into the current
  /// 16-bit oriented TJS/TVP text-input path.
  /// 这里故意只接收单个 BMP 字符，避免把组合字符、emoji 或控制字符
  /// 误发给目前仍以 16-bit 字符为主的 TJS/TVP 输入链路。
  int? _tryGetTextInputCodepoint(KeyEvent event, int modifiers) {
    if ((modifiers & (_modifierAlt | _modifierCtrl)) != 0) {
      return null;
    }

    final String? character = event.character;
    if (character == null || character.isEmpty) {
      return null;
    }

    final List<int> runes = character.runes.toList(growable: false);
    if (runes.length != 1) {
      return null;
    }

    final int codepoint = runes.first;
    final bool isControl =
        (codepoint >= 0x00 && codepoint <= 0x1F) ||
        (codepoint >= 0x7F && codepoint <= 0x9F);
    if (isControl || codepoint > 0xFFFF) {
      return null;
    }

    return codepoint;
  }

  /// Translate Flutter keyboard events into the Windows-VK-style key codes
  /// expected by the current engine_api / EngineLoop contract.
  /// 将 Flutter 键盘事件转换为当前 engine_api / EngineLoop 约定使用的
  /// Windows VK 风格键码。
  int? _toEngineVirtualKeyCode(KeyEvent event) {
    final int? mapped = _logicalToVirtualKey[event.logicalKey];
    if (mapped != null) {
      return mapped;
    }

    final String label = event.logicalKey.keyLabel;
    if (label.length != 1) {
      return null;
    }

    final int rune = label.runes.single;
    if (rune >= 0x30 && rune <= 0x39) {
      return rune;
    }

    final int uppercase = (rune >= 0x61 && rune <= 0x7A) ? (rune - 0x20) : rune;
    if (uppercase >= 0x41 && uppercase <= 0x5A) {
      return uppercase;
    }

    switch (rune) {
      case 0x60:
        return 0xC0; // `
      case 0x2D:
        return 0xBD; // -
      case 0x3D:
        return 0xBB; // =
      case 0x5B:
        return 0xDB; // [
      case 0x5D:
        return 0xDD; // ]
      case 0x5C:
        return 0xDC; // backslash
      case 0x3B:
        return 0xBA; // ;
      case 0x27:
        return 0xDE; // '
      case 0x2C:
        return 0xBC; // ,
      case 0x2E:
        return 0xBE; // .
      case 0x2F:
        return 0xBF; // /
      default:
        return null;
    }
  }

  EngineInputEventData _buildPointerEventData({
    required int type,
    required PointerEvent event,
    double? deltaX,
    double? deltaY,
  }) {
    // Map pointer position from Listener logical coordinates
    // to engine-surface physical-pixel coordinates.
    // 将 Listener 的逻辑坐标映射到引擎 surface 的物理像素坐标。
    //
    // Listener's localPosition is in logical pixels. The engine's
    // EGL surface is sized in physical pixels (logical * dpr), so
    // multiply by dpr to get surface coordinates.
    // Listener.localPosition 使用逻辑像素，而引擎 EGL surface 使用
    // 物理像素（logical * dpr），因此这里需要乘以 dpr。
    //
    // The C++ side (DrawDevice::TransformToPrimaryLayerManager)
    // then maps these surface coordinates to primary-layer coordinates.
    // C++ 侧随后会通过 DrawDevice::TransformToPrimaryLayerManager
    // 再把 surface 坐标转换成主 Layer 坐标。
    final double dpr = _devicePixelRatio > 0 ? _devicePixelRatio : 1.0;
    return EngineInputEventData(
      type: type,
      timestampMicros: event.timeStamp.inMicroseconds,
      x: event.localPosition.dx * dpr,
      y: event.localPosition.dy * dpr,
      deltaX: (deltaX ?? event.delta.dx) * dpr,
      deltaY: (deltaY ?? event.delta.dy) * dpr,
      pointerId: event.pointer,
      button: _flutterButtonsToEngineButton(event.buttons),
      modifiers: _buildModifierMask(buttons: event.buttons),
    );
  }

  void _sendPointer({
    required int type,
    required PointerEvent event,
    double? deltaX,
    double? deltaY,
  }) {
    if (!widget.active) {
      return;
    }
    unawaited(
      _sendInputEvent(
        _buildPointerEventData(
          type: type,
          event: event,
          deltaX: deltaX,
          deltaY: deltaY,
        ),
      ),
    );
  }

  void _sendCoalescedPointerMove(PointerEvent event) {
    if (!widget.active) {
      return;
    }
    _pendingPointerMoveEvent = _buildPointerEventData(
      type: EngineInputEventType.pointerMove,
      event: event,
    );
    if (_pointerMoveFlushScheduled) {
      return;
    }
    _pointerMoveFlushScheduled = true;
    SchedulerBinding.instance.scheduleFrameCallback((_) {
      _pointerMoveFlushScheduled = false;
      if (!mounted || !widget.active) {
        _pendingPointerMoveEvent = null;
        return;
      }
      final EngineInputEventData? pending = _pendingPointerMoveEvent;
      _pendingPointerMoveEvent = null;
      if (pending != null) {
        unawaited(_sendInputEvent(pending));
      }
    });
  }

  void _ensureSurfaceSizeIfNeeded(Size size, double devicePixelRatio) {
    if (!widget.active || !size.isFinite) {
      return;
    }
    final double normalizedDpr = devicePixelRatio <= 0 ? 1.0 : devicePixelRatio;
    if ((_lastRequestedLogicalSize.width - size.width).abs() < 0.01 &&
        (_lastRequestedLogicalSize.height - size.height).abs() < 0.01 &&
        (_lastRequestedDpr - normalizedDpr).abs() < 0.001) {
      return;
    }
    _lastRequestedLogicalSize = size;
    _lastRequestedDpr = normalizedDpr;
    unawaited(_ensureSurfaceSize(size, normalizedDpr));
  }

  Future<void> _sendInputEvent(EngineInputEventData event) async {
    final int result = await widget.bridge.engineSendInput(event);
    if (result != 0) {
      _reportError(
        'engine_send_input failed: result=$result, error=${widget.bridge.engineGetLastError()}',
      );
    }
  }

  KeyEventResult _onKeyEvent(FocusNode node, KeyEvent event) {
    if (!widget.active) {
      return KeyEventResult.ignored;
    }

    final bool isDown = event is KeyDownEvent;
    final bool isUp = event is KeyUpEvent;
    if (!isDown && !isUp) {
      return KeyEventResult.ignored;
    }

    final int type = isDown
        ? EngineInputEventType.keyDown
        : EngineInputEventType.keyUp;
    final int? keyCode = _toEngineVirtualKeyCode(event);
    final int modifiers = _buildModifierMask();
    if (keyCode != null) {
      unawaited(
        _sendInputEvent(
          EngineInputEventData(
            type: type,
            timestampMicros: event.timeStamp.inMicroseconds,
            keyCode: keyCode,
            modifiers: modifiers,
          ),
        ),
      );
    }

    if (isDown) {
      final int? textCodepoint = _tryGetTextInputCodepoint(event, modifiers);
      if (textCodepoint != null) {
        unawaited(
          _sendInputEvent(
            EngineInputEventData(
              type: EngineInputEventType.textInput,
              timestampMicros: event.timeStamp.inMicroseconds,
              unicodeCodepoint: textCodepoint,
              modifiers: modifiers,
            ),
          ),
        );
      }
    }

    if (isDown &&
        (event.logicalKey == LogicalKeyboardKey.escape ||
            event.logicalKey == LogicalKeyboardKey.goBack)) {
      unawaited(
        _sendInputEvent(
          EngineInputEventData(
            type: EngineInputEventType.back,
            timestampMicros: event.timeStamp.inMicroseconds,
            keyCode: keyCode ?? 0x1B,
            modifiers: modifiers,
          ),
        ),
      );
    }

    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    final int? activeTextureId =
        _ioSurfaceTextureId ?? _surfaceTextureId ?? _textureId;

    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        final Size size = Size(constraints.maxWidth, constraints.maxHeight);
        final double dpr = MediaQuery.of(context).devicePixelRatio;
        _ensureSurfaceSizeIfNeeded(size, dpr);

        return Focus(
          focusNode: _focusNode,
          autofocus: true,
          onKeyEvent: _onKeyEvent,
          child: Listener(
            behavior: HitTestBehavior.opaque,
            onPointerDown: (event) {
              _focusNode.requestFocus();
              _sendPointer(
                type: EngineInputEventType.pointerDown,
                event: event,
              );
            },
            onPointerMove: (event) {
              _sendCoalescedPointerMove(event);
            },
            onPointerUp: (event) {
              _sendPointer(type: EngineInputEventType.pointerUp, event: event);
            },
            onPointerHover: (event) {
              _sendCoalescedPointerMove(event);
            },
            onPointerSignal: (PointerSignalEvent signal) {
              if (signal is PointerScrollEvent) {
                _sendPointer(
                  type: EngineInputEventType.pointerScroll,
                  event: signal,
                  deltaX: signal.scrollDelta.dx,
                  deltaY: signal.scrollDelta.dy,
                );
              }
            },
            child: Container(
              color: Colors.black,
              child: Stack(
                fit: StackFit.expand,
                children: [
                  if (activeTextureId != null)
                    _buildTextureView(activeTextureId)
                  else if (_frameImage == null)
                    const SizedBox.shrink()
                  else
                    RawImage(
                      image: _frameImage,
                      fit: BoxFit.contain,
                      filterQuality: FilterQuality.none,
                    ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}
