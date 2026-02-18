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
    FlutterEngineBridge flutterEngineBridgePlugin = FlutterEngineBridge();
    MockFlutterEngineBridgePlatform fakePlatform = MockFlutterEngineBridgePlatform();
    FlutterEngineBridgePlatform.instance = fakePlatform;

    expect(await flutterEngineBridgePlugin.getPlatformVersion(), '42');
  });
}
