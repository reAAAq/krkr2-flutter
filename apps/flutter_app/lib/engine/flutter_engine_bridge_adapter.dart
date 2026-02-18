import 'dart:typed_data';

import 'package:flutter_engine_bridge/flutter_engine_bridge.dart'
    as plugin_bridge;

import 'engine_bridge.dart';

EngineBridge createEngineBridge({String? ffiLibraryPath}) {
  return FlutterEngineBridgeAdapter(ffiLibraryPath: ffiLibraryPath);
}

class FlutterEngineBridgeAdapter implements EngineBridge {
  FlutterEngineBridgeAdapter({String? ffiLibraryPath})
    : _delegate = plugin_bridge.FlutterEngineBridge(
        ffiLibraryPath: ffiLibraryPath,
      );

  final plugin_bridge.FlutterEngineBridge _delegate;

  @override
  bool get isFfiAvailable => _delegate.isFfiAvailable;

  @override
  String get ffiInitializationError => _delegate.ffiInitializationError;

  @override
  Future<String?> getPlatformVersion() => _delegate.getPlatformVersion();

  @override
  Future<String> getBackendDescription() => _delegate.getBackendDescription();

  @override
  Future<int> engineCreate({String? writablePath, String? cachePath}) {
    return _delegate.engineCreate(
      writablePath: writablePath,
      cachePath: cachePath,
    );
  }

  @override
  Future<int> engineDestroy() => _delegate.engineDestroy();

  @override
  Future<int> engineOpenGame(String gameRootPath, {String? startupScript}) {
    return _delegate.engineOpenGame(gameRootPath, startupScript: startupScript);
  }

  @override
  Future<int> engineTick({int deltaMs = 16}) {
    return _delegate.engineTick(deltaMs: deltaMs);
  }

  @override
  Future<int> enginePause() => _delegate.enginePause();

  @override
  Future<int> engineResume() => _delegate.engineResume();

  @override
  Future<int> engineSetOption({required String key, required String value}) {
    return _delegate.engineSetOption(key: key, value: value);
  }

  @override
  Future<int> engineSetSurfaceSize({required int width, required int height}) {
    return _delegate.engineSetSurfaceSize(width: width, height: height);
  }

  @override
  Future<EngineFrameInfo?> engineGetFrameDesc() async {
    final frame = await _delegate.engineGetFrameDesc();
    if (frame == null) {
      return null;
    }
    return EngineFrameInfo(
      width: frame.width,
      height: frame.height,
      strideBytes: frame.strideBytes,
      pixelFormat: frame.pixelFormat,
      frameSerial: frame.frameSerial,
    );
  }

  @override
  Future<Uint8List?> engineReadFrameRgba() {
    return _delegate.engineReadFrameRgba();
  }

  @override
  Future<EngineFrameData?> engineReadFrame() async {
    final plugin_bridge.EngineFrameData? frame = await _delegate
        .engineReadFrame();
    if (frame == null) {
      return null;
    }
    return EngineFrameData(
      info: EngineFrameInfo(
        width: frame.info.width,
        height: frame.info.height,
        strideBytes: frame.info.strideBytes,
        pixelFormat: frame.info.pixelFormat,
        frameSerial: frame.info.frameSerial,
      ),
      pixels: frame.pixels,
    );
  }

  @override
  Future<int> engineSendInput(EngineInputEventData event) {
    return _delegate.engineSendInput(
      plugin_bridge.EngineInputEventData(
        type: event.type,
        timestampMicros: event.timestampMicros,
        x: event.x,
        y: event.y,
        deltaX: event.deltaX,
        deltaY: event.deltaY,
        pointerId: event.pointerId,
        button: event.button,
        keyCode: event.keyCode,
        modifiers: event.modifiers,
        unicodeCodepoint: event.unicodeCodepoint,
      ),
    );
  }

  @override
  Future<int> engineRuntimeApiVersion() => _delegate.engineRuntimeApiVersion();

  @override
  String engineGetLastError() => _delegate.engineGetLastError();
}
