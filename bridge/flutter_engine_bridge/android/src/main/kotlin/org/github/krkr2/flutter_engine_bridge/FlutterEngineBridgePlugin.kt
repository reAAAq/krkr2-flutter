package org.github.krkr2.flutter_engine_bridge

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.DocumentsContract
import android.provider.Settings
import android.util.Log
import android.view.Surface
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result
import io.flutter.plugin.common.PluginRegistry
import io.flutter.view.TextureRegistry

/** FlutterEngineBridgePlugin */
class FlutterEngineBridgePlugin :
    FlutterPlugin,
    MethodCallHandler,
    ActivityAware,
    PluginRegistry.ActivityResultListener {

    private lateinit var channel: MethodChannel
    private lateinit var textureRegistry: TextureRegistry
    private lateinit var binding: FlutterPlugin.FlutterPluginBinding
    private var engineAttached: Boolean = false
    private var activity: Activity? = null
    private var activityBinding: ActivityPluginBinding? = null
    private var pendingPickResult: Result? = null

    // SurfaceTexture zero-copy textures (Android equivalent of IOSurface)
    private val surfaceTextures = mutableMapOf<Long, TextureRegistry.SurfaceTextureEntry>()
    private val surfaces = mutableMapOf<Long, Surface>()

    // Legacy RGBA texture entries
    private val legacyTextures = mutableMapOf<Long, TextureRegistry.SurfaceTextureEntry>()

    companion object {
        private const val PICK_FILE_REQUEST = 9001

        init {
            try {
                System.loadLibrary("engine_api")
            } catch (e: UnsatisfiedLinkError) {
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
        engineAttached = true
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
        if (!engineAttached && call.method != "getPlatformVersion") {
            result.error("detached", "Flutter engine is detached", null)
            return
        }
        when (call.method) {
            "getPlatformVersion" -> {
                result.success("Android ${Build.VERSION.RELEASE}")
            }

            "hasManageExternalStorage" -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    result.success(Environment.isExternalStorageManager())
                } else {
                    result.success(true)
                }
            }

            "requestManageExternalStorage" -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    val ctx = binding.applicationContext
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                        .setData(Uri.parse("package:${ctx.packageName}"))
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    try {
                        ctx.startActivity(intent)
                        result.success(true)
                    } catch (_: Exception) {
                        val fallbackIntent = Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                        try {
                            ctx.startActivity(fallbackIntent)
                            result.success(true)
                        } catch (e: Exception) {
                            result.error("permission", "Failed to open all-files access settings: ${e.message}", null)
                        }
                    }
                } else {
                    result.success(true)
                }
            }

            // --- SurfaceTexture zero-copy (Android equivalent of IOSurface) ---

            "createSurfaceTexture" -> {
                val width = call.argument<Int>("width") ?: 1
                val height = call.argument<Int>("height") ?: 1

                val entry = try {
                    textureRegistry.createSurfaceTexture()
                } catch (e: RuntimeException) {
                    result.error("detached", "Unable to create texture: ${e.message}", null)
                    return
                }
                val textureId = entry.id()
                val surfaceTexture = entry.surfaceTexture()
                surfaceTexture.setDefaultBufferSize(width, height)
                val surface = Surface(surfaceTexture)

                surfaceTextures[textureId] = entry
                surfaces[textureId] = surface
                Log.i("krkr2", "plugin.createSurfaceTexture: id=$textureId size=${width}x$height")

                // Pass the Surface to the C++ engine via JNI
                try {
                    nativeSetSurface(surface, width, height)
                    Log.i("krkr2", "plugin.nativeSetSurface: id=$textureId size=${width}x$height")
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
                    Log.i("krkr2", "plugin.resizeSurfaceTexture: id=$textureId size=${width}x$height")

                    // Update the C++ side with the new surface dimensions
                    val surface = surfaces[textureId]
                    if (surface != null) {
                        try {
                            nativeSetSurface(surface, width, height)
                            Log.i("krkr2", "plugin.nativeSetSurface(resize): id=$textureId size=${width}x$height")
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
                    Log.i("krkr2", "plugin.nativeDetachSurface: id=$textureId")
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

                val entry = try {
                    textureRegistry.createSurfaceTexture()
                } catch (e: RuntimeException) {
                    result.error("detached", "Unable to create texture: ${e.message}", null)
                    return
                }
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

            "resolveContentUri" -> {
                val uriString = call.argument<String>("uri")
                if (uriString == null) {
                    result.success(null)
                    return
                }
                val realPath = resolveRealPath(Uri.parse(uriString))
                result.success(realPath)
            }

            "pickFile" -> {
                val act = activity
                if (act == null) {
                    result.error("no_activity", "No activity available", null)
                    return
                }
                if (pendingPickResult != null) {
                    result.error("busy", "Another pick is in progress", null)
                    return
                }
                pendingPickResult = result
                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    addCategory(Intent.CATEGORY_OPENABLE)
                    type = "*/*"
                }
                act.startActivityForResult(intent, PICK_FILE_REQUEST)
            }

            else -> result.notImplemented()
        }
    }

    private fun resolveRealPath(uri: Uri): String? {
        val ctx = binding.applicationContext
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT &&
            DocumentsContract.isDocumentUri(ctx, uri)
        ) {
            val docId = DocumentsContract.getDocumentId(uri)
            if (uri.authority == "com.android.externalstorage.documents") {
                val parts = docId.split(":", limit = 2)
                if (parts.size == 2) {
                    val storageType = parts[0]
                    val relativePath = parts[1]
                    val root = if (storageType.equals("primary", ignoreCase = true)) {
                        Environment.getExternalStorageDirectory().absolutePath
                    } else {
                        "/storage/$storageType"
                    }
                    return "$root/$relativePath"
                }
            }
        }
        try {
            ctx.contentResolver.query(uri, arrayOf("_data"), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex("_data")
                    if (idx >= 0) return cursor.getString(idx)
                }
            }
        } catch (_: Exception) {}
        return null
    }

    // --- ActivityAware ---

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activity = binding.activity
        activityBinding = binding
        binding.addActivityResultListener(this)
    }

    override fun onDetachedFromActivity() {
        activityBinding?.removeActivityResultListener(this)
        activity = null
        activityBinding = null
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        onAttachedToActivity(binding)
    }

    override fun onDetachedFromActivityForConfigChanges() {
        onDetachedFromActivity()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode != PICK_FILE_REQUEST) return false
        val result = pendingPickResult
        pendingPickResult = null
        if (result == null) return true

        if (resultCode != Activity.RESULT_OK || data?.data == null) {
            result.success(null)
            return true
        }
        val uri = data.data!!
        val realPath = resolveRealPath(uri)
        result.success(realPath)
        return true
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        engineAttached = false
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
