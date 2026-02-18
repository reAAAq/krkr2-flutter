import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'engine_bindings.dart';

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
