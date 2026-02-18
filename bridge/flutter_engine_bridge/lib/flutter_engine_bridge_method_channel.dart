import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'flutter_engine_bridge_platform_interface.dart';

/// An implementation of [FlutterEngineBridgePlatform] that uses method channels.
class MethodChannelFlutterEngineBridge extends FlutterEngineBridgePlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('flutter_engine_bridge');

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>(
      'getPlatformVersion',
    );
    return version;
  }
}
