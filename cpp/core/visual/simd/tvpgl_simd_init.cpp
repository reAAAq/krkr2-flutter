/*
 * KrKr2 Engine - Highway SIMD Initialization
 *
 * Overrides the scalar function pointers with Highway SIMD-optimized versions.
 * Called from TVPInitTVPGL() after TVPGL_C_Init().
 */

#include "tvpgl_simd_init.h"
#include "tjsTypes.h"
#include "tvpgl.h"

// Forward declarations of Highway SIMD implementations
extern "C" {

// Phase 1: Copy/Fill
void TVPCopyOpaqueImage_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len);
void TVPFillARGB_hwy(tjs_uint32 *dest, tjs_int len, tjs_uint32 value);

// Phase 2: Alpha Blend (4 variants)
void TVPAlphaBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len);
void TVPAlphaBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len);
void TVPAlphaBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len, tjs_int opa);
void TVPAlphaBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len, tjs_int opa);

// Phase 2: Add Blend (4 variants)
void TVPAddBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len);
void TVPAddBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len);
void TVPAddBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len, tjs_int opa);
void TVPAddBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len, tjs_int opa);

// Phase 2: Sub Blend (4 variants)
void TVPSubBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len);
void TVPSubBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len);
void TVPSubBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len, tjs_int opa);
void TVPSubBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len, tjs_int opa);

}  // extern "C"

void TVPGL_SIMD_Init() {
    // =====================================================================
    // Phase 1: Copy/Fill operations
    // =====================================================================
    TVPCopyOpaqueImage = TVPCopyOpaqueImage_hwy;
    TVPFillARGB        = TVPFillARGB_hwy;
    TVPFillARGB_NC     = TVPFillARGB_hwy;

    // =====================================================================
    // Phase 2: Core alpha blend (4 of 8 variants)
    // Note: _d, _a, _do, _ao variants require dest-alpha table access,
    //       will be implemented in a later phase.
    // =====================================================================
    TVPAlphaBlend       = TVPAlphaBlend_hwy;
    TVPAlphaBlend_HDA   = TVPAlphaBlend_HDA_hwy;
    TVPAlphaBlend_o     = TVPAlphaBlend_o_hwy;
    TVPAlphaBlend_HDA_o = TVPAlphaBlend_HDA_o_hwy;

    // =====================================================================
    // Phase 2: Add blend (4 variants)
    // =====================================================================
    TVPAddBlend       = TVPAddBlend_hwy;
    TVPAddBlend_HDA   = TVPAddBlend_HDA_hwy;
    TVPAddBlend_o     = TVPAddBlend_o_hwy;
    TVPAddBlend_HDA_o = TVPAddBlend_HDA_o_hwy;

    // =====================================================================
    // Phase 2: Sub blend (4 variants)
    // =====================================================================
    TVPSubBlend       = TVPSubBlend_hwy;
    TVPSubBlend_HDA   = TVPSubBlend_HDA_hwy;
    TVPSubBlend_o     = TVPSubBlend_o_hwy;
    TVPSubBlend_HDA_o = TVPSubBlend_HDA_o_hwy;

    // =====================================================================
    // TODO: Phase 2 remaining
    //   TVPConstAlphaBlend (4 variants)
    //   TVPMulBlend (4 variants)
    //   TVPScreenBlend (4 variants)
    //   TVPAdditiveAlphaBlend (6 variants)
    //   TVPAlphaColorMat
    //
    // TODO: Phase 3 - Photoshop blend modes (16 types × 4 variants = 64)
    // TODO: Phase 4 - Stretch/LinTrans/Blur/Misc
    // =====================================================================
}
