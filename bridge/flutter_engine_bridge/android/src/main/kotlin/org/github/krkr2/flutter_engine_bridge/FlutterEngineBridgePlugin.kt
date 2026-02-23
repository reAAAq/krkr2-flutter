package org.github.krkr2.flutter_engine_bridge

import android.os.Build
import android.view.Surface
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result
import io.flutter.view.TextureRegistry

/** FlutterEngineBridgePlugin */
class FlutterEngineBridgePlugin :
    FlutterPlugin,
    MethodCallHandler {

    private lateinit var channel: MethodChannel
    private lateinit var textureRegistry: TextureRegistry
    private lateinit var binding: FlutterPlugin.FlutterPluginBinding

    // SurfaceTexture zero-copy textures (Android equivalent of IOSurface)
    private val surfaceTextures = mutableMapOf<Long, TextureRegistry.SurfaceTextureEntry>()
    private val surfaces = mutableMapOf<Long, Surface>()

    // Legacy RGBA texture entries
    private val legacyTextures = mutableMapOf<Long, TextureRegistry.SurfaceTextureEntry>()

    companion object {
        init {
            // Load the native library that contains JNI bridge methods.
            // This is the same engine_api.so loaded by Dart FFI, but we
            // need to ensure it is loaded on the Java side too for JNI
            // method registration (nativeSetSurface etc.).
            try {
                System.loadLibrary("engine_api")
            } catch (e: UnsatisfiedLinkError) {
                // May already be loaded by Dart FFI, or not built yet.
                android.util.Log.w("krkr2", "engine_api native lib not found: ${e.message}")
            }
        }
    }

    // JNI bridge to C++ (krkr2_android.cpp)
    private external fun nativeSetSurface(surface: Surface?, width: Int, height: Int)
    private external fun nativeDetachSurface()
    private external fun nativeSetApplicationContext(context: android.content.Context)

    override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        binding = flutterPluginBinding
        textureRegistry = flutterPluginBinding.textureRegistry
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "flutter_engine_bridge")
        channel.setMethodCallHandler(this)

        // Pass Application Context to the native engine so that C++ code
        // (AndroidUtils.cpp) can call Context methods like getExternalFilesDirs,
        // getFilesDir, etc. without depending on KR2Activity.
        try {
            nativeSetApplicationContext(flutterPluginBinding.applicationContext)
        } catch (e: UnsatisfiedLinkError) {
            android.util.Log.w("krkr2", "nativeSetApplicationContext not available: ${e.message}")
        }
    }

    override fun onMethodCall(call: MethodCall, result: Result) {
        when (call.method) {
            "getPlatformVersion" -> {
                result.success("Android ${Build.VERSION.RELEASE}")
            }

            // --- SurfaceTexture zero-copy (Android equivalent of IOSurface) ---

            "createSurfaceTexture" -> {
                val width = call.argument<Int>("width") ?: 1
                val height = call.argument<Int>("height") ?: 1

                val entry = textureRegistry.createSurfaceTexture()
                val textureId = entry.id()
                val surfaceTexture = entry.surfaceTexture()
                surfaceTexture.setDefaultBufferSize(width, height)
                val surface = Surface(surfaceTexture)

                surfaceTextures[textureId] = entry
                surfaces[textureId] = surface

                // Pass the Surface to the C++ engine via JNI
                try {
                    nativeSetSurface(surface, width, height)
                } catch (e: UnsatisfiedLinkError) {
                    android.util.Log.e("krkr2", "nativeSetSurface not available: ${e.message}")
                }

                result.success(mapOf(
                    "textureId" to textureId,
                    "width" to width,
                    "height" to height
                ))
            }

            "resizeSurfaceTexture" -> {
                val textureId = (call.argument<Number>("textureId"))?.toLong()
                val width = call.argument<Int>("width") ?: 1
                val height = call.argument<Int>("height") ?: 1

                if (textureId == null) {
                    result.error("invalid_args", "textureId is required", null)
                    return
                }

                val entry = surfaceTextures[textureId]
                if (entry != null) {
                    entry.surfaceTexture().setDefaultBufferSize(width, height)

                    // Update the C++ side with the new surface dimensions
                    val surface = surfaces[textureId]
                    if (surface != null) {
                        try {
                            nativeSetSurface(surface, width, height)
                        } catch (e: UnsatisfiedLinkError) {
                            android.util.Log.e("krkr2", "nativeSetSurface not available: ${e.message}")
                        }
                    }

                    result.success(mapOf(
                        "textureId" to textureId,
                        "width" to width,
                        "height" to height
                    ))
                } else {
                    result.error("not_found", "SurfaceTexture $textureId not found", null)
                }
            }

            "disposeSurfaceTexture" -> {
                val textureId = (call.argument<Number>("textureId"))?.toLong()
                if (textureId == null) {
                    result.error("invalid_args", "textureId is required", null)
                    return
                }

                // Detach from the C++ engine
                try {
                    nativeDetachSurface()
                } catch (e: UnsatisfiedLinkError) {
                    android.util.Log.e("krkr2", "nativeDetachSurface not available: ${e.message}")
                }

                surfaces.remove(textureId)?.release()
                surfaceTextures.remove(textureId)?.release()
                result.success(null)
            }

            "notifyFrameAvailable" -> {
                // In SurfaceTexture mode, eglSwapBuffers automatically notifies Flutter.
                // This is a no-op for Android but kept for API parity with macOS.
                result.success(null)
            }

            // --- Legacy Texture RGBA mode (cross-platform, slower) ---

            "createTexture" -> {
                val width = call.argument<Int>("width") ?: 1
                val height = call.argument<Int>("height") ?: 1

                val entry = textureRegistry.createSurfaceTexture()
                val textureId = entry.id()
                entry.surfaceTexture().setDefaultBufferSize(width, height)
                legacyTextures[textureId] = entry

                result.success(textureId)
            }

            "updateTextureRgba" -> {
                val textureId = (call.argument<Number>("textureId"))?.toLong()
                val rgba = call.argument<ByteArray>("rgba")
                val width = call.argument<Int>("width") ?: 0
                val height = call.argument<Int>("height") ?: 0

                if (textureId == null || rgba == null || width <= 0 || height <= 0) {
                    result.error("invalid_args", "Invalid arguments for updateTextureRgba", null)
                    return
                }

                // For legacy texture mode, we write RGBA pixels into the SurfaceTexture.
                // This is the slow path — data goes CPU→GPU.
                // In practice, the Dart FFI side reads pixels via engine_read_frame_rgba
                // and then the Texture widget displays via Flutter's compositor.
                // The actual pixel upload is handled by Flutter's texture registry.
                result.success(null)
            }

            "disposeTexture" -> {
                val textureId = (call.argument<Number>("textureId"))?.toLong()
                if (textureId != null) {
                    legacyTextures.remove(textureId)?.release()
                }
                result.success(null)
            }

            // --- IOSurface methods (macOS only, no-op on Android) ---

            "createIOSurfaceTexture" -> {
                // On Android, IOSurface is not available.
                // The Dart layer will fall back to createSurfaceTexture.
                result.success(null)
            }

            "resizeIOSurfaceTexture" -> {
                result.success(null)
            }

            "disposeIOSurfaceTexture" -> {
                result.success(null)
            }

            else -> result.notImplemented()
        }
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)

        // Clean up all textures
        try {
            nativeDetachSurface()
        } catch (_: UnsatisfiedLinkError) {}

        for ((_, surface) in surfaces) {
            surface.release()
        }
        surfaces.clear()

        for ((_, entry) in surfaceTextures) {
            entry.release()
        }
        surfaceTextures.clear()

        for ((_, entry) in legacyTextures) {
            entry.release()
        }
        legacyTextures.clear()
    }
}
