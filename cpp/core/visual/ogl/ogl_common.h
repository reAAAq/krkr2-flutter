#pragma once

// ---------------------------------------------------------------------------
// OpenGL headers — unified through ANGLE when available
// ---------------------------------------------------------------------------
#if defined(KRKR_USE_ANGLE)
// ANGLE provides a consistent GLES2 + EGL interface across all platforms:
//   macOS  → Metal backend
//   Windows → D3D11 backend
//   Linux  → Desktop GL / Vulkan backend
//   Android → native GLES / Vulkan backend
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#elif defined(_WIN32) || defined(LINUX)
#if defined(_M_X64)
#define GLEW_STATIC
#endif
#include "GL/glew.h"

#elif TARGET_OS_MAC
#include <OpenGL/gl.h>

#elif TARGET_OS_IPHONE
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>

#elif defined(__ANDROID__)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#endif

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

// ---------------------------------------------------------------------------
// GLES2 compatibility defines for desktop GL constants
// ---------------------------------------------------------------------------
#ifndef GL_DEPTH24_STENCIL8
#ifdef GL_DEPTH24_STENCIL8_OES
#define GL_DEPTH24_STENCIL8 GL_DEPTH24_STENCIL8_OES
#else
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#endif

#ifndef GL_READ_BUFFER
// GL_READ_BUFFER is not part of GLES2 core; used only behind #ifdef guards
#endif

#include <string>

bool TVPCheckGLExtension(const std::string &extname);

// ---------------------------------------------------------------------------
// CHECK_GL_ERROR_DEBUG — replacement for cocos2d-x macro
// In debug builds, checks for GL errors after each call.
// In release builds, this is a no-op.
// ---------------------------------------------------------------------------
#ifndef CHECK_GL_ERROR_DEBUG
#ifdef _DEBUG
#include <cassert>
#define CHECK_GL_ERROR_DEBUG()                                                 \
    do {                                                                       \
        GLenum __error = glGetError();                                         \
        if (__error) {                                                         \
            /* Log but don't assert — some errors are recoverable */           \
        }                                                                      \
    } while (false)
#else
#define CHECK_GL_ERROR_DEBUG() ((void)0)
#endif
#endif
