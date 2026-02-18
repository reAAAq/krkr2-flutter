import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'engine_bindings.dart';

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

class EngineFrameData {
  const EngineFrameData({required this.info, required this.pixels});

  final EngineFrameInfo info;
  final Uint8List pixels;
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

class EngineFfiBridge {
  EngineFfiBridge._(this._bindings);

  final EngineBindings _bindings;
  Pointer<Void> _handle = nullptr;
  static String _lastCreateError = '';
  static String get lastCreateError => _lastCreateError;

  static EngineFfiBridge? tryCreate({String? libraryPath}) {
    _lastCreateError = '';
    final library = EngineBindings.tryLoadLibrary(overridePath: libraryPath);
    if (library == null) {
      _lastCreateError =
          'Failed to load engine_api dynamic library.\n${EngineBindings.lastLoadError}';
      return null;
    }
    try {
      return EngineFfiBridge._(EngineBindings(library));
    } catch (error) {
      _lastCreateError = 'Failed to bind engine_api symbols: $error';
      return null;
    }
  }

  bool get isCreated => _handle != nullptr;

  int runtimeApiVersion() {
    final outVersion = calloc<Uint32>();
    try {
      final result = _bindings.engineGetRuntimeApiVersion(outVersion);
      if (result != kEngineResultOk) {
        return result;
      }
      return outVersion.value;
    } finally {
      calloc.free(outVersion);
    }
  }

  int create({String? writablePath, String? cachePath}) {
    if (_handle != nullptr) {
      return kEngineResultOk;
    }

    final desc = calloc<EngineCreateDesc>();
    final outHandle = calloc<Pointer<Void>>();
    final allocatedStrings = <Pointer<Utf8>>[];
    try {
      final writablePtr = _toNativeUtf8OrNull(writablePath, allocatedStrings);
      final cachePtr = _toNativeUtf8OrNull(cachePath, allocatedStrings);

      desc.ref.structSize = sizeOf<EngineCreateDesc>();
      desc.ref.apiVersion = kEngineApiVersion;
      desc.ref.writablePathUtf8 = writablePtr;
      desc.ref.cachePathUtf8 = cachePtr;
      desc.ref.userData = nullptr;
      desc.ref.reservedU640 = 0;
      desc.ref.reservedU641 = 0;
      desc.ref.reservedU642 = 0;
      desc.ref.reservedU643 = 0;
      desc.ref.reservedPtr0 = nullptr;
      desc.ref.reservedPtr1 = nullptr;
      desc.ref.reservedPtr2 = nullptr;
      desc.ref.reservedPtr3 = nullptr;
      outHandle.value = nullptr;

      final result = _bindings.engineCreate(desc, outHandle);
      if (result == kEngineResultOk) {
        _handle = outHandle.value;
      }
      return result;
    } finally {
      for (final value in allocatedStrings) {
        calloc.free(value);
      }
      calloc.free(outHandle);
      calloc.free(desc);
    }
  }

  int destroy() {
    final current = _handle;
    if (current == nullptr) {
      return kEngineResultOk;
    }

    final result = _bindings.engineDestroy(current);
    if (result == kEngineResultOk) {
      _handle = nullptr;
    }
    return result;
  }

  int openGame(String gameRootPath, {String? startupScript}) {
    final gameRootPtr = gameRootPath.toNativeUtf8();
    final startupScriptPtr = startupScript == null
        ? nullptr.cast<Utf8>()
        : startupScript.toNativeUtf8();
    try {
      return _bindings.engineOpenGame(_handle, gameRootPtr, startupScriptPtr);
    } finally {
      calloc.free(gameRootPtr);
      if (startupScriptPtr != nullptr) {
        calloc.free(startupScriptPtr);
      }
    }
  }

  int tick({int deltaMs = 16}) {
    return _bindings.engineTick(_handle, deltaMs);
  }

  int pause() {
    return _bindings.enginePause(_handle);
  }

  int resume() {
    return _bindings.engineResume(_handle);
  }

  int setOption({required String key, required String value}) {
    final option = calloc<EngineOption>();
    final keyPtr = key.toNativeUtf8();
    final valuePtr = value.toNativeUtf8();
    try {
      option.ref.keyUtf8 = keyPtr;
      option.ref.valueUtf8 = valuePtr;
      option.ref.reservedU640 = 0;
      option.ref.reservedU641 = 0;
      option.ref.reservedPtr0 = nullptr;
      option.ref.reservedPtr1 = nullptr;
      return _bindings.engineSetOption(_handle, option);
    } finally {
      calloc.free(valuePtr);
      calloc.free(keyPtr);
      calloc.free(option);
    }
  }

  int setSurfaceSize({required int width, required int height}) {
    return _bindings.engineSetSurfaceSize(_handle, width, height);
  }

  EngineFrameInfo? getFrameDesc() {
    final desc = calloc<EngineFrameDesc>();
    try {
      desc.ref.structSize = sizeOf<EngineFrameDesc>();
      final result = _bindings.engineGetFrameDesc(_handle, desc);
      if (result != kEngineResultOk) {
        return null;
      }
      return EngineFrameInfo(
        width: desc.ref.width,
        height: desc.ref.height,
        strideBytes: desc.ref.strideBytes,
        pixelFormat: desc.ref.pixelFormat,
        frameSerial: desc.ref.frameSerial,
      );
    } finally {
      calloc.free(desc);
    }
  }

  Uint8List? readFrameRgba() {
    final frameDesc = getFrameDesc();
    if (frameDesc == null) {
      return null;
    }

    final bufferSize = frameDesc.strideBytes * frameDesc.height;
    if (bufferSize <= 0) {
      return Uint8List(0);
    }

    final buffer = calloc<Uint8>(bufferSize);
    try {
      final result = _bindings.engineReadFrameRgba(
        _handle,
        buffer.cast<Void>(),
        bufferSize,
      );
      if (result != kEngineResultOk) {
        return null;
      }
      return Uint8List.fromList(buffer.asTypedList(bufferSize));
    } finally {
      calloc.free(buffer);
    }
  }

  EngineFrameData? readFrameRgbaWithDesc({int maxRetries = 1}) {
    for (int attempt = 0; attempt <= maxRetries; attempt++) {
      final EngineFrameInfo? frameDesc = getFrameDesc();
      if (frameDesc == null) {
        return null;
      }

      final int bufferSize = frameDesc.strideBytes * frameDesc.height;
      if (bufferSize <= 0) {
        return EngineFrameData(info: frameDesc, pixels: Uint8List(0));
      }

      final Pointer<Uint8> buffer = calloc<Uint8>(bufferSize);
      try {
        final int result = _bindings.engineReadFrameRgba(
          _handle,
          buffer.cast<Void>(),
          bufferSize,
        );
        if (result == kEngineResultOk) {
          return EngineFrameData(
            info: frameDesc,
            pixels: Uint8List.fromList(buffer.asTypedList(bufferSize)),
          );
        }
        // Surface size can race with frame read; retry once with fresh desc.
        if (result == kEngineResultInvalidArgument && attempt < maxRetries) {
          continue;
        }
        return null;
      } finally {
        calloc.free(buffer);
      }
    }
    return null;
  }

  int sendInput(EngineInputEventData event) {
    final nativeEvent = calloc<EngineInputEvent>();
    try {
      nativeEvent.ref.structSize = sizeOf<EngineInputEvent>();
      nativeEvent.ref.type = event.type;
      nativeEvent.ref.timestampMicros = event.timestampMicros;
      nativeEvent.ref.x = event.x;
      nativeEvent.ref.y = event.y;
      nativeEvent.ref.deltaX = event.deltaX;
      nativeEvent.ref.deltaY = event.deltaY;
      nativeEvent.ref.pointerId = event.pointerId;
      nativeEvent.ref.button = event.button;
      nativeEvent.ref.keyCode = event.keyCode;
      nativeEvent.ref.modifiers = event.modifiers;
      nativeEvent.ref.unicodeCodepoint = event.unicodeCodepoint;
      nativeEvent.ref.reservedU32 = 0;
      nativeEvent.ref.reservedU640 = 0;
      nativeEvent.ref.reservedU641 = 0;
      nativeEvent.ref.reservedPtr0 = nullptr;
      nativeEvent.ref.reservedPtr1 = nullptr;
      return _bindings.engineSendInput(_handle, nativeEvent);
    } finally {
      calloc.free(nativeEvent);
    }
  }

  String lastError() {
    final errorPtr = _bindings.engineGetLastError(_handle);
    if (errorPtr == nullptr) {
      return '';
    }
    return errorPtr.toDartString();
  }

  static Pointer<Utf8> _toNativeUtf8OrNull(
    String? value,
    List<Pointer<Utf8>> allocatedStrings,
  ) {
    if (value == null) {
      return nullptr.cast<Utf8>();
    }
    final ptr = value.toNativeUtf8();
    allocatedStrings.add(ptr);
    return ptr;
  }
}
