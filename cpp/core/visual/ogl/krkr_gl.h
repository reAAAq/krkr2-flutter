/**
 * @file krkr_gl.h
 * @brief Lightweight OpenGL state cache layer — replaces Cocos2d-x ccGLStateCache.
 *
 * Provides a thin wrapper around raw GL calls with basic state caching
 * to avoid redundant state changes. This is a drop-in replacement for
 * cocos2d::GL::* functions used in the KiriKiri2 rendering pipeline.
 */
#pragma once

#include "ogl_common.h"
#include <cstdint>
#include <functional>

namespace krkr {
namespace gl {

// ---------------------------------------------------------------------------
// Texture binding (with cache to skip redundant glBindTexture calls)
// ---------------------------------------------------------------------------

/**
 * Bind a 2D texture on texture unit GL_TEXTURE0.
 * Equivalent to cocos2d::GL::bindTexture2D().
 */
void BindTexture2D(GLuint textureId);

/**
 * Bind a 2D texture on the specified texture unit (0-based index).
 * Equivalent to cocos2d::GL::bindTexture2DN().
 */
void BindTexture2DN(unsigned int slot, GLuint textureId);

/**
 * Activate a texture unit.
 * Equivalent to cocos2d::GL::activeTexture().
 */
void ActiveTexture(GLenum textureUnit);

/**
 * Delete a GL texture and invalidate it from the cache.
 * Equivalent to cocos2d::GL::deleteTexture().
 */
void DeleteTexture(GLuint textureId);

// ---------------------------------------------------------------------------
// Shader program (with cache to skip redundant glUseProgram calls)
// ---------------------------------------------------------------------------

/**
 * Use a shader program (with cache).
 * Equivalent to cocos2d::GL::useProgram().
 */
void UseProgram(GLuint program);

// ---------------------------------------------------------------------------
// Vertex attribute management
// ---------------------------------------------------------------------------

/**
 * Enable vertex attribute arrays based on a bitmask.
 * Each bit i in `flags` means glEnableVertexAttribArray(i).
 * Previously enabled attributes not in the new mask are disabled.
 * Equivalent to cocos2d::GL::enableVertexAttribs().
 */
void EnableVertexAttribs(unsigned int flags);

// ---------------------------------------------------------------------------
// Blend state
// ---------------------------------------------------------------------------

/**
 * Reset cached blend state so next blend call goes through.
 * Equivalent to cocos2d::GL::blendResetToCache().
 */
void BlendResetToCache();

// ---------------------------------------------------------------------------
// Cache invalidation
// ---------------------------------------------------------------------------

/**
 * Invalidate all cached GL state. Call this when the GL context is
 * recreated or when switching contexts.
 */
void InvalidateStateCache();

// ---------------------------------------------------------------------------
// Renderer recreated callback (for Android GL context loss recovery)
// ---------------------------------------------------------------------------

/**
 * Register a callback to be invoked when the GL renderer is recreated
 * (e.g. after Android GL context loss). Replaces the Cocos2d-x
 * EVENT_RENDERER_RECREATED / EventListenerCustom mechanism.
 */
void OnRendererRecreated(std::function<void()> callback);

/**
 * Fire the renderer-recreated event. Called by the platform layer
 * when the GL context has been recreated.
 */
void FireRendererRecreated();

} // namespace gl
} // namespace krkr
