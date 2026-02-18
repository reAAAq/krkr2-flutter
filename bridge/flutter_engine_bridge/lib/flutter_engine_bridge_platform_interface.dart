import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'flutter_engine_bridge_method_channel.dart';

abstract class FlutterEngineBridgePlatform extends PlatformInterface {
  /// Constructs a FlutterEngineBridgePlatform.
  FlutterEngineBridgePlatform() : super(token: _token);

  static final Object _token = Object();

  static FlutterEngineBridgePlatform _instance = MethodChannelFlutterEngineBridge();

  /// The default instance of [FlutterEngineBridgePlatform] to use.
  ///
  /// Defaults to [MethodChannelFlutterEngineBridge].
  static FlutterEngineBridgePlatform get instance => _instance;

  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [FlutterEngineBridgePlatform] when
  /// they register themselves.
  static set instance(FlutterEngineBridgePlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('platformVersion() has not been implemented.');
  }
}
