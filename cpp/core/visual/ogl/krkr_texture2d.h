/**
 * @file krkr_texture2d.h
 * @brief Lightweight Texture2D replacement for Cocos2d-x CCTexture2D.
 *
 * This class provides just enough of the cocos2d::Texture2D API
 * that is actually used by RenderManager.cpp and RenderManager_ogl.cpp.
 * It is NOT a general-purpose texture class — only the methods actually
 * called in KiriKiri2 are implemented.
 *
 * Used in RenderManager.cpp:
 *   - new Texture2D / autorelease()
 *   - initWithData(data, dataLen, pixelFormat, width, height, size)
 *   - updateWithData(data, offsetX, offsetY, width, height)
 *   - getPixelsWide() / getPixelsHigh()
 *
 * Used in RenderManager_ogl.cpp (AdapterTexture2D):
 *   - _name, _contentSize, _maxS, _maxT, _pixelsWide, _pixelsHigh
 *   - _pixelFormat, _hasPremultipliedAlpha, _hasMipmaps
 *   - setGLProgram() — NOT needed (we remove this dependency)
 */
#pragma once

#include "ogl_common.h"
#include <cstddef>   // size_t, ssize_t
#include <cstring>   // memcpy

namespace krkr {

// ---------------------------------------------------------------------------
// PixelFormat — maps from cocos2d::Texture2D::PixelFormat
// ---------------------------------------------------------------------------
enum class PixelFormat {
    RGBA8888,
    RGB888,
    RGBA4444,
    RGB565,
    A8,
    I8,
    AI88,
    BGRA8888,
};

// ---------------------------------------------------------------------------
// Size — minimal replacement for cocos2d::Size
// ---------------------------------------------------------------------------
struct Size {
    float width  = 0;
    float height = 0;

    Size() = default;
    Size(float w, float h) : width(w), height(h) {}

    static const Size ZERO;
};

inline const Size Size::ZERO = {0, 0};

// ---------------------------------------------------------------------------
// Texture2D — lightweight replacement for cocos2d::Texture2D
//
// Implements reference counting with autorelease() support.
// autorelease() is a no-op that just returns this — the caller is
// expected to manage lifetime manually or via the existing KiriKiri2
// reference counting in iTVPTexture2D.
// ---------------------------------------------------------------------------
class Texture2D {
public:
    Texture2D() = default;
    virtual ~Texture2D() {
        if (_name && _ownsTexture) {
            glDeleteTextures(1, &_name);
        }
    }

    // --- Reference counting (simplified autorelease pool is not needed) ---
    void autorelease() {
        // In Cocos2d-x, autorelease adds to a pool. Here we simply mark
        // that external code manages the lifetime. The caller (iTVPTexture2D)
        // handles Release() properly.
        _autoreleased = true;
    }

    // --- Initialization ---
    /**
     * Initialize with pixel data. Compatible with cocos2d::Texture2D::initWithData.
     *
     * @param data       Pixel data pointer
     * @param dataLen    Byte length of data (unused, kept for API compat)
     * @param format     Pixel format
     * @param pixelsWide Width in pixels
     * @param pixelsHigh Height in pixels
     * @param contentSize Content size (unused, kept for API compat)
     */
    bool initWithData(const void *data, ssize_t dataLen, PixelFormat format,
                      int pixelsWide, int pixelsHigh, const Size &contentSize) {
        _pixelsWide  = pixelsWide;
        _pixelsHigh  = pixelsHigh;
        _pixelFormat = format;
        _contentSize = Size(static_cast<float>(pixelsWide),
                            static_cast<float>(pixelsHigh));

        GLenum glFormat = GL_RGBA;
        GLenum glType   = GL_UNSIGNED_BYTE;
        resolveGLFormat(format, glFormat, glType);

        if (_name == 0) {
            glGenTextures(1, &_name);
            _ownsTexture = true;
        }
        glBindTexture(GL_TEXTURE_2D, _name);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, glFormat, pixelsWide, pixelsHigh, 0,
                     glFormat, glType, data);
        return true;
    }

    /**
     * Update a sub-region of the texture. Compatible with cocos2d::Texture2D::updateWithData.
     */
    bool updateWithData(const void *data, int offsetX, int offsetY,
                        int width, int height) {
        if (_name == 0) return false;

        GLenum glFormat = GL_RGBA;
        GLenum glType   = GL_UNSIGNED_BYTE;
        resolveGLFormat(_pixelFormat, glFormat, glType);

        glBindTexture(GL_TEXTURE_2D, _name);
        glTexSubImage2D(GL_TEXTURE_2D, 0, offsetX, offsetY, width, height,
                        glFormat, glType, data);
        return true;
    }

    // --- Accessors ---
    GLuint getName()       const { return _name; }
    int    getPixelsWide() const { return _pixelsWide; }
    int    getPixelsHigh() const { return _pixelsHigh; }
    PixelFormat getPixelFormat() const { return _pixelFormat; }
    const Size& getContentSize() const { return _contentSize; }

    // --- Fields exposed for AdapterTexture2D in RenderManager_ogl.cpp ---
    // These mirror cocos2d::Texture2D protected members that AdapterTexture2D
    // directly accesses.
    GLuint _name = 0;
    Size   _contentSize;
    float  _maxS = 1.0f;
    float  _maxT = 1.0f;
    int    _pixelsWide = 0;
    int    _pixelsHigh = 0;
    PixelFormat _pixelFormat = PixelFormat::RGBA8888;
    bool   _hasPremultipliedAlpha = false;
    bool   _hasMipmaps = false;

protected:
    bool _ownsTexture = false;
    bool _autoreleased = false;

    static void resolveGLFormat(PixelFormat format, GLenum &glFormat, GLenum &glType) {
        switch (format) {
            case PixelFormat::RGBA8888:
                glFormat = GL_RGBA; glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::RGB888:
                glFormat = GL_RGB;  glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::RGBA4444:
                glFormat = GL_RGBA; glType = GL_UNSIGNED_SHORT_4_4_4_4; break;
            case PixelFormat::RGB565:
                glFormat = GL_RGB;  glType = GL_UNSIGNED_SHORT_5_6_5; break;
            case PixelFormat::A8:
            case PixelFormat::I8:
                glFormat = GL_LUMINANCE; glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::AI88:
                glFormat = GL_LUMINANCE_ALPHA; glType = GL_UNSIGNED_BYTE; break;
            case PixelFormat::BGRA8888:
#ifdef GL_BGRA
                glFormat = GL_BGRA; glType = GL_UNSIGNED_BYTE; break;
#else
                glFormat = GL_RGBA; glType = GL_UNSIGNED_BYTE; break;
#endif
        }
    }
};

} // namespace krkr
