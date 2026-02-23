import 'dart:typed_data';

import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'flutter_engine_bridge_method_channel.dart';

abstract class FlutterEngineBridgePlatform extends PlatformInterface {
  /// Constructs a FlutterEngineBridgePlatform.
  FlutterEngineBridgePlatform() : super(token: _token);

  static final Object _token = Object();

  static FlutterEngineBridgePlatform _instance =
      MethodChannelFlutterEngineBridge();

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

  Future<int?> createTexture({required int width, required int height}) {
    throw UnimplementedError('createTexture() has not been implemented.');
  }

  Future<void> updateTextureRgba({
    required int textureId,
    required Uint8List rgba,
    required int width,
    required int height,
    required int rowBytes,
  }) {
    throw UnimplementedError('updateTextureRgba() has not been implemented.');
  }

  Future<void> disposeTexture({required int textureId}) {
    throw UnimplementedError('disposeTexture() has not been implemented.');
  }

  /// Creates an IOSurface-backed texture for zero-copy rendering.
  /// Returns a map with: textureId, ioSurfaceID, width, height.
  Future<Map<String, dynamic>?> createIOSurfaceTexture({
    required int width,
    required int height,
  }) {
    throw UnimplementedError(
        'createIOSurfaceTexture() has not been implemented.');
  }

  /// Notifies Flutter that a new frame is available in the texture.
  Future<void> notifyFrameAvailable({required int textureId}) {
    throw UnimplementedError(
        'notifyFrameAvailable() has not been implemented.');
  }

  /// Resizes an IOSurface texture. Returns a map with updated ioSurfaceID.
  Future<Map<String, dynamic>?> resizeIOSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) {
    throw UnimplementedError(
        'resizeIOSurfaceTexture() has not been implemented.');
  }

  /// Disposes an IOSurface texture.
  Future<void> disposeIOSurfaceTexture({required int textureId}) {
    throw UnimplementedError(
        'disposeIOSurfaceTexture() has not been implemented.');
  }

  /// Creates a SurfaceTexture-backed texture for zero-copy rendering (Android).
  /// Returns a map with: textureId, width, height.
  Future<Map<String, dynamic>?> createSurfaceTexture({
    required int width,
    required int height,
  }) {
    throw UnimplementedError(
        'createSurfaceTexture() has not been implemented.');
  }

  /// Resizes a SurfaceTexture.
  Future<Map<String, dynamic>?> resizeSurfaceTexture({
    required int textureId,
    required int width,
    required int height,
  }) {
    throw UnimplementedError(
        'resizeSurfaceTexture() has not been implemented.');
  }

  /// Disposes a SurfaceTexture.
  Future<void> disposeSurfaceTexture({required int textureId}) {
    throw UnimplementedError(
        'disposeSurfaceTexture() has not been implemented.');
  }
}
