/**
 * @file krkr_egl_context.cpp
 * @brief Headless EGL context manager using ANGLE.
 */

#include "krkr_egl_context.h"
#include "krkr_gl.h"

#include <GLES2/gl2.h>
#include <spdlog/spdlog.h>

namespace krkr {

// ---------------------------------------------------------------------------
// EGLContextManager
// ---------------------------------------------------------------------------

EGLContextManager::~EGLContextManager() {
    Destroy();
}

bool EGLContextManager::Initialize(uint32_t width, uint32_t height) {
    if (context_ != EGL_NO_CONTEXT) {
        spdlog::warn("EGLContextManager::Initialize called but context already exists, destroying first");
        Destroy();
    }

    // Get the default ANGLE display
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        spdlog::error("eglGetDisplay failed: 0x{:x}", eglGetError());
        return false;
    }

    EGLint majorVersion, minorVersion;
    if (!eglInitialize(display_, &majorVersion, &minorVersion)) {
        spdlog::error("eglInitialize failed: 0x{:x}", eglGetError());
        display_ = EGL_NO_DISPLAY;
        return false;
    }
    spdlog::info("EGL initialized: version {}.{}", majorVersion, minorVersion);
    spdlog::info("EGL vendor: {}", eglQueryString(display_, EGL_VENDOR));
    spdlog::info("EGL version string: {}", eglQueryString(display_, EGL_VERSION));

    // Choose a config that supports Pbuffer + GLES2
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      0,
        EGL_STENCIL_SIZE,    8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(display_, configAttribs, &config_, 1, &numConfigs) ||
        numConfigs == 0) {
        spdlog::error("eglChooseConfig failed: 0x{:x}, numConfigs={}", eglGetError(), numConfigs);
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        return false;
    }

    // Create the Pbuffer surface
    if (!CreateSurface(width, height)) {
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        return false;
    }

    // Create GLES2 context
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        spdlog::error("eglCreateContext failed: 0x{:x}", eglGetError());
        DestroySurface();
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
        return false;
    }

    // Make current
    if (!MakeCurrent()) {
        spdlog::error("Initial MakeCurrent failed");
        Destroy();
        return false;
    }

    spdlog::info("ANGLE EGL context created successfully: {}x{}", width, height);
    spdlog::info("GL_RENDERER: {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    spdlog::info("GL_VERSION: {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // Invalidate the GL state cache since we have a fresh context
    krkr::gl::InvalidateStateCache();

    return true;
}

void EGLContextManager::Destroy() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }

        DestroySurface();

        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
    config_ = nullptr;
    width_ = 0;
    height_ = 0;
}

bool EGLContextManager::MakeCurrent() {
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE ||
        context_ == EGL_NO_CONTEXT) {
        return false;
    }
    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        spdlog::error("eglMakeCurrent failed: 0x{:x}", eglGetError());
        return false;
    }
    return true;
}

bool EGLContextManager::ReleaseCurrent() {
    if (display_ == EGL_NO_DISPLAY) return false;
    return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                          EGL_NO_CONTEXT) == EGL_TRUE;
}

bool EGLContextManager::Resize(uint32_t width, uint32_t height) {
    if (width == width_ && height == height_) {
        return true; // No change needed
    }

    if (display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT) {
        spdlog::error("Cannot resize: EGL not initialized");
        return false;
    }

    // Release the current surface
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    DestroySurface();

    // Create new surface with updated size
    if (!CreateSurface(width, height)) {
        spdlog::error("Failed to create new surface during resize");
        return false;
    }

    // Re-make current
    if (!MakeCurrent()) {
        spdlog::error("MakeCurrent failed after resize");
        return false;
    }

    spdlog::info("EGL surface resized to {}x{}", width, height);
    return true;
}

void EGLContextManager::SwapBuffers() {
    if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
        eglSwapBuffers(display_, surface_);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool EGLContextManager::CreateSurface(uint32_t width, uint32_t height) {
    EGLint pbufferAttribs[] = {
        EGL_WIDTH,  static_cast<EGLint>(width),
        EGL_HEIGHT, static_cast<EGLint>(height),
        EGL_NONE
    };
    surface_ = eglCreatePbufferSurface(display_, config_, pbufferAttribs);
    if (surface_ == EGL_NO_SURFACE) {
        spdlog::error("eglCreatePbufferSurface failed: 0x{:x}", eglGetError());
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

void EGLContextManager::DestroySurface() {
    if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
}

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------

EGLContextManager& GetEngineEGLContext() {
    static EGLContextManager instance;
    return instance;
}

} // namespace krkr
