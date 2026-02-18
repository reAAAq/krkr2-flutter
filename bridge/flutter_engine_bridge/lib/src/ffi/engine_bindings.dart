import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

const int kEngineApiVersion = 0x01000000;
const int kEngineResultOk = 0;
const int kEngineResultNotSupported = -3;

final class EngineCreateDesc extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  external Pointer<Utf8> writablePathUtf8;
  external Pointer<Utf8> cachePathUtf8;
  external Pointer<Void> userData;

  @Uint64()
  external int reservedU640;

  @Uint64()
  external int reservedU641;

  @Uint64()
  external int reservedU642;

  @Uint64()
  external int reservedU643;

  external Pointer<Void> reservedPtr0;
  external Pointer<Void> reservedPtr1;
  external Pointer<Void> reservedPtr2;
  external Pointer<Void> reservedPtr3;
}

final class EngineOption extends Struct {
  external Pointer<Utf8> keyUtf8;
  external Pointer<Utf8> valueUtf8;

  @Uint64()
  external int reservedU640;

  @Uint64()
  external int reservedU641;

  external Pointer<Void> reservedPtr0;
  external Pointer<Void> reservedPtr1;
}

typedef _EngineGetRuntimeApiVersionNative = Int32 Function(Pointer<Uint32>);
typedef _EngineGetRuntimeApiVersionDart = int Function(Pointer<Uint32>);

typedef _EngineCreateNative =
    Int32 Function(Pointer<EngineCreateDesc>, Pointer<Pointer<Void>>);
typedef _EngineCreateDart =
    int Function(Pointer<EngineCreateDesc>, Pointer<Pointer<Void>>);

typedef _EngineDestroyNative = Int32 Function(Pointer<Void>);
typedef _EngineDestroyDart = int Function(Pointer<Void>);

typedef _EngineOpenGameNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);
typedef _EngineOpenGameDart =
    int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>);

typedef _EngineTickNative = Int32 Function(Pointer<Void>, Uint32);
typedef _EngineTickDart = int Function(Pointer<Void>, int);

typedef _EnginePauseNative = Int32 Function(Pointer<Void>);
typedef _EnginePauseDart = int Function(Pointer<Void>);

typedef _EngineResumeNative = Int32 Function(Pointer<Void>);
typedef _EngineResumeDart = int Function(Pointer<Void>);

typedef _EngineSetOptionNative =
    Int32 Function(Pointer<Void>, Pointer<EngineOption>);
typedef _EngineSetOptionDart =
    int Function(Pointer<Void>, Pointer<EngineOption>);

typedef _EngineGetLastErrorNative = Pointer<Utf8> Function(Pointer<Void>);
typedef _EngineGetLastErrorDart = Pointer<Utf8> Function(Pointer<Void>);

class EngineBindings {
  EngineBindings(DynamicLibrary library)
    : engineGetRuntimeApiVersion = library
          .lookupFunction<
            _EngineGetRuntimeApiVersionNative,
            _EngineGetRuntimeApiVersionDart
          >('engine_get_runtime_api_version'),
      engineCreate = library
          .lookupFunction<_EngineCreateNative, _EngineCreateDart>(
            'engine_create',
          ),
      engineDestroy = library
          .lookupFunction<_EngineDestroyNative, _EngineDestroyDart>(
            'engine_destroy',
          ),
      engineOpenGame = library
          .lookupFunction<_EngineOpenGameNative, _EngineOpenGameDart>(
            'engine_open_game',
          ),
      engineTick = library.lookupFunction<_EngineTickNative, _EngineTickDart>(
        'engine_tick',
      ),
      enginePause = library
          .lookupFunction<_EnginePauseNative, _EnginePauseDart>('engine_pause'),
      engineResume = library
          .lookupFunction<_EngineResumeNative, _EngineResumeDart>(
            'engine_resume',
          ),
      engineSetOption = library
          .lookupFunction<_EngineSetOptionNative, _EngineSetOptionDart>(
            'engine_set_option',
          ),
      engineGetLastError = library
          .lookupFunction<_EngineGetLastErrorNative, _EngineGetLastErrorDart>(
            'engine_get_last_error',
          );

  final int Function(Pointer<Uint32>) engineGetRuntimeApiVersion;
  final int Function(Pointer<EngineCreateDesc>, Pointer<Pointer<Void>>)
  engineCreate;
  final int Function(Pointer<Void>) engineDestroy;
  final int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>)
  engineOpenGame;
  final int Function(Pointer<Void>, int) engineTick;
  final int Function(Pointer<Void>) enginePause;
  final int Function(Pointer<Void>) engineResume;
  final int Function(Pointer<Void>, Pointer<EngineOption>) engineSetOption;
  final Pointer<Utf8> Function(Pointer<Void>) engineGetLastError;

  static String _lastLoadError = '';
  static String get lastLoadError => _lastLoadError;

  static DynamicLibrary? tryLoadLibrary({String? overridePath}) {
    final List<String> errors = <String>[];
    _lastLoadError = '';

    if (overridePath != null && overridePath.isNotEmpty) {
      try {
        return DynamicLibrary.open(overridePath);
      } catch (error) {
        errors.add('open("$overridePath") failed: $error');
      }
    }

    final candidates = _candidateLibraryNames();
    for (final candidate in candidates) {
      try {
        return DynamicLibrary.open(candidate);
      } catch (error) {
        errors.add('open("$candidate") failed: $error');
      }
    }

    try {
      return DynamicLibrary.process();
    } catch (error) {
      errors.add('DynamicLibrary.process() failed: $error');
    }

    try {
      return DynamicLibrary.executable();
    } catch (error) {
      errors.add('DynamicLibrary.executable() failed: $error');
    }

    if (errors.isEmpty) {
      _lastLoadError =
          'No dynamic library candidates available for this platform.';
    } else {
      _lastLoadError = errors.join('\n');
    }

    return null;
  }

  static List<String> _candidateLibraryNames() {
    if (Platform.isWindows) {
      return const ['engine_api.dll', 'libengine_api.dll'];
    }
    if (Platform.isMacOS) {
      return const ['libengine_api.dylib'];
    }
    if (Platform.isAndroid || Platform.isLinux) {
      return const ['libengine_api.so'];
    }
    if (Platform.isIOS) {
      return const [];
    }
    return const [];
  }
}
