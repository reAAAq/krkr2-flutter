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
  Future<int> engineTick({int deltaMs = 16}) async {
    tickCalls += 1;
    return 0;
  }
}

void main() {
  testWidgets('Flutter shell smoke test', (WidgetTester tester) async {
    await tester.pumpWidget(const FlutterShellApp());
    await tester.pump(const Duration(milliseconds: 200));

    expect(find.text('Krkr2 Flutter Shell'), findsWidgets);
    expect(find.textContaining('Engine bridge status'), findsOneWidget);
    expect(find.text('engine_create'), findsOneWidget);
    expect(find.text('engine_open_game'), findsOneWidget);
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
}
