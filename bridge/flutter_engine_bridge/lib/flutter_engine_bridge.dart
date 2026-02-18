
import 'flutter_engine_bridge_platform_interface.dart';

class FlutterEngineBridge {
  Future<String?> getPlatformVersion() {
    return FlutterEngineBridgePlatform.instance.getPlatformVersion();
  }
}
