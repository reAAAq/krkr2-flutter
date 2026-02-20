/**
 * @file krkr_egl_context.cpp
 * @brief Headless EGL context manager using ANGLE.
 */

#include "krkr_egl_context.h"
#include "krkr_gl.h"

#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include <spdlog/spdlog.h>

#if defined(__APPLE__)
#include <IOSurface/IOSurface.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES2/gl2ext.h>
#include <GLES2/gl2ext_angle.h>
#endif

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
    spdlog::default_logger()->flush();

    // glGetString may return nullptr if the context is not fully ready
    auto safeGlString = [](GLenum name) -> const char* {
        const char* s = reinterpret_cast<const char*>(glGetString(name));
        return s ? s : "(null)";
    };
    spdlog::info("GL_RENDERER: {}", safeGlString(GL_RENDERER));
    spdlog::info("GL_VERSION: {}", safeGlString(GL_VERSION));
    spdlog::default_logger()->flush();

    // Check for GL errors after context creation
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        spdlog::warn("GL error after context creation: 0x{:x}", err);
    }

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
// IOSurface FBO attachment (macOS zero-copy rendering)
// ---------------------------------------------------------------------------

bool EGLContextManager::AttachIOSurface(uint32_t iosurface_id,
                                         uint32_t width, uint32_t height) {
#if defined(__APPLE__)
    if (context_ == EGL_NO_CONTEXT) {
        spdlog::error("AttachIOSurface: EGL context not initialized");
        return false;
    }
    if (iosurface_id == 0 || width == 0 || height == 0) {
        spdlog::error("AttachIOSurface: invalid parameters (id={}, {}x{})",
                      iosurface_id, width, height);
        return false;
    }

    // Clean up previous IOSurface resources
    DestroyIOSurfaceResources();

    // Look up the IOSurface by ID
    IOSurfaceRef surface = IOSurfaceLookup(iosurface_id);
    if (!surface) {
        spdlog::error("AttachIOSurface: IOSurfaceLookup({}) failed", iosurface_id);
        return false;
    }

    // Query the texture target supported by this config
    EGLint textureTarget = 0;
    eglGetConfigAttrib(display_, config_, EGL_BIND_TO_TEXTURE_TARGET_ANGLE,
                       &textureTarget);
    if (textureTarget == 0) {
        // Fallback: try EGL_TEXTURE_RECTANGLE_ANGLE (common on macOS Metal)
        textureTarget = EGL_TEXTURE_RECTANGLE_ANGLE;
    }
    spdlog::info("AttachIOSurface: EGL_BIND_TO_TEXTURE_TARGET_ANGLE = 0x{:x}",
                 textureTarget);

    // Determine the corresponding GL texture target
    GLenum glTextureTarget = GL_TEXTURE_2D;
    if (textureTarget == EGL_TEXTURE_RECTANGLE_ANGLE) {
        glTextureTarget = GL_TEXTURE_RECTANGLE_ANGLE;
    }

    // Create a Pbuffer from the IOSurface using ANGLE's extension
    // EGL_ANGLE_iosurface_client_buffer
    const EGLint pbufferAttribs[] = {
        EGL_WIDTH,                         static_cast<EGLint>(width),
        EGL_HEIGHT,                        static_cast<EGLint>(height),
        EGL_IOSURFACE_PLANE_ANGLE,         0,
        EGL_TEXTURE_TARGET,                textureTarget,
        EGL_TEXTURE_INTERNAL_FORMAT_ANGLE, GL_BGRA_EXT,
        EGL_TEXTURE_FORMAT,                EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TYPE_ANGLE,            GL_UNSIGNED_BYTE,
        EGL_NONE,                          EGL_NONE,
    };

    EGLSurface pbuffer = eglCreatePbufferFromClientBuffer(
        display_, EGL_IOSURFACE_ANGLE,
        static_cast<EGLClientBuffer>(surface),
        config_, pbufferAttribs);

    CFRelease(surface);

    if (pbuffer == EGL_NO_SURFACE) {
        spdlog::error("AttachIOSurface: eglCreatePbufferFromClientBuffer failed: 0x{:x}",
                      eglGetError());
        return false;
    }

    // Create a GL texture and bind the IOSurface pbuffer to it
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(glTextureTarget, tex);
    glTexParameteri(glTextureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(glTextureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Bind the pbuffer to the texture (this connects the IOSurface content)
    EGLBoolean bindResult = eglBindTexImage(display_, pbuffer, EGL_BACK_BUFFER);
    if (!bindResult) {
        spdlog::error("AttachIOSurface: eglBindTexImage failed: 0x{:x}",
                      eglGetError());
        glDeleteTextures(1, &tex);
        eglDestroySurface(display_, pbuffer);
        return false;
    }

    // Create FBO and attach the IOSurface-backed texture
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            glTextureTarget, tex, 0);

    // Create stencil renderbuffer
    GLuint rbo = 0;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                          static_cast<GLsizei>(width),
                          static_cast<GLsizei>(height));
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                               GL_RENDERBUFFER, rbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        spdlog::error("AttachIOSurface: FBO incomplete: 0x{:x}", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        eglReleaseTexImage(display_, pbuffer, EGL_BACK_BUFFER);
        glDeleteTextures(1, &tex);
        glDeleteRenderbuffers(1, &rbo);
        eglDestroySurface(display_, pbuffer);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    iosurface_pbuffer_ = pbuffer;
    iosurface_fbo_ = fbo;
    iosurface_texture_ = tex;
    iosurface_tex_target_ = glTextureTarget;
    iosurface_rbo_depth_ = rbo;
    iosurface_width_ = width;
    iosurface_height_ = height;
    iosurface_id_ = iosurface_id;

    spdlog::info("AttachIOSurface: success (id={}, {}x{}, fbo={}, tex={}, target=0x{:x})",
                 iosurface_id, width, height, fbo, tex, glTextureTarget);
    return true;
#else
    (void)iosurface_id;
    (void)width;
    (void)height;
    spdlog::error("AttachIOSurface: not supported on this platform");
    return false;
#endif
}

void EGLContextManager::DetachIOSurface() {
    DestroyIOSurfaceResources();
    spdlog::info("DetachIOSurface: reverted to Pbuffer mode");
}

void EGLContextManager::BindRenderTarget() {
    if (iosurface_fbo_ != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, iosurface_fbo_);
        glViewport(0, 0, static_cast<GLsizei>(iosurface_width_),
                   static_cast<GLsizei>(iosurface_height_));
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, static_cast<GLsizei>(width_),
                   static_cast<GLsizei>(height_));
    }
}

void EGLContextManager::DestroyIOSurfaceResources() {
    if (iosurface_fbo_ != 0) {
        glDeleteFramebuffers(1, &iosurface_fbo_);
        iosurface_fbo_ = 0;
    }
    if (iosurface_rbo_depth_ != 0) {
        glDeleteRenderbuffers(1, &iosurface_rbo_depth_);
        iosurface_rbo_depth_ = 0;
    }
    if (iosurface_texture_ != 0) {
        // Release the texture image binding before deleting
        if (iosurface_pbuffer_ != EGL_NO_SURFACE && display_ != EGL_NO_DISPLAY) {
            eglReleaseTexImage(display_, iosurface_pbuffer_, EGL_BACK_BUFFER);
        }
        glDeleteTextures(1, &iosurface_texture_);
        iosurface_texture_ = 0;
    }
    if (iosurface_pbuffer_ != EGL_NO_SURFACE && display_ != EGL_NO_DISPLAY) {
        eglDestroySurface(display_, iosurface_pbuffer_);
        iosurface_pbuffer_ = EGL_NO_SURFACE;
    }
    iosurface_tex_target_ = 0;
    iosurface_width_ = 0;
    iosurface_height_ = 0;
    iosurface_id_ = 0;
}

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------

EGLContextManager& GetEngineEGLContext() {
    static EGLContextManager instance;
    return instance;
}

} // namespace krkr
