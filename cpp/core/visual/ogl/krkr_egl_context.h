/**
 * @file krkr_egl_context.h
 * @brief Headless EGL context manager using ANGLE.
 *
 * Replaces GLFW window + GLViewImpl with an offscreen
 * EGL Pbuffer surface, providing a pure headless OpenGL ES 2.0
 * context that works on all platforms via ANGLE:
 *   - macOS  → Metal backend
 *   - Windows → D3D11 backend
 *   - Linux  → Desktop GL / Vulkan backend
 *   - Android → native GLES / Vulkan backend
 */
#pragma once

#include <cstdint>

// ANGLE / native EGL — always available (all platforms use ANGLE)
#define KRKR_HAS_EGL 1
#include <EGL/egl.h>

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

    /**
     * Attach an IOSurface as the render target FBO.
     * When attached, all rendering goes to the IOSurface instead
     * of the Pbuffer. The FBO is bound as GL_FRAMEBUFFER.
     *
     * @param iosurface_id  IOSurfaceID from IOSurfaceGetID()
     * @param width         IOSurface width in pixels
     * @param height        IOSurface height in pixels
     * @return true on success
     */
    bool AttachIOSurface(uint32_t iosurface_id, uint32_t width, uint32_t height);

    /**
     * Detach the IOSurface FBO, reverting to the default Pbuffer.
     */
    void DetachIOSurface();

    /**
     * Bind the IOSurface FBO (if attached) or the default FBO.
     * Call this before rendering a frame.
     */
    void BindRenderTarget();

    /**
     * @return true if an IOSurface is currently attached as render target.
     */
    bool HasIOSurface() const { return iosurface_fbo_ != 0; }

    /**
     * @return the IOSurface FBO width (0 if not attached).
     */
    uint32_t GetIOSurfaceWidth() const { return iosurface_width_; }

    /**
     * @return the IOSurface FBO height (0 if not attached).
     */
    uint32_t GetIOSurfaceHeight() const { return iosurface_height_; }

    /**
     * Attach an Android ANativeWindow as the render target.
     * Creates an EGL WindowSurface and binds it as the primary surface.
     * eglSwapBuffers() delivers frames directly to the SurfaceTexture.
     *
     * @param window  ANativeWindow* from ANativeWindow_fromSurface()
     * @param width   Surface width in pixels
     * @param height  Surface height in pixels
     * @return true on success
     */
    bool AttachNativeWindow(void* window, uint32_t width, uint32_t height);

    /**
     * Detach the ANativeWindow, reverting to Pbuffer mode.
     */
    void DetachNativeWindow();

    /**
     * @return true if rendering to an ANativeWindow (Android SurfaceTexture).
     */
    bool HasNativeWindow() const { return native_window_ != nullptr; }

    /**
     * @return the EGL WindowSurface for eglSwapBuffers (Android).
     */
    EGLSurface GetWindowSurface() const { return window_surface_; }

    /**
     * @return the ANativeWindow width (0 if not attached).
     */
    uint32_t GetNativeWindowWidth() const { return window_width_; }

    /**
     * @return the ANativeWindow height (0 if not attached).
     */
    uint32_t GetNativeWindowHeight() const { return window_height_; }

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
    void DestroyIOSurfaceResources();
    void DestroyNativeWindowResources();

    EGLDisplay display_  = EGL_NO_DISPLAY;
    EGLSurface surface_  = EGL_NO_SURFACE;
    EGLContext context_   = EGL_NO_CONTEXT;
    EGLConfig  config_    = nullptr;
    uint32_t   width_     = 0;
    uint32_t   height_    = 0;

    // IOSurface FBO resources (macOS zero-copy rendering)
    EGLSurface iosurface_pbuffer_   = EGL_NO_SURFACE;
    uint32_t   iosurface_fbo_       = 0;
    uint32_t   iosurface_texture_   = 0;
    uint32_t   iosurface_tex_target_= 0; // GL_TEXTURE_2D or GL_TEXTURE_RECTANGLE_ANGLE
    uint32_t   iosurface_rbo_depth_ = 0;
    uint32_t   iosurface_width_     = 0;
    uint32_t   iosurface_height_    = 0;
    uint32_t   iosurface_id_        = 0;

    // Android WindowSurface resources (SurfaceTexture zero-copy rendering)
    void*      native_window_       = nullptr; // ANativeWindow*
    EGLSurface window_surface_      = EGL_NO_SURFACE;
    uint32_t   window_width_        = 0;
    uint32_t   window_height_       = 0;
};

/**
 * Get the global engine EGL context singleton.
 * Initialized once during engine bootstrap.
 */
EGLContextManager& GetEngineEGLContext();

} // namespace krkr
