#ifndef KRKR2_ENGINE_API_H_
#define KRKR2_ENGINE_API_H_

#include <stddef.h>
#include <stdint.h>

/* Export macro for shared-library builds. / 共享库导出宏。 */
#if defined(_WIN32)
#if defined(ENGINE_API_BUILD_SHARED)
#define ENGINE_API_EXPORT __declspec(dllexport)
#elif defined(ENGINE_API_USE_SHARED)
#define ENGINE_API_EXPORT __declspec(dllimport)
#else
#define ENGINE_API_EXPORT
#endif
#else
#if defined(__GNUC__) && __GNUC__ >= 4
#define ENGINE_API_EXPORT __attribute__((visibility("default")))
#else
#define ENGINE_API_EXPORT
#endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* ABI version: major(8bit), minor(8bit), patch(16bit).
 * ABI
 * 版本号：major(8bit)、minor(8bit)、patch(16bit)。 */
#define ENGINE_API_VERSION 0x01000000u
#define ENGINE_API_MAKE_VERSION(major, minor, patch)                           \
    ((((uint32_t)(major) & 0xFFu) << 24u) |                                    \
     (((uint32_t)(minor) & 0xFFu) << 16u) | ((uint32_t)(patch) & 0xFFFFu))

typedef struct engine_handle_s *engine_handle_t;

typedef enum engine_result_t {
    ENGINE_RESULT_OK = 0,
    ENGINE_RESULT_INVALID_ARGUMENT = -1,
    ENGINE_RESULT_INVALID_STATE = -2,
    ENGINE_RESULT_NOT_SUPPORTED = -3,
    ENGINE_RESULT_IO_ERROR = -4,
    ENGINE_RESULT_INTERNAL_ERROR = -5
} engine_result_t;

typedef struct engine_create_desc_t {
    uint32_t struct_size;
    uint32_t api_version;
    const char *writable_path_utf8;
    const char *cache_path_utf8;
    void *user_data;
    uint64_t reserved_u64[4];
    void *reserved_ptr[4];
} engine_create_desc_t;

typedef struct engine_option_t {
    const char *key_utf8;
    const char *value_utf8;
    uint64_t reserved_u64[2];
    void *reserved_ptr[2];
} engine_option_t;

typedef enum engine_pixel_format_t {
    ENGINE_PIXEL_FORMAT_UNKNOWN = 0,
    ENGINE_PIXEL_FORMAT_RGBA8888 = 1
} engine_pixel_format_t;

typedef struct engine_frame_desc_t {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    uint32_t pixel_format;
    uint64_t frame_serial;
    uint64_t reserved_u64[4];
    void *reserved_ptr[4];
} engine_frame_desc_t;

typedef struct engine_memory_stats_t {
    uint32_t struct_size;
    uint32_t self_used_mb;
    uint32_t system_free_mb;
    uint32_t system_total_mb;

    uint64_t graphic_cache_bytes;
    uint64_t graphic_cache_limit_bytes;
    uint64_t xp3_segment_cache_bytes;

    uint64_t psb_cache_bytes;
    uint32_t psb_cache_entries;
    uint32_t psb_cache_entry_limit;
    uint64_t psb_cache_hits;
    uint64_t psb_cache_misses;

    uint32_t archive_cache_entries;
    uint32_t archive_cache_limit;
    uint32_t autopath_cache_entries;
    uint32_t autopath_cache_limit;
    uint32_t autopath_table_entries;
    uint32_t reserved_u32;

    uint64_t reserved_u64[4];
    void *reserved_ptr[4];
} engine_memory_stats_t;

typedef enum engine_input_event_type_t {
    ENGINE_INPUT_EVENT_POINTER_DOWN = 1,
    ENGINE_INPUT_EVENT_POINTER_MOVE = 2,
    ENGINE_INPUT_EVENT_POINTER_UP = 3,
    ENGINE_INPUT_EVENT_POINTER_SCROLL = 4,
    ENGINE_INPUT_EVENT_KEY_DOWN = 5,
    ENGINE_INPUT_EVENT_KEY_UP = 6,
    ENGINE_INPUT_EVENT_TEXT_INPUT = 7,
    ENGINE_INPUT_EVENT_BACK = 8
} engine_input_event_type_t;

typedef enum engine_startup_state_t {
    ENGINE_STARTUP_STATE_IDLE = 0,
    ENGINE_STARTUP_STATE_RUNNING = 1,
    ENGINE_STARTUP_STATE_SUCCEEDED = 2,
    ENGINE_STARTUP_STATE_FAILED = 3
} engine_startup_state_t;

typedef struct engine_input_event_t {
    uint32_t struct_size;
    uint32_t type;
    uint64_t timestamp_micros;
    double x;
    double y;
    double delta_x;
    double delta_y;
    int32_t pointer_id;
    int32_t button;
    int32_t key_code;
    int32_t modifiers;
    uint32_t unicode_codepoint;
    uint32_t reserved_u32;
    uint64_t reserved_u64[2];
    void *reserved_ptr[2];
} engine_input_event_t;

/*
 * Returns runtime API version in out_api_version.
 * 将运行时 API 版本写入
 * out_api_version。
 * out_api_version must be non-null.
 * out_api_version
 * 不能为空。
 */
ENGINE_API_EXPORT engine_result_t
engine_get_runtime_api_version(uint32_t *out_api_version);

/*
 * Creates an engine handle.
 * 创建一个引擎句柄。
 * desc and out_handle
 * must be non-null.
 * desc 和 out_handle 不能为空。
 * out_handle is set only
 * when ENGINE_RESULT_OK is returned.
 * 仅当返回 ENGINE_RESULT_OK 时才会写入
 * out_handle。
 */
ENGINE_API_EXPORT engine_result_t
engine_create(const engine_create_desc_t *desc, engine_handle_t *out_handle);

/*
 * Destroys engine handle and releases all resources.
 *
 * 销毁引擎句柄并释放全部资源。
 * Idempotent: passing a null handle returns
 * ENGINE_RESULT_OK.
 * 幂等：传入空句柄时返回 ENGINE_RESULT_OK。

 */
ENGINE_API_EXPORT engine_result_t engine_destroy(engine_handle_t handle);

/*
 * Opens a game package/root directory.
 * 打开游戏包或游戏根目录。
 * handle
 * and game_root_path_utf8 must be non-null.
 * handle 和 game_root_path_utf8
 * 不能为空。
 * startup_script_utf8 may be null to use default startup script.

 * * startup_script_utf8 可为空，此时使用默认启动脚本。
 */
ENGINE_API_EXPORT engine_result_t
engine_open_game(engine_handle_t handle, const char *game_root_path_utf8,
                 const char *startup_script_utf8);

/*
 * Starts game opening asynchronously on a background worker.
 *
 * 在后台线程异步启动游戏打开流程。
 * Returns immediately when the startup task
 * is scheduled.
 * 当启动任务排队成功后立即返回。
 */
ENGINE_API_EXPORT engine_result_t
engine_open_game_async(engine_handle_t handle, const char *game_root_path_utf8,
                       const char *startup_script_utf8);

/*
 * Gets async startup state.
 * 获取异步启动状态。
 * out_state must be
 * non-null.
 * out_state 不能为空。
 */
ENGINE_API_EXPORT engine_result_t
engine_get_startup_state(engine_handle_t handle, uint32_t *out_state);

/*
 * Drains startup logs into caller buffer as UTF-8 text.
 * 将启动日志以
 * UTF-8 文本形式写入调用方缓冲区。
 * Each log line is terminated by '\n'.
 *
 * 每条日志都以 '\n' 结尾。
 * Returns bytes written in out_bytes_written.
 *
 * 实际写入字节数通过 out_bytes_written 返回。
 */
ENGINE_API_EXPORT engine_result_t
engine_drain_startup_logs(engine_handle_t handle, char *out_buffer,
                          uint32_t buffer_size, uint32_t *out_bytes_written);

/*
 * Ticks engine main loop once.
 * 驱动引擎主循环执行一帧。
 * handle must be
 * non-null.
 * handle 不能为空。
 * delta_ms is caller-provided elapsed
 * milliseconds.
 * delta_ms 由调用方提供，表示距离上一帧经过的毫秒数。

 */
ENGINE_API_EXPORT engine_result_t engine_tick(engine_handle_t handle,
                                              uint32_t delta_ms);

/*
 * Pauses runtime execution.
 * 暂停运行时执行。
 * Idempotent: calling pause
 * on a paused engine returns ENGINE_RESULT_OK.
 *
 * 幂等：对已暂停引擎重复调用会返回 ENGINE_RESULT_OK。
 */
ENGINE_API_EXPORT engine_result_t engine_pause(engine_handle_t handle);

/*
 * Resumes runtime execution.
 * 恢复运行时执行。
 * Idempotent: calling
 * resume on a running engine returns ENGINE_RESULT_OK.
 *
 * 幂等：对运行中的引擎重复调用会返回 ENGINE_RESULT_OK。
 */
ENGINE_API_EXPORT engine_result_t engine_resume(engine_handle_t handle);

/*
 * Sets runtime option by UTF-8 key/value pair.
 * 通过 UTF-8
 * 键值对设置运行时选项。
 * handle and option must be non-null.
 * handle 和
 * option 不能为空。
 */
ENGINE_API_EXPORT engine_result_t
engine_set_option(engine_handle_t handle, const engine_option_t *option);

/*
 * Sets logical render surface size in pixels.
 * 设置逻辑渲染 surface
 * 的像素尺寸。
 * width and height must be greater than zero.
 * width 和
 * height 必须大于零。
 */
ENGINE_API_EXPORT engine_result_t engine_set_surface_size(
    engine_handle_t handle, uint32_t width, uint32_t height);

/*
 * Gets current frame descriptor.
 * 获取当前帧描述信息。
 *
 * out_frame_desc->struct_size must be initialized by caller.
 *
 * 调用方必须先初始化 out_frame_desc->struct_size。
 */
ENGINE_API_EXPORT engine_result_t engine_get_frame_desc(
    engine_handle_t handle, engine_frame_desc_t *out_frame_desc);

/*
 * Reads current frame into caller-provided RGBA8888 buffer.
 *
 * 将当前帧读取到调用方提供的 RGBA8888 缓冲区。
 * out_pixels_size must be >=
 * stride_bytes * height from engine_get_frame_desc.
 * out_pixels_size
 * 必须大于等于 engine_get_frame_desc 返回的 stride_bytes * height。

 */
ENGINE_API_EXPORT engine_result_t engine_read_frame_rgba(
    engine_handle_t handle, void *out_pixels, size_t out_pixels_size);

/*
 * Gets host-native render window handle.
 * 获取宿主平台原生渲染窗口句柄。

 * * On macOS runtime build this is NSWindow*.
 * 在 macOS 运行时构建中返回
 * NSWindow*。
 * Returns ENGINE_RESULT_NOT_SUPPORTED on unsupported
 * platforms/builds.
 * 不支持的平台或构建将返回 ENGINE_RESULT_NOT_SUPPORTED。

 */
ENGINE_API_EXPORT engine_result_t
engine_get_host_native_window(engine_handle_t handle, void **out_window_handle);

/*
 * Gets host-native render view handle.
 * 获取宿主平台原生渲染视图句柄。
 *
 * On macOS runtime build this is NSView* (typically the GLFW content view).
 *
 * 在 macOS 运行时构建中返回 NSView*（通常是 GLFW content view）。
 * Returns
 * ENGINE_RESULT_NOT_SUPPORTED on unsupported platforms/builds.
 *
 * 不支持的平台或构建将返回 ENGINE_RESULT_NOT_SUPPORTED。
 */
ENGINE_API_EXPORT engine_result_t
engine_get_host_native_view(engine_handle_t handle, void **out_view_handle);

/*
 * Sends one input event to the runtime.
 * 向运行时发送一条输入事件。
 *
 * event->struct_size must be initialized by caller.
 * 调用方必须先初始化
 * event->struct_size。
 */
ENGINE_API_EXPORT engine_result_t
engine_send_input(engine_handle_t handle, const engine_input_event_t *event);

/*
 * Sets an IOSurface as the render target for the engine.
 * 将 IOSurface
 * 设置为引擎渲染目标。
 * When set, engine_tick renders directly to this
 * IOSurface (zero-copy),
 * 设置后 engine_tick 会直接渲染到该
 * IOSurface（零拷贝），
 * bypassing the glReadPixels path used by
 * engine_read_frame_rgba.
 * 绕过 engine_read_frame_rgba 使用的 glReadPixels
 * 读回路径。
 *
 * iosurface_id: The IOSurfaceID obtained from
 * IOSurfaceGetID().
 * iosurface_id：通过 IOSurfaceGetID() 获取的
 * IOSurfaceID。
 *               Pass 0 to detach and revert to the default
 * Pbuffer mode.
 *               传 0 表示解除绑定并退回默认的 Pbuffer 模式。

 * * width/height: Dimensions of the IOSurface in pixels.
 *
 * width/height：IOSurface 的像素尺寸。
 *
 * Platform: macOS only. Returns
 * ENGINE_RESULT_NOT_SUPPORTED on other platforms.
 * 平台限制：仅 macOS
 * 支持；其他平台返回 ENGINE_RESULT_NOT_SUPPORTED。
 */
ENGINE_API_EXPORT engine_result_t engine_set_render_target_iosurface(
    engine_handle_t handle, uint32_t iosurface_id, uint32_t width,
    uint32_t height);

/*
 * Sets an Android Surface (from SurfaceTexture) as the render target.
 * 将
 * Android Surface（来自 SurfaceTexture）设置为渲染目标。
 * When set,
 * engine_tick renders to an EGL WindowSurface created from the
 *
 * 设置后，engine_tick 会渲染到由 ANativeWindow 创建的 EGL WindowSurface。
 *
 * ANativeWindow. eglSwapBuffers() delivers frames directly to Flutter's
 *
 * eglSwapBuffers() 会把帧直接送到 Flutter 的 SurfaceTexture。
 * SurfaceTexture
 * (GPU zero-copy).
 * 整个路径为 GPU 零拷贝。
 *
 * native_window:
 * ANativeWindow* obtained from ANativeWindow_fromSurface().
 *
 * native_window：由 ANativeWindow_fromSurface() 获取的 ANativeWindow*。
 *
 * Pass NULL to detach and revert to the default Pbuffer mode.
 * 传 NULL
 * 表示解除绑定并退回默认 Pbuffer 模式。
 * width/height: Dimensions in pixels.

 * * width/height：渲染目标的像素尺寸。
 *
 * Platform: Android only. Returns
 * ENGINE_RESULT_NOT_SUPPORTED on other platforms.
 * 平台限制：仅 Android
 * 支持；其他平台返回 ENGINE_RESULT_NOT_SUPPORTED。
 */
ENGINE_API_EXPORT engine_result_t
engine_set_render_target_surface(engine_handle_t handle, void *native_window,
                                 uint32_t width, uint32_t height);

/*
 * Queries whether the last engine_tick produced a new rendered frame.
 *
 * 查询最近一次 engine_tick 是否产出了新渲染帧。
 *
 * out_flag:
 *   - 0: no new frame since the previous query
 *   - 0：自上一次查询以来没有新帧
 *   - 1: a new frame was rendered
 *   - 1：产生了新渲染帧
 *
 * This is mainly used by zero-copy presentation paths such as IOSurface
 * and SurfaceTexture to decide when the host should notify Flutter that a
 * frame is available.
 * 该标记主要供 IOSurface、SurfaceTexture 等零拷贝展示路径使用，
 * 以便宿主侧判断何时通知 Flutter 有新帧可用。
 */
ENGINE_API_EXPORT engine_result_t
engine_get_frame_rendered_flag(engine_handle_t handle, uint32_t *out_flag);

/*
 * Queries the graphics renderer information string.
 *
 * 查询图形渲染器信息字符串。
 * Writes a null-terminated UTF-8 string into
 * out_buffer describing
 * 将一个以空字符结尾的 UTF-8 字符串写入 out_buffer，

 * * the active graphics backend (e.g. "Metal", "OpenGL ES", "D3D11").
 *
 * 描述当前激活的图形后端（例如 "Metal"、"OpenGL ES"、"D3D11"）。
 *
 *
 * out_buffer and buffer_size must be non-null / > 0.
 * out_buffer 与
 * buffer_size 必须有效，且 buffer_size > 0。
 * If the buffer is too small the
 * string is truncated.
 * 如果缓冲区过小，字符串会被截断。
 * Returns
 * ENGINE_RESULT_INVALID_STATE if the runtime is not active.
 *
 * 若运行时未激活，则返回 ENGINE_RESULT_INVALID_STATE。
 */
ENGINE_API_EXPORT engine_result_t engine_get_renderer_info(
    engine_handle_t handle, char *out_buffer, uint32_t buffer_size);

/*
 * Gets runtime memory/cache statistics snapshot.
 *
 * 获取运行时内存与缓存统计快照。
 * out_stats->struct_size must be initialized
 * by caller.
 * 调用方必须先初始化 out_stats->struct_size。
 */
ENGINE_API_EXPORT engine_result_t engine_get_memory_stats(
    engine_handle_t handle, engine_memory_stats_t *out_stats);

/*
 * Returns last error message as UTF-8 null-terminated string.
 *
 * 返回最近一次错误信息，格式为 UTF-8 且以空字符结尾。
 * The returned pointer
 * remains valid until next API call on the same handle.
 * 返回指针在同一
 * handle 的下一次 API 调用前保持有效。
 * Returns empty string when no error is
 * recorded.
 * 如果当前没有记录错误，则返回空字符串。
 */
ENGINE_API_EXPORT const char *engine_get_last_error(engine_handle_t handle);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* KRKR2_ENGINE_API_H_ */
