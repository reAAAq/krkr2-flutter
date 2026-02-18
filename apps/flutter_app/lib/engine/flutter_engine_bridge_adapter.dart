import 'package:flutter_engine_bridge/flutter_engine_bridge.dart';

import 'engine_bridge.dart';

EngineBridge createEngineBridge({String? ffiLibraryPath}) {
  return FlutterEngineBridgeAdapter(ffiLibraryPath: ffiLibraryPath);
}

class FlutterEngineBridgeAdapter implements EngineBridge {
  FlutterEngineBridgeAdapter({String? ffiLibraryPath})
    : _delegate = FlutterEngineBridge(ffiLibraryPath: ffiLibraryPath);

  final FlutterEngineBridge _delegate;

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
  Future<int> engineRuntimeApiVersion() => _delegate.engineRuntimeApiVersion();

  @override
  String engineGetLastError() => _delegate.engineGetLastError();
}
