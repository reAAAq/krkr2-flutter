import 'dart:async';
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
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
enum EngineSurfaceMode {
  /// IOSurface zero-copy mode (macOS only, highest performance).
  iosurface,
  /// Flutter Texture with RGBA pixel upload (cross-platform, slower).
  texture,
  /// Pure software decoding via RawImage (slowest, always works).
  software,
}

class EngineSurface extends StatefulWidget {
  const EngineSurface({
    super.key,
    required this.bridge,
    required this.active,
    this.surfaceMode = EngineSurfaceMode.iosurface,
    this.pollInterval = const Duration(milliseconds: 16),
    this.onLog,
    this.onError,
  });

  final EngineBridge bridge;
  final bool active;
  final EngineSurfaceMode surfaceMode;
  final Duration pollInterval;
  final ValueChanged<String>? onLog;
  final ValueChanged<String>? onError;

  @override
  State<EngineSurface> createState() => _EngineSurfaceState();
}

class _EngineSurfaceState extends State<EngineSurface> {
  final FocusNode _focusNode = FocusNode(debugLabel: 'engine-surface-focus');
  Timer? _frameTimer;
  bool _frameInFlight = false;
  bool _textureInitInFlight = false;
  ui.Image? _frameImage;
  int _lastFrameSerial = -1;
  int _surfaceWidth = 0;
  int _surfaceHeight = 0;
  int _frameWidth = 0;
  int _frameHeight = 0;
  double _devicePixelRatio = 1.0;

  // Legacy texture mode
  int? _textureId;

  // IOSurface zero-copy mode
  int? _ioSurfaceTextureId;
  int? _ioSurfaceId;
  bool _ioSurfaceInitInFlight = false;

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
        oldWidget.surfaceMode != widget.surfaceMode) {
      _reconcilePolling();
    }
  }

  @override
  void dispose() {
    _frameTimer?.cancel();
    _frameImage?.dispose();
    unawaited(_disposeAllTextures());
    _focusNode.dispose();
    super.dispose();
  }

  Future<void> _disposeAllTextures() async {
    await _disposeTexture();
    await _disposeIOSurfaceTexture();
  }

  void _reconcilePolling() {
    if (!widget.active) {
      _frameTimer?.cancel();
      _frameTimer = null;
      return;
    }

    _frameTimer ??= Timer.periodic(widget.pollInterval, (_) {
      unawaited(_pollFrame());
    });
    unawaited(_pollFrame());
  }

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
      case EngineSurfaceMode.iosurface:
        await _ensureIOSurfaceTexture();
        break;
      case EngineSurfaceMode.texture:
        await _ensureTexture();
        break;
      case EngineSurfaceMode.software:
        // No texture needed
        break;
    }
  }

  // --- IOSurface zero-copy mode ---

  Future<void> _ensureIOSurfaceTexture() async {
    if (!widget.active ||
        _ioSurfaceInitInFlight ||
        _surfaceWidth <= 0 ||
        _surfaceHeight <= 0) {
      return;
    }

    // Check if we need to resize
    if (_ioSurfaceTextureId != null) {
      if (_frameWidth == _surfaceWidth && _frameHeight == _surfaceHeight) {
        return; // Already at correct size
      }
      // Resize needed
      _ioSurfaceInitInFlight = true;
      try {
        final result = await widget.bridge.resizeIOSurfaceTexture(
          textureId: _ioSurfaceTextureId!,
          width: _surfaceWidth,
          height: _surfaceHeight,
        );
        if (result != null && mounted) {
          final int newIOSurfaceId = result['ioSurfaceID'] as int;
          // Tell the engine about the new IOSurface
          final int setResult =
              await widget.bridge.engineSetRenderTargetIOSurface(
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
        // Fall back to legacy texture mode
        await _ensureTexture();
        return;
      }

      final int textureId = result['textureId'] as int;
      final int ioSurfaceId = result['ioSurfaceID'] as int;

      // Tell the C++ engine to render to this IOSurface
      final int setResult =
          await widget.bridge.engineSetRenderTargetIOSurface(
        iosurfaceId: ioSurfaceId,
        width: _surfaceWidth,
        height: _surfaceHeight,
      );

      if (setResult != 0) {
        _reportError(
          'engine_set_render_target_iosurface failed: $setResult, '
          'error=${widget.bridge.engineGetLastError()}',
        );
        // Clean up and fall back
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
    // Detach from engine
    try {
      await widget.bridge.engineSetRenderTargetIOSurface(
        iosurfaceId: 0,
        width: 0,
        height: 0,
      );
    } catch (_) {}
    await widget.bridge.disposeIOSurfaceTexture(textureId: textureId);
  }

  // --- Legacy texture mode ---

  Future<void> _ensureTexture() async {
    if (!widget.active ||
        _textureInitInFlight ||
        _textureId != null) {
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

  // --- Frame polling ---

  Future<void> _pollFrame() async {
    if (!widget.active || _frameInFlight) {
      return;
    }

    _frameInFlight = true;
    try {
      // IOSurface zero-copy mode: just notify Flutter, no pixel transfer
      if (_ioSurfaceTextureId != null) {
        final bool rendered =
            await widget.bridge.engineGetFrameRenderedFlag();
        if (rendered && mounted) {
          await widget.bridge.notifyFrameAvailable(
            textureId: _ioSurfaceTextureId!,
          );
          setState(() {
            _lastFrameSerial += 1;
          });
        }
        return;
      }

      // Legacy path: read pixels
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
    final int width = _frameWidth > 0
        ? _frameWidth
        : (_surfaceWidth > 0 ? _surfaceWidth : 1);
    final int height = _frameHeight > 0
        ? _frameHeight
        : (_surfaceHeight > 0 ? _surfaceHeight : 1);
    return ClipRect(
      child: FittedBox(
        fit: BoxFit.cover,
        child: SizedBox(
          width: width.toDouble(),
          height: height.toDouble(),
          child: Texture(textureId: textureId),
        ),
      ),
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
        EngineInputEventData(
          type: type,
          timestampMicros: event.timeStamp.inMicroseconds,
          x: event.localPosition.dx * _devicePixelRatio,
          y: event.localPosition.dy * _devicePixelRatio,
          deltaX: (deltaX ?? event.delta.dx) * _devicePixelRatio,
          deltaY: (deltaY ?? event.delta.dy) * _devicePixelRatio,
          pointerId: event.pointer,
          button: event.buttons,
        ),
      ),
    );
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
    final int keyCode = event.logicalKey.keyId & 0xFFFFFFFF;
    unawaited(
      _sendInputEvent(
        EngineInputEventData(
          type: type,
          timestampMicros: event.timeStamp.inMicroseconds,
          keyCode: keyCode,
        ),
      ),
    );

    if (isDown &&
        (event.logicalKey == LogicalKeyboardKey.escape ||
            event.logicalKey == LogicalKeyboardKey.goBack)) {
      unawaited(
        _sendInputEvent(
          EngineInputEventData(
            type: EngineInputEventType.back,
            timestampMicros: event.timeStamp.inMicroseconds,
            keyCode: keyCode,
          ),
        ),
      );
    }

    return KeyEventResult.handled;
  }

  String get _modeLabel {
    if (_ioSurfaceTextureId != null) return 'iosurface(id:$_ioSurfaceId)';
    if (_textureId != null) return 'texture';
    return 'software';
  }

  @override
  Widget build(BuildContext context) {
    final int? activeTextureId = _ioSurfaceTextureId ?? _textureId;

    return AspectRatio(
      aspectRatio: 16 / 9,
      child: LayoutBuilder(
        builder: (BuildContext context, BoxConstraints constraints) {
          final Size size = Size(constraints.maxWidth, constraints.maxHeight);
          final double dpr = MediaQuery.of(context).devicePixelRatio;
          unawaited(_ensureSurfaceSize(size, dpr));

          return Focus(
            focusNode: _focusNode,
            autofocus: true,
            onKeyEvent: _onKeyEvent,
            child: Listener(
              onPointerDown: (event) {
                _focusNode.requestFocus();
                _sendPointer(
                  type: EngineInputEventType.pointerDown,
                  event: event,
                );
              },
              onPointerMove: (event) {
                _sendPointer(
                  type: EngineInputEventType.pointerMove,
                  event: event,
                );
              },
              onPointerUp: (event) {
                _sendPointer(
                  type: EngineInputEventType.pointerUp,
                  event: event,
                );
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
                decoration: BoxDecoration(
                  color: Colors.black,
                  border: Border.all(
                    color: Theme.of(context).colorScheme.outlineVariant,
                  ),
                  borderRadius: BorderRadius.circular(8),
                ),
                clipBehavior: Clip.antiAlias,
                child: Stack(
                  fit: StackFit.expand,
                  children: [
                    if (activeTextureId != null)
                      _buildTextureView(activeTextureId)
                    else if (_frameImage == null)
                      const Center(
                        child: Text(
                          'Engine Surface (No frame)',
                          style: TextStyle(color: Colors.white70),
                        ),
                      )
                    else
                      RawImage(
                        image: _frameImage,
                        fit: BoxFit.cover,
                        filterQuality: FilterQuality.none,
                      ),
                    Positioned(
                      left: 10,
                      top: 10,
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 8,
                          vertical: 4,
                        ),
                        decoration: BoxDecoration(
                          color: Colors.black54,
                          borderRadius: BorderRadius.circular(4),
                        ),
                        child: Text(
                          'surface:${_surfaceWidth}x$_surfaceHeight  '
                          'frame:${_frameWidth}x$_frameHeight  '
                          '#$_lastFrameSerial  '
                          '$_modeLabel',
                          style: const TextStyle(
                            color: Colors.white,
                            fontSize: 12,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          );
        },
      ),
    );
  }
}
