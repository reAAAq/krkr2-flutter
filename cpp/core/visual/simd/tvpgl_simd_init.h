/*
 * KrKr2 Engine - Highway SIMD Initialization
 *
 * Call TVPGL_SIMD_Init() after TVPGL_C_Init() to override scalar
 * implementations with Highway SIMD-optimized versions.
 */

#ifndef __TVPGL_SIMD_INIT_H__
#define __TVPGL_SIMD_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Initialize SIMD-optimized pixel blending functions using Google Highway.
/// Call this AFTER TVPGL_C_Init() in TVPInitTVPGL() to override scalar
/// implementations with SIMD-optimized versions.
void TVPGL_SIMD_Init();

#ifdef __cplusplus
}
#endif

#endif  // __TVPGL_SIMD_INIT_H__
