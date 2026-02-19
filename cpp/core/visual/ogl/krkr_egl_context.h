/**
 * @file krkr_egl_context.h
 * @brief Headless EGL context manager using ANGLE.
 *
 * Replaces GLFW window + Cocos2d-x GLViewImpl with an offscreen
 * EGL Pbuffer surface, providing a pure headless OpenGL ES 2.0
 * context that works on all platforms via ANGLE:
 *   - macOS  → Metal backend
 *   - Windows → D3D11 backend
 *   - Linux  → Desktop GL / Vulkan backend
 *   - Android → native GLES / Vulkan backend
 */
#pragma once

#include <cstdint>

// ANGLE / native EGL availability
#if defined(KRKR_USE_ANGLE) || defined(__ANDROID__)
#define KRKR_HAS_EGL 1
#include <EGL/egl.h>
#else
#define KRKR_HAS_EGL 0
// Provide stub types so the header compiles without EGL
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;
#define EGL_NO_DISPLAY  ((EGLDisplay)0)
#define EGL_NO_SURFACE  ((EGLSurface)0)
#define EGL_NO_CONTEXT  ((EGLContext)0)
#endif

namespace krkr {

class EGLContextManager {
public:
    EGLContextManager() = default;
    ~EGLContextManager();

    // Non-copyable
    EGLContextManager(const EGLContextManager&) = delete;
    EGLContextManager& operator=(const EGLContextManager&) = delete;

    /**
     * Initialize the EGL display, create a Pbuffer surface and
     * an OpenGL ES 2.0 context.  Makes the context current.
     *
     * @param width   Initial surface width in pixels
     * @param height  Initial surface height in pixels
     * @return true on success
     */
    bool Initialize(uint32_t width, uint32_t height);

    /**
     * Destroy the EGL context, surface, and display.
     */
    void Destroy();

    /**
     * Make this context current on the calling thread.
     */
    bool MakeCurrent();

    /**
     * Release the context from the calling thread.
     */
    bool ReleaseCurrent();

    /**
     * Resize the Pbuffer surface. This destroys the old surface
     * and creates a new one with the requested dimensions.
     * The context is re-made current after resize.
     *
     * @param width   New width in pixels
     * @param height  New height in pixels
     * @return true on success
     */
    bool Resize(uint32_t width, uint32_t height);

    /**
     * Swap buffers (no-op for Pbuffer, but kept for interface
     * compatibility if we later switch to window surfaces).
     */
    void SwapBuffers();

    // Accessors
    uint32_t GetWidth()   const { return width_; }
    uint32_t GetHeight()  const { return height_; }
    bool     IsValid()    const { return context_ != EGL_NO_CONTEXT; }

    EGLDisplay GetDisplay() const { return display_; }
    EGLSurface GetSurface() const { return surface_; }
    EGLContext GetContext() const { return context_; }

private:
    bool CreateSurface(uint32_t width, uint32_t height);
    void DestroySurface();

    EGLDisplay display_  = EGL_NO_DISPLAY;
    EGLSurface surface_  = EGL_NO_SURFACE;
    EGLContext context_   = EGL_NO_CONTEXT;
    EGLConfig  config_    = nullptr;
    uint32_t   width_     = 0;
    uint32_t   height_    = 0;
};

/**
 * Get the global engine EGL context singleton.
 * Initialized once during engine bootstrap.
 */
EGLContextManager& GetEngineEGLContext();

} // namespace krkr
