import 'dart:typed_data';

class EngineFrameInfo {
  const EngineFrameInfo({
    required this.width,
    required this.height,
    required this.strideBytes,
    required this.pixelFormat,
    required this.frameSerial,
  });

  final int width;
  final int height;
  final int strideBytes;
  final int pixelFormat;
  final int frameSerial;
}

class EngineInputEventData {
  const EngineInputEventData({
    required this.type,
    this.timestampMicros = 0,
    this.x = 0,
    this.y = 0,
    this.deltaX = 0,
    this.deltaY = 0,
    this.pointerId = 0,
    this.button = 0,
    this.keyCode = 0,
    this.modifiers = 0,
    this.unicodeCodepoint = 0,
  });

  final int type;
  final int timestampMicros;
  final double x;
  final double y;
  final double deltaX;
  final double deltaY;
  final int pointerId;
  final int button;
  final int keyCode;
  final int modifiers;
  final int unicodeCodepoint;
}

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
  Future<int> engineSetSurfaceSize({required int width, required int height});
  Future<EngineFrameInfo?> engineGetFrameDesc();
  Future<Uint8List?> engineReadFrameRgba();
  Future<int> engineSendInput(EngineInputEventData event);
  Future<int> engineRuntimeApiVersion();

  String engineGetLastError();
}

typedef EngineBridgeBuilder = EngineBridge Function({String? ffiLibraryPath});
