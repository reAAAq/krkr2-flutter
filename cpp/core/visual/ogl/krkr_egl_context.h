/**
 * @file krkr_egl_context.h
 * @brief Headless EGL context manager using
 * ANGLE.
 * @brief 基于 ANGLE 的无窗口 EGL 上下文管理器。
 *
 * Replaces GLFW
 * window + GLViewImpl with an offscreen
 * EGL Pbuffer surface, providing a
 * pure headless OpenGL ES 2.0
 * context that works on all platforms via
 * ANGLE:
 * 使用离屏 EGL Pbuffer surface 替代 GLFW window + GLViewImpl，
 *
 * 提供一个纯粹的无窗口 OpenGL ES 2.0 上下文，并通过 ANGLE 适配多平台：
 *   -
 * macOS  → Metal backend
 *   - macOS  → Metal 后端
 *   - Windows → D3D11
 * backend
 *   - Windows → D3D11 后端
 *   - Linux  → Desktop GL / Vulkan
 * backend
 *   - Linux  → Desktop GL / Vulkan 后端
 *   - Android → native GLES
 * / Vulkan backend
 *   - Android → 原生 GLES / Vulkan 后端
 */
#pragma once

#include <cstdint>

// ANGLE / native EGL — always available.
// ANGLE / 原生 EGL 始终可用；当前所有平台都通过 ANGLE 接入。
#define KRKR_HAS_EGL 1
#include <EGL/egl.h>

#include "angle_backend.h" // krkr::AngleBackend

namespace krkr {

    class EGLContextManager {
    public:
        EGLContextManager() = default;
        ~EGLContextManager();

        // Non-copyable / 禁止拷贝
        EGLContextManager(const EGLContextManager &) = delete;
        EGLContextManager &operator=(const EGLContextManager &) = delete;

        /**
         * Initialize the EGL display, create a Pbuffer surface and
     * an
         * OpenGL ES 2.0 context.  Makes the context current.
     * 初始化 EGL
         * display，创建 Pbuffer surface 和 OpenGL ES 2.0 context，
     *
         * 并将该 context 设为当前线程的 current context。
     *
         * @param width   Initial surface width in pixels / 初始 surface
         * 宽度（像素）
     * @param height  Initial surface height in pixels /
         * 初始 surface 高度（像素）
     * @return true on success / 成功返回
         * true
     */
        bool Initialize(uint32_t width, uint32_t height,
                        AngleBackend backend = AngleBackend::OpenGLES);

        /**
         * Destroy the EGL context, surface, and display.
     * 销毁 EGL
         * context、surface 与 display。
     */
        void Destroy();

        /**
         * Make this context current on the calling thread.
     * 将该 context
         * 设为调用线程的 current context。
     */
        bool MakeCurrent();

        /**
         * Release the context from the calling thread.
     * 将当前线程与该
         * context 解绑。
     */
        bool ReleaseCurrent();

        /**
         * Resize the Pbuffer surface. This destroys the old surface
     * and
         * creates a new one with the requested dimensions.
     * The context
         * is re-made current after resize.
     * 调整 Pbuffer surface
         * 大小。该操作会销毁旧 surface，
     * 并按新尺寸重建；重建后会再次将
         * context 设为 current。
     *
         * @param width   New width in pixels / 新宽度（像素）
     * @param
         * height  New height in pixels / 新高度（像素）
     * @return true on
         * success / 成功返回 true
     */
        bool Resize(uint32_t width, uint32_t height);

        /**
         * Attach an IOSurface as the render target FBO.
     * When attached,
         * all rendering goes to the IOSurface instead
     * of the Pbuffer.
         * The FBO is bound as GL_FRAMEBUFFER.
     * 将 IOSurface
         * 绑定为渲染目标 FBO。
     * 绑定后所有渲染都会输出到
         * IOSurface，而不是默认的 Pbuffer，
     * 对应 FBO 会以 GL_FRAMEBUFFER
         * 形式绑定。
     *
         * @param iosurface_id  IOSurfaceID from IOSurfaceGetID() / IOSurface
         * 标识
     * @param width         IOSurface width in pixels /
         * IOSurface 宽度
     * @param height        IOSurface height in pixels
         * / IOSurface 高度
     * @return true on success / 成功返回 true

         */
        bool AttachIOSurface(uint32_t iosurface_id, uint32_t width,
                             uint32_t height);

        /**
         * Detach the IOSurface FBO, reverting to the default Pbuffer.
     *
         * 解除 IOSurface FBO 绑定，回退到默认 Pbuffer。
     */
        void DetachIOSurface();

        /**
         * Bind the IOSurface FBO (if attached) or the default FBO.
     * 绑定
         * IOSurface FBO（如果已附加）或默认 FBO。
     * Call this before
         * rendering a frame.
     * 该函数应在每帧渲染前调用。
 */
        void BindRenderTarget();

        /**
         * @return true if an IOSurface is currently attached as render target.
         *         若当前已附加 IOSurface 作为渲染目标，则返回 true。
         */
        bool HasIOSurface() const { return iosurface_fbo_ != 0; }

        /**
         * @return the IOSurface FBO width (0 if not attached).
     * 返回
         * IOSurface FBO 宽度；若未附加则返回 0。
     */
        uint32_t GetIOSurfaceWidth() const { return iosurface_width_; }

        /**
         * @return the IOSurface FBO height (0 if not attached).
     * 返回
         * IOSurface FBO 高度；若未附加则返回 0。
     */
        uint32_t GetIOSurfaceHeight() const { return iosurface_height_; }

        /**
         * Initialize EGL directly with an ANativeWindow (Android only).
     *
         * Unlike Initialize() which creates a Pbuffer, this creates
     * a
         * WindowSurface directly, avoiding Pbuffer-related failures
     * on
         * some Android devices/drivers.
     * 直接用 ANativeWindow 初始化
         * EGL（仅 Android）。
     * 与 Initialize() 先创建 Pbuffer
         * 不同，这里会直接创建 WindowSurface，
     * 从而规避部分 Android
         * 设备/驱动上的 Pbuffer 相关失败。
     * After this call,
         * IsValid()==true and HasNativeWindow()==true.
     *
         * 调用成功后，IsValid()==true 且 HasNativeWindow()==true。
 *
         * @param window  ANativeWindow* from ANativeWindow_fromSurface() /
         * 原生窗口
     * @param width   Surface width in pixels / Surface
         * 宽度
     * @param height  Surface height in pixels / Surface 高度

         * * @return true on success / 成功返回 true
     */
        bool
        InitializeWithWindow(void *window, uint32_t width, uint32_t height,
                             AngleBackend backend = AngleBackend::OpenGLES);

        /**
         * Attach an Android ANativeWindow as the render target.
     * Creates
         * an EGL WindowSurface and binds it as the primary surface.
     *
         * eglSwapBuffers() delivers frames directly to the SurfaceTexture.

         * * 将 Android ANativeWindow 绑定为渲染目标。
     * 该函数会创建 EGL
         * WindowSurface，并将其设为主 surface；
     * 后续 eglSwapBuffers()
         * 会把帧直接送到 SurfaceTexture。
     *
         * @param window  ANativeWindow* from ANativeWindow_fromSurface() /
         * 原生窗口
     * @param width   Surface width in pixels / Surface
         * 宽度
     * @param height  Surface height in pixels / Surface 高度

         * * @return true on success / 成功返回 true
     */
        bool AttachNativeWindow(void *window, uint32_t width, uint32_t height);

        /**
         * Detach the ANativeWindow, reverting to Pbuffer mode.
     * 解除
         * ANativeWindow 绑定，回退到 Pbuffer 模式。
     */
        void DetachNativeWindow();

        /**
         * @return true if rendering to an ANativeWindow (Android
         * SurfaceTexture).
     *         若当前渲染目标是
         * ANativeWindow（Android SurfaceTexture），返回 true。
     */
        bool HasNativeWindow() const { return native_window_ != nullptr; }

        /**
         * @return the EGL WindowSurface for eglSwapBuffers (Android).
     *
         * 返回用于 eglSwapBuffers 的 EGL WindowSurface（Android）。
 */
        EGLSurface GetWindowSurface() const { return window_surface_; }

        /**
         * @return the ANativeWindow width (0 if not attached).
     * 返回
         * ANativeWindow 宽度；未附加时返回 0。
     */
        uint32_t GetNativeWindowWidth() const { return window_width_; }

        /**
         * @return the ANativeWindow height (0 if not attached).
     * 返回
         * ANativeWindow 高度；未附加时返回 0。
     */
        uint32_t GetNativeWindowHeight() const { return window_height_; }

        /**
         * Update stored native window dimensions after SurfaceTexture resize.

         * * setDefaultBufferSize() changes the buffer dimensions; the EGL
         * surface
     * auto-adapts on next eglSwapBuffers. We track the new
         * size here so
     * UpdateDrawBuffer() calculates the correct
         * letterbox viewport.
     * 在 SurfaceTexture
         * 尺寸变化后更新缓存的原生窗口尺寸。
     * setDefaultBufferSize()
         * 会改变 buffer 尺寸，EGL surface 会在下一次
     * eglSwapBuffers
         * 时自动适配；这里记录最新尺寸，确保 UpdateDrawBuffer()
     *
         * 可以计算出正确的 letterbox viewport。
     */
        void UpdateNativeWindowSize(uint32_t w, uint32_t h) {
            window_width_ = w;
            window_height_ = h;
        }

        /**
         * Mark the current frame as dirty (new content rendered).
     *
         * 标记当前帧为脏帧（已有新内容渲染出来）。
     * Must be called after
         * UpdateDrawBuffer() completes rendering.
     * 必须在
         * UpdateDrawBuffer() 完成渲染后调用。
     */
        void MarkFrameDirty() { frame_dirty_ = true; }

        /**
         * Check and consume the dirty flag.
     * 检查并消费 dirty 标志位。

         * * @return true if the frame was dirty (and flag is now cleared).
 *
         * 如果本帧为脏帧则返回 true，同时清除标志位。
 */
        bool ConsumeFrameDirty() {
            if(!frame_dirty_)
                return false;
            frame_dirty_ = false;
            return true;
        }

        /**
         * Peek at the dirty flag without consuming it.
     * 查看 dirty
         * 标志位但不消费它。
     */
        bool IsFrameDirty() const { return frame_dirty_; }

        // Accessors / 基础访问器
        uint32_t GetWidth() const { return width_; }
        uint32_t GetHeight() const { return height_; }
        bool IsValid() const { return context_ != EGL_NO_CONTEXT; }

        EGLDisplay GetDisplay() const { return display_; }
        EGLSurface GetSurface() const { return surface_; }
        EGLContext GetContext() const { return context_; }

    private:
        bool CreateSurface(uint32_t width, uint32_t height);
        void DestroySurface();
        void DestroyIOSurfaceResources();
        void DestroyNativeWindowResources();

        /**
         * Acquire ANGLE EGL display with the specified backend.
     * On
         * Android, uses eglGetPlatformDisplayEXT with Vulkan → OpenGLES
         * fallback.
     * On other platforms, falls back to
         * eglGetDisplay(EGL_DEFAULT_DISPLAY).
     * Updates `backend` in-place
         * if a fallback was triggered.
     * 按给定后端获取 ANGLE EGL
         * display。
     * 在 Android 上优先通过 eglGetPlatformDisplayEXT 走
         * Vulkan，
     * 失败时自动回退到 OpenGLES；其他平台则回退到
         * eglGetDisplay(EGL_DEFAULT_DISPLAY)。
     * 如果发生回退，`backend`
         * 会被原地改写为实际后端。
     */
        EGLDisplay AcquireAngleDisplay(AngleBackend &backend);

        EGLDisplay display_ = EGL_NO_DISPLAY;
        EGLSurface surface_ = EGL_NO_SURFACE;
        EGLContext context_ = EGL_NO_CONTEXT;
        EGLConfig config_ = nullptr;
        uint32_t width_ = 0;
        uint32_t height_ = 0;

        // ANGLE backend type / ANGLE 后端类型
        // Android 可切换；macOS/iOS 固定走 Metal。
        AngleBackend angle_backend_ = AngleBackend::OpenGLES;

        // IOSurface FBO resources / IOSurface FBO 资源（macOS 零拷贝渲染）
        EGLSurface iosurface_pbuffer_ = EGL_NO_SURFACE;
        uint32_t iosurface_fbo_ = 0;
        uint32_t iosurface_texture_ = 0;
        uint32_t iosurface_tex_target_ =
            0; // GL_TEXTURE_2D or GL_TEXTURE_RECTANGLE_ANGLE
        uint32_t iosurface_rbo_depth_ = 0;
        uint32_t iosurface_width_ = 0;
        uint32_t iosurface_height_ = 0;
        uint32_t iosurface_id_ = 0;

        // Android WindowSurface resources / Android WindowSurface 资源
        // 对应 SurfaceTexture 零拷贝渲染路径。
        void *native_window_ = nullptr; // ANativeWindow*
        EGLSurface window_surface_ = EGL_NO_SURFACE;
        uint32_t window_width_ = 0;
        uint32_t window_height_ = 0;

        // Frame dirty flag / 脏帧标记
        // Prevents eglSwapBuffers on frames where UpdateDrawBuffer() was not
        // called, avoiding double-buffer flicker.
        // 用于避免在未调用 UpdateDrawBuffer() 的帧上执行 eglSwapBuffers，
        // 从而减少双缓冲闪烁（当前帧与陈旧 back-buffer 交替显示）。
        bool frame_dirty_ = false;
    };

    /**
 * Get the global engine EGL context singleton.
 * 获取全局引擎 EGL
     * 上下文单例。
 * Initialized once during engine bootstrap.
 *
     * 该单例会在引擎启动阶段初始化一次。
 */
    EGLContextManager &GetEngineEGLContext();

} // namespace krkr
