import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_engine_bridge/flutter_engine_bridge.dart';
import 'package:flutter_engine_bridge/flutter_engine_bridge_platform_interface.dart';
import 'package:flutter_engine_bridge/flutter_engine_bridge_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockFlutterEngineBridgePlatform
    with MockPlatformInterfaceMixin
    implements FlutterEngineBridgePlatform {
  @override
  Future<String?> getPlatformVersion() => Future.value('42');
}

void main() {
  final FlutterEngineBridgePlatform initialPlatform = FlutterEngineBridgePlatform.instance;

  test('$MethodChannelFlutterEngineBridge is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelFlutterEngineBridge>());
  });

  test('getPlatformVersion', () async {
    final MockFlutterEngineBridgePlatform fakePlatform = MockFlutterEngineBridgePlatform();
    final FlutterEngineBridge flutterEngineBridgePlugin = FlutterEngineBridge(
      platform: fakePlatform,
    );

    expect(await flutterEngineBridgePlugin.getPlatformVersion(), '42');
  });

  test('method_channel fallback is used when ffi is unavailable', () async {
    final MockFlutterEngineBridgePlatform fakePlatform = MockFlutterEngineBridgePlatform();
    final FlutterEngineBridge bridge = FlutterEngineBridge(platform: fakePlatform);

    expect(bridge.isFfiAvailable, isFalse);
    expect(await bridge.getBackendDescription(), 'method_channel(42)');
  });

  test('engineCreate returns not_supported on fallback path', () async {
    final MockFlutterEngineBridgePlatform fakePlatform = MockFlutterEngineBridgePlatform();
    final FlutterEngineBridge bridge = FlutterEngineBridge(platform: fakePlatform);

    final int result = await bridge.engineCreate();
    expect(result, -3);
    expect(bridge.engineGetLastError(), contains('FFI unavailable for engine_create'));
  });

  test('engineRuntimeApiVersion returns not_supported on fallback path', () async {
    final MockFlutterEngineBridgePlatform fakePlatform = MockFlutterEngineBridgePlatform();
    final FlutterEngineBridge bridge = FlutterEngineBridge(platform: fakePlatform);

    final int result = await bridge.engineRuntimeApiVersion();
    expect(result, -3);
    expect(
      bridge.engineGetLastError(),
      contains('FFI unavailable for engine_get_runtime_api_version'),
    );
  });
}
