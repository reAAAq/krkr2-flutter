import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';

import '../engine/engine_bridge.dart';

const String _nativeSurfaceViewType =
    'flutter_engine_bridge/native_engine_surface';
const int _inputPointerDown = 1;
const int _inputPointerUp = 3;

class EngineNativeSurface extends StatefulWidget {
  const EngineNativeSurface({
    super.key,
    required this.bridge,
    required this.active,
    this.onLog,
    this.onError,
  });

  final EngineBridge bridge;
  final bool active;
  final ValueChanged<String>? onLog;
  final ValueChanged<String>? onError;

  @override
  State<EngineNativeSurface> createState() => _EngineNativeSurfaceState();
}

class _EngineNativeSurfaceState extends State<EngineNativeSurface> {
  static const MethodChannel _pluginChannel = MethodChannel(
    'flutter_engine_bridge',
  );

  Timer? _windowHandlePollTimer;
  Timer? _nativeInputPollTimer;
  bool _nativeHandleQueryInFlight = false;
  bool _nativeInputDrainInFlight = false;
  bool _attachInFlight = false;
  bool _attached = false;
  bool _attachedAsNativeView = false;

  int _surfaceWidth = 0;
  int _surfaceHeight = 0;
  int? _nativeViewHandle;
  int? _nativeWindowHandle;
  int? _platformViewId;
  double _devicePixelRatio = 1.0;

  @override
  void initState() {
    super.initState();
    _reconcileActiveState();
  }

  @override
  void didUpdateWidget(covariant EngineNativeSurface oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.bridge != widget.bridge) {
      unawaited(_detachIfNeeded());
      _nativeViewHandle = null;
      _nativeWindowHandle = null;
    }
    if (!widget.active && oldWidget.active) {
      unawaited(_detachIfNeeded());
    }
    if (oldWidget.active != widget.active ||
        oldWidget.bridge != widget.bridge) {
      _reconcileActiveState();
    }
  }

  @override
  void dispose() {
    _windowHandlePollTimer?.cancel();
    _nativeInputPollTimer?.cancel();
    unawaited(_detachIfNeeded());
    super.dispose();
  }

  void _reconcileActiveState() {
    if (!widget.active || !Platform.isMacOS) {
      _windowHandlePollTimer?.cancel();
      _windowHandlePollTimer = null;
      _nativeInputPollTimer?.cancel();
      _nativeInputPollTimer = null;
      return;
    }

    _windowHandlePollTimer ??= Timer.periodic(
      const Duration(milliseconds: 300),
      (_) {
        unawaited(_ensureNativeHandles());
        unawaited(_tryAttach());
      },
    );
    _nativeInputPollTimer ??= Timer.periodic(const Duration(milliseconds: 16), (
      _,
    ) {
      unawaited(_drainNativePointerEvents());
    });

    unawaited(_ensureNativeHandles());
    unawaited(_tryAttach());
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
      'native surface resized: ${width}x$height (dpr=${_devicePixelRatio.toStringAsFixed(2)})',
    );
  }

  Future<void> _ensureNativeHandles() async {
    if (!widget.active ||
        !Platform.isMacOS ||
        _nativeHandleQueryInFlight ||
        _nativeViewHandle != null) {
      return;
    }

    _nativeHandleQueryInFlight = true;
    try {
      final int? nativeViewHandle = await widget.bridge
          .engineGetHostNativeView();
      final int? nativeWindowHandle = await widget.bridge
          .engineGetHostNativeWindow();
      if (!mounted || !widget.active) {
        return;
      }
      final int? resolvedViewHandle =
          nativeViewHandle != null && nativeViewHandle != 0
          ? nativeViewHandle
          : null;
      final int? resolvedWindowHandle =
          nativeWindowHandle != null && nativeWindowHandle != 0
          ? nativeWindowHandle
          : null;

      if (resolvedViewHandle == null && resolvedWindowHandle == null) {
        return;
      }

      final bool hadWindowAttach = _attached && !_attachedAsNativeView;
      final bool hasNewNativeView = resolvedViewHandle != null;

      setState(() {
        _nativeViewHandle = resolvedViewHandle ?? _nativeViewHandle;
        _nativeWindowHandle = resolvedWindowHandle ?? _nativeWindowHandle;
      });
      if (resolvedViewHandle != null) {
        widget.onLog?.call(
          'native view handle ready: 0x${resolvedViewHandle.toRadixString(16)}',
        );
      }
      if (resolvedWindowHandle != null) {
        widget.onLog?.call(
          'native window handle ready: 0x${resolvedWindowHandle.toRadixString(16)}',
        );
      }

      // If we were attached via child window fallback, upgrade to NSView attach
      // as soon as a valid native view handle is available.
      if (hadWindowAttach && hasNewNativeView) {
        await _detachIfNeeded();
      }
      await _tryAttach();
    } catch (error) {
      _reportError('query native handles failed: $error');
    } finally {
      _nativeHandleQueryInFlight = false;
    }
  }

  Future<void> _tryAttach() async {
    if (!widget.active || !Platform.isMacOS || _attachInFlight || _attached) {
      return;
    }
    final int? viewId = _platformViewId;
    final int? nativeViewHandle = _nativeViewHandle;
    final int? windowHandle = _nativeWindowHandle;
    if (viewId == null) {
      return;
    }
    if ((nativeViewHandle == null || nativeViewHandle == 0) &&
        (windowHandle == null || windowHandle == 0)) {
      return;
    }

    _attachInFlight = true;
    try {
      if (nativeViewHandle != null && nativeViewHandle != 0) {
        await widget.bridge.attachNativeView(
          viewId: viewId,
          viewHandle: nativeViewHandle,
          windowHandle: windowHandle,
        );
      } else {
        await widget.bridge.attachNativeWindow(
          viewId: viewId,
          windowHandle: windowHandle!,
        );
      }
      if (!mounted) {
        return;
      }
      setState(() {
        _attached = true;
        _attachedAsNativeView =
            nativeViewHandle != null && nativeViewHandle != 0;
      });
      if (_attachedAsNativeView) {
        widget.onLog?.call('native view attached (viewId=$viewId)');
      } else {
        widget.onLog?.call('native window attached (viewId=$viewId)');
      }
    } catch (error) {
      _reportError('attach native surface failed: $error');
    } finally {
      _attachInFlight = false;
    }
  }

  Future<void> _detachIfNeeded() async {
    if (!_attached) {
      return;
    }
    final int? viewId = _platformViewId;
    _attached = false;
    final bool detachNativeView = _attachedAsNativeView;
    _attachedAsNativeView = false;
    if (viewId == null) {
      return;
    }
    try {
      if (detachNativeView) {
        await widget.bridge.detachNativeView(viewId: viewId);
        widget.onLog?.call('native view detached (viewId=$viewId)');
      } else {
        await widget.bridge.detachNativeWindow(viewId: viewId);
        widget.onLog?.call('native window detached (viewId=$viewId)');
      }
    } catch (error) {
      _reportError('detach native surface failed: $error');
    }
  }

  void _onPlatformViewCreated(int viewId) {
    setState(() {
      _platformViewId = viewId;
      _attached = false;
      _attachedAsNativeView = false;
    });
    unawaited(_tryAttach());
  }

  void _reportError(String message) {
    widget.onError?.call(message);
  }

  Future<void> _drainNativePointerEvents() async {
    if (!widget.active ||
        !_attachedAsNativeView ||
        _nativeInputDrainInFlight ||
        _platformViewId == null) {
      return;
    }
    _nativeInputDrainInFlight = true;
    try {
      final List<dynamic>? events = await _pluginChannel
          .invokeMethod<List<dynamic>>(
            'drainNativePointerEvents',
            <String, Object>{'viewId': _platformViewId!},
          );
      if (events == null || events.isEmpty) {
        return;
      }

      int? asInt(dynamic value) {
        if (value is int) return value;
        if (value is num) return value.toInt();
        return null;
      }

      double asDouble(dynamic value) {
        if (value is double) return value;
        if (value is num) return value.toDouble();
        return 0;
      }

      for (final dynamic rawEvent in events) {
        if (rawEvent is! Map) {
          continue;
        }
        final Map<dynamic, dynamic> args = rawEvent;

        final int? type = asInt(args['type']);
        if (type == null) {
          continue;
        }

        await _sendInputEvent(
          EngineInputEventData(
            type: type,
            timestampMicros: asInt(args['timestampMicros']) ?? 0,
            x: asDouble(args['x']),
            y: asDouble(args['y']),
            deltaX: asDouble(args['deltaX']),
            deltaY: asDouble(args['deltaY']),
            pointerId: asInt(args['pointerId']) ?? 0,
            button: asInt(args['button']) ?? 0,
          ),
        );
      }
    } finally {
      _nativeInputDrainInFlight = false;
    }
  }

  Future<void> _sendInputEvent(EngineInputEventData event) async {
    final int result = await widget.bridge.engineSendInput(event);
    if (result != 0) {
      _reportError(
        'engine_send_input failed: result=$result, error=${widget.bridge.engineGetLastError()}',
      );
      return;
    }
    if (event.type == _inputPointerDown || event.type == _inputPointerUp) {
      widget.onLog?.call(
        'native input forwarded: type=${event.type} pointer=${event.pointerId} x=${event.x.toStringAsFixed(1)} y=${event.y.toStringAsFixed(1)}',
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return AspectRatio(
      aspectRatio: 16 / 9,
      child: LayoutBuilder(
        builder: (BuildContext context, BoxConstraints constraints) {
          final Size size = Size(constraints.maxWidth, constraints.maxHeight);
          final double dpr = MediaQuery.of(context).devicePixelRatio;
          unawaited(_ensureSurfaceSize(size, dpr));
          if (widget.active && Platform.isMacOS) {
            unawaited(_ensureNativeHandles());
            unawaited(_tryAttach());
          }

          return Container(
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
                if (!Platform.isMacOS)
                  const Center(
                    child: Text(
                      'Native view is only available on macOS',
                      style: TextStyle(color: Colors.white70),
                    ),
                  )
                else
                  AppKitView(
                    viewType: _nativeSurfaceViewType,
                    onPlatformViewCreated: _onPlatformViewCreated,
                    hitTestBehavior: PlatformViewHitTestBehavior.opaque,
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
                      'view:${_platformViewId ?? "-"}  '
                      'nativeView:${_nativeViewHandle != null ? "0x${_nativeViewHandle!.toRadixString(16)}" : "-"}  '
                      'handle:${_nativeWindowHandle != null ? "0x${_nativeWindowHandle!.toRadixString(16)}" : "-"}  '
                      '${_attached ? (_attachedAsNativeView ? "attached:view" : "attached:window") : "pending"}',
                      style: const TextStyle(color: Colors.white, fontSize: 12),
                    ),
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}
