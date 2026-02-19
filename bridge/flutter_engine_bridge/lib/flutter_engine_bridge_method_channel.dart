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

  @override
  Future<int?> createTexture({required int width, required int height}) async {
    return methodChannel.invokeMethod<int>('createTexture', <String, Object>{
      'width': width,
      'height': height,
    });
  }

  @override
  Future<void> updateTextureRgba({
    required int textureId,
    required Uint8List rgba,
    required int width,
    required int height,
    required int rowBytes,
  }) async {
    await methodChannel
        .invokeMethod<void>('updateTextureRgba', <String, Object>{
          'textureId': textureId,
          'rgba': rgba,
          'width': width,
          'height': height,
          'rowBytes': rowBytes,
        });
  }

  @override
  Future<void> disposeTexture({required int textureId}) async {
    await methodChannel.invokeMethod<void>('disposeTexture', <String, Object>{
      'textureId': textureId,
    });
  }

  @override
  Future<Map<String, dynamic>?> createIOSurfaceTexture({
    required int width,
    required int height,
  }) async {
    final result = await methodChannel
        .invokeMapMethod<String, dynamic>(
            'createIOSurfaceTexture', <String, Object>{
          'width': width,
          'height': height,
        });
    return result;
  }

  @override
  Future<void> notifyFrameAvailable({required int textureId}) async {
    await methodChannel
        .invokeMethod<void>('notifyFrameAvailable', <String, Object>{
          'textureId': textureId,
        });
  }

  @override
  Future<Map<String, dynamic>?> resizeIOSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) async {
    final result = await methodChannel
        .invokeMapMethod<String, dynamic>(
            'resizeIOSurfaceTexture', <String, Object>{
          'textureId': textureId,
          'width': width,
          'height': height,
        });
    return result;
  }

  @override
  Future<void> disposeIOSurfaceTexture({required int textureId}) async {
    await methodChannel
        .invokeMethod<void>('disposeIOSurfaceTexture', <String, Object>{
          'textureId': textureId,
        });
  }
}
