/**
 * @file engine_options.h
 * @brief Well-known engine option key/value constants.
 * @brief 引擎常用选项键值常量定义。
 *
 * These constants unify the option strings used across the C++ codebase
 * (engine_api, EngineBootstrap, runtime configuration) to avoid typos and
 * keep behavior consistent.
 * 这些常量统一了 C++ 代码库中的选项字符串
 * （如 engine_api、EngineBootstrap 与运行时配置），
 * 用于避免拼写错误并保持行为一致。
 */
#ifndef KRKR2_ENGINE_OPTIONS_H_
#define KRKR2_ENGINE_OPTIONS_H_

/* ── Option Keys / 选项键 ─────────────────────────────────────────────── */

/** ANGLE EGL backend selection / ANGLE EGL 后端选择。 */
#define ENGINE_OPTION_ANGLE_BACKEND "angle_backend"

/** Frame-rate limit / 帧率限制（0 表示不限速或跟随 vsync）。 */
#define ENGINE_OPTION_FPS_LIMIT "fps_limit"

/** Render pipeline selection / 渲染管线选择（"opengl" 或 "software"）。 */
#define ENGINE_OPTION_RENDERER "renderer"

/** Memory profile / 内存档位（"balanced" / "aggressive"）。 */
#define ENGINE_OPTION_MEMORY_PROFILE "memory_profile"

/** Runtime memory budget in MB / 运行时内存预算（单位 MB，0 表示自动）。 */
#define ENGINE_OPTION_MEMORY_BUDGET_MB "memory_budget_mb"

/** Memory-governor log interval in ms / 内存治理日志间隔（毫秒）。 */
#define ENGINE_OPTION_MEMORY_LOG_INTERVAL_MS "memory_log_interval_ms"

/** PSB resource cache budget in MB / PSB 资源缓存预算（MB）。 */
#define ENGINE_OPTION_PSB_CACHE_MB "psb_cache_mb"

/** PSB resource cache entry limit / PSB 资源缓存最大条目数。 */
#define ENGINE_OPTION_PSB_CACHE_ENTRIES "psb_cache_entries"

/** Archive cache entry limit / 归档缓存最大条目数。 */
#define ENGINE_OPTION_ARCHIVE_CACHE_COUNT "archive_cache_count"

/** Auto-path cache entry limit / 自动路径缓存最大条目数。 */
#define ENGINE_OPTION_AUTOPATH_CACHE_COUNT "autopath_cache_count"

/* ── ANGLE Backend Values / ANGLE 后端枚举值 ─────────────────────────── */

/** Use ANGLE's OpenGL ES backend / 使用 ANGLE 的 OpenGL ES 后端（默认）。 */
#define ENGINE_ANGLE_BACKEND_GLES "gles"

/** Use ANGLE's Vulkan backend / 使用 ANGLE 的 Vulkan 后端。 */
#define ENGINE_ANGLE_BACKEND_VULKAN "vulkan"

/* ── Renderer Values / 渲染器枚举值 ───────────────────────────────────── */

#define ENGINE_RENDERER_OPENGL "opengl"
#define ENGINE_RENDERER_SOFTWARE "software"

#define ENGINE_MEMORY_PROFILE_BALANCED "balanced"
#define ENGINE_MEMORY_PROFILE_AGGRESSIVE "aggressive"

#endif /* KRKR2_ENGINE_OPTIONS_H_ */
