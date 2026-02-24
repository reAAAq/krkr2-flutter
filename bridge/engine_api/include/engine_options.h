/**
 * @file engine_options.h
 * @brief Well-known engine option key/value constants.
 *
 * These constants unify the option strings used across the C++ codebase
 * (engine_api, EngineBootstrap) to avoid typos and ensure consistency.
 */
#ifndef KRKR2_ENGINE_OPTIONS_H_
#define KRKR2_ENGINE_OPTIONS_H_

/* ── Option Keys ────────────────────────────────────────────────── */

/** ANGLE EGL backend selection (Android only; other platforms ignore). */
#define ENGINE_OPTION_ANGLE_BACKEND       "angle_backend"

/** Frame rate limit (0 = unlimited / follow vsync). */
#define ENGINE_OPTION_FPS_LIMIT           "fps_limit"

/** Render pipeline selection ("opengl" or "software"). */
#define ENGINE_OPTION_RENDERER            "renderer"

/* ── ANGLE Backend Values ───────────────────────────────────────── */

/** Use ANGLE's OpenGL ES backend (default). */
#define ENGINE_ANGLE_BACKEND_GLES         "gles"

/** Use ANGLE's Vulkan backend. */
#define ENGINE_ANGLE_BACKEND_VULKAN       "vulkan"

/* ── Renderer Values ────────────────────────────────────────────── */

#define ENGINE_RENDERER_OPENGL            "opengl"
#define ENGINE_RENDERER_SOFTWARE          "software"

#endif  /* KRKR2_ENGINE_OPTIONS_H_ */
