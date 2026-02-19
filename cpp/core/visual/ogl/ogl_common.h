#pragma once

#if defined(_WIN32) || defined(LINUX)
#if defined(_M_X64)
#define GLEW_STATIC
#endif
#include "GL/glew.h"
#endif

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

#if TARGET_OS_MAC
#include <OpenGL/gl.h>
#endif

#if TARGET_OS_IPHONE
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#endif

#ifdef __ANDROID__
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
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
