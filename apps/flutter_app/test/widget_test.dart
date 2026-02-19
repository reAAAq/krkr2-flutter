import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:flutter_app/engine/engine_bridge.dart';
import 'package:flutter_app/main.dart';

class _FakeEngineBridge implements EngineBridge {
  int createCalls = 0;
  int destroyCalls = 0;
  int openGameCalls = 0;
  int tickCalls = 0;
  int pauseCalls = 0;
  int resumeCalls = 0;
  int setOptionCalls = 0;
  int setSurfaceSizeCalls = 0;
  int sendInputCalls = 0;
  int createTextureCalls = 0;
  int updateTextureCalls = 0;
  int disposeTextureCalls = 0;

  @override
  bool get isFfiAvailable => true;

  @override
  String get ffiInitializationError => '';

  @override
  Future<String> getBackendDescription() async => 'fake';

  @override
  Future<String?> getPlatformVersion() async => 'test-platform';

  @override
  Future<int> engineCreate({String? writablePath, String? cachePath}) async {
    createCalls += 1;
    return 0;
  }

  @override
  Future<int> engineDestroy() async {
    destroyCalls += 1;
    return 0;
  }

  @override
  String engineGetLastError() => '';

  @override
  Future<int> engineOpenGame(
    String gameRootPath, {
    String? startupScript,
  }) async {
    openGameCalls += 1;
    return 0;
  }

  @override
  Future<int> enginePause() async {
    pauseCalls += 1;
    return 0;
  }

  @override
  Future<int> engineResume() async {
    resumeCalls += 1;
    return 0;
  }

  @override
  Future<int> engineRuntimeApiVersion() async => 0x01000000;

  @override
  Future<int> engineSetOption({
    required String key,
    required String value,
  }) async {
    setOptionCalls += 1;
    return 0;
  }

  @override
  Future<int> engineSetSurfaceSize({
    required int width,
    required int height,
  }) async {
    setSurfaceSizeCalls += 1;
    return 0;
  }

  @override
  Future<EngineFrameInfo?> engineGetFrameDesc() async {
    return const EngineFrameInfo(
      width: 16,
      height: 16,
      strideBytes: 64,
      pixelFormat: 1,
      frameSerial: 1,
    );
  }

  @override
  Future<Uint8List?> engineReadFrameRgba() async {
    return Uint8List(16 * 16 * 4);
  }

  @override
  Future<EngineFrameData?> engineReadFrame() async {
    return EngineFrameData(
      info: const EngineFrameInfo(
        width: 16,
        height: 16,
        strideBytes: 64,
        pixelFormat: 1,
        frameSerial: 1,
      ),
      pixels: Uint8List(16 * 16 * 4),
    );
  }

  @override
  Future<int> engineSendInput(EngineInputEventData event) async {
    sendInputCalls += 1;
    return 0;
  }

  @override
  Future<int?> createTexture({required int width, required int height}) async {
    createTextureCalls += 1;
    return null;
  }

  @override
  Future<bool> updateTextureRgba({
    required int textureId,
    required Uint8List rgba,
    required int width,
    required int height,
    required int rowBytes,
  }) async {
    updateTextureCalls += 1;
    return true;
  }

  @override
  Future<void> disposeTexture({required int textureId}) async {
    disposeTextureCalls += 1;
  }

  @override
  Future<int> engineTick({int deltaMs = 16}) async {
    tickCalls += 1;
    return 0;
  }

  @override
  Future<int> engineSetRenderTargetIOSurface({
    required int iosurfaceId,
    required int width,
    required int height,
  }) async => 0;

  @override
  Future<bool> engineGetFrameRenderedFlag() async => false;

  @override
  Future<Map<String, dynamic>?> createIOSurfaceTexture({
    required int width,
    required int height,
  }) async => null;

  @override
  Future<void> notifyFrameAvailable({required int textureId}) async {}

  @override
  Future<Map<String, dynamic>?> resizeIOSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) async => null;

  @override
  Future<void> disposeIOSurfaceTexture({required int textureId}) async {}
}

void main() {
  testWidgets('Flutter shell smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const FlutterShellApp());
    await tester.pump(const Duration(milliseconds: 200));

    expect(find.text('Krkr2 Flutter Shell'), findsWidgets);
    expect(find.textContaining('Engine bridge status'), findsOneWidget);
    expect(find.text('engine_create'), findsOneWidget);
    expect(find.text('engine_open_game'), findsOneWidget);
    expect(find.text('Run Open+Tick Smoke'), findsOneWidget);
    expect(find.text('Start Tick'), findsOneWidget);
    expect(find.text('engine_destroy'), findsOneWidget);
    expect(find.text('engine_pause'), findsOneWidget);
    expect(find.text('engine_resume'), findsOneWidget);
    expect(find.text('engine_set_option'), findsOneWidget);
    expect(find.text('Apply bridge path'), findsOneWidget);
    expect(find.text('Event logs'), findsOneWidget);
    expect(find.text('Clear logs'), findsOneWidget);
  });

  testWidgets('create/destroy flow uses injected engine bridge', (
    WidgetTester tester,
  ) async {
    final fakeBridge = _FakeEngineBridge();
    await tester.pumpWidget(
      FlutterShellApp(
        engineBridgeBuilder: ({String? ffiLibraryPath}) => fakeBridge,
      ),
    );
    await tester.pumpAndSettle();

    final createButton = find.widgetWithText(FilledButton, 'engine_create');
    await tester.ensureVisible(createButton);
    await tester.tap(createButton);
    await tester.pumpAndSettle();
    expect(fakeBridge.createCalls, 1);
    expect(find.textContaining('Engine status: Created'), findsOneWidget);

    final destroyButton = find.widgetWithText(OutlinedButton, 'engine_destroy');
    await tester.ensureVisible(destroyButton);
    await tester.tap(destroyButton);
    await tester.pumpAndSettle();
    expect(fakeBridge.destroyCalls, 1);
    expect(find.textContaining('Engine status: Not created'), findsOneWidget);
  });

  testWidgets('open+tick smoke calls create/open/tick in order', (
    WidgetTester tester,
  ) async {
    final fakeBridge = _FakeEngineBridge();
    await tester.pumpWidget(
      FlutterShellApp(
        engineBridgeBuilder: ({String? ffiLibraryPath}) => fakeBridge,
      ),
    );
    await tester.pumpAndSettle();

    final smokeButton = find.widgetWithText(
      ElevatedButton,
      'Run Open+Tick Smoke',
    );
    await tester.ensureVisible(smokeButton);
    await tester.tap(smokeButton);
    await tester.pump(const Duration(milliseconds: 200));

    expect(fakeBridge.createCalls, 1);
    expect(fakeBridge.openGameCalls, 1);
    expect(fakeBridge.tickCalls, 3);
    expect(
      find.textContaining('Last result: open_tick_smoke => 0'),
      findsOneWidget,
    );
  });
}
