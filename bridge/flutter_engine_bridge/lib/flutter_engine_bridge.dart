import 'flutter_engine_bridge_platform_interface.dart';
import 'src/ffi/engine_bindings.dart';
import 'src/ffi/engine_ffi.dart';

class FlutterEngineBridge {
  FlutterEngineBridge({
    FlutterEngineBridgePlatform? platform,
    EngineFfiBridge? ffiBridge,
    String? ffiLibraryPath,
  }) : _platform = platform ?? FlutterEngineBridgePlatform.instance,
       _ffiBridge = ffiBridge ?? EngineFfiBridge.tryCreate(libraryPath: ffiLibraryPath);

  final FlutterEngineBridgePlatform _platform;
  final EngineFfiBridge? _ffiBridge;
  String _fallbackLastError = '';

  bool get isFfiAvailable => _ffiBridge != null;

  Future<String?> getPlatformVersion() {
    return _platform.getPlatformVersion();
  }

  Future<String> getBackendDescription() async {
    if (isFfiAvailable) {
      return 'ffi';
    }
    final platformVersion = await _platform.getPlatformVersion();
    final versionLabel = platformVersion ?? 'unknown';
    return 'method_channel($versionLabel)';
  }

  Future<int> engineCreate({String? writablePath, String? cachePath}) async {
    return _withFfiCall(
      apiName: 'engine_create',
      call: (ffi) => ffi.create(writablePath: writablePath, cachePath: cachePath),
    );
  }

  Future<int> engineDestroy() async {
    return _withFfiCall(
      apiName: 'engine_destroy',
      call: (ffi) => ffi.destroy(),
    );
  }

  Future<int> engineOpenGame(String gameRootPath, {String? startupScript}) async {
    return _withFfiCall(
      apiName: 'engine_open_game',
      call: (ffi) => ffi.openGame(gameRootPath, startupScript: startupScript),
    );
  }

  Future<int> engineTick({int deltaMs = 16}) async {
    return _withFfiCall(
      apiName: 'engine_tick',
      call: (ffi) => ffi.tick(deltaMs: deltaMs),
    );
  }

  Future<int> engineRuntimeApiVersion() async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError =
          'FFI unavailable for engine_get_runtime_api_version. '
          'MethodChannel fallback active ($versionLabel).';
      return kEngineResultNotSupported;
    }

    final resultOrVersion = ffi.runtimeApiVersion();
    if (resultOrVersion < 0) {
      _fallbackLastError = ffi.lastError();
      return resultOrVersion;
    }

    _fallbackLastError = '';
    return resultOrVersion;
  }

  String engineGetLastError() {
    final ffi = _ffiBridge;
    if (ffi == null) {
      return _fallbackLastError;
    }
    return ffi.lastError();
  }

  Future<int> _withFfiCall({
    required String apiName,
    required int Function(EngineFfiBridge ffi) call,
    int fallbackResult = kEngineResultNotSupported,
  }) async {
    final ffi = _ffiBridge;
    if (ffi == null) {
      final platformVersion = await _platform.getPlatformVersion();
      final versionLabel = platformVersion ?? 'unknown';
      _fallbackLastError =
          'FFI unavailable for $apiName. MethodChannel fallback active ($versionLabel).';
      return fallbackResult;
    }

    final result = call(ffi);
    if (result != kEngineResultOk) {
      _fallbackLastError = ffi.lastError();
    } else {
      _fallbackLastError = '';
    }
    return result;
  }
}
