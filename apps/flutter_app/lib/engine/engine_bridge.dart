abstract interface class EngineBridge {
  bool get isFfiAvailable;
  String get ffiInitializationError;

  Future<String?> getPlatformVersion();
  Future<String> getBackendDescription();

  Future<int> engineCreate({String? writablePath, String? cachePath});
  Future<int> engineDestroy();
  Future<int> engineOpenGame(String gameRootPath, {String? startupScript});
  Future<int> engineTick({int deltaMs = 16});
  Future<int> enginePause();
  Future<int> engineResume();
  Future<int> engineSetOption({required String key, required String value});
  Future<int> engineRuntimeApiVersion();

  String engineGetLastError();
}

typedef EngineBridgeBuilder = EngineBridge Function({String? ffiLibraryPath});
