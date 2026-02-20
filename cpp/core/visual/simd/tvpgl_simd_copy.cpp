/*
 * KrKr2 Engine - Highway SIMD Copy/Fill Operations
 *
 * Implements simple pixel copy and fill operations using Highway SIMD:
 *   TVPCopyOpaqueImage - Copy with alpha set to 0xFF (Phase 1 POC)
 *   TVPFillARGB        - Fill with constant ARGB value
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_copy.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

/*
 * Copy opaque image: dest[i] = src[i] | 0xFF000000
 * Sets the alpha channel to fully opaque while copying RGB.
 */
void CopyOpaqueImage_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint32_t> d32;
    const size_t N = hn::Lanes(d32);
    const auto opaque_mask = hn::Set(d32, 0xFF000000u);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N) <= len; i += N) {
        auto v = hn::LoadU(d32, reinterpret_cast<const uint32_t*>(src + i));
        v = hn::Or(v, opaque_mask);
        hn::StoreU(v, d32, reinterpret_cast<uint32_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        dest[i] = src[i] | 0xFF000000;
    }
}

/*
 * Fill ARGB: dest[i] = value for all i in [0, len)
 */
void FillARGB_HWY(tjs_uint32 *dest, tjs_int len, tjs_uint32 value) {
    const hn::ScalableTag<uint32_t> d32;
    const size_t N = hn::Lanes(d32);
    const auto vvalue = hn::Set(d32, value);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N) <= len; i += N) {
        hn::StoreU(vvalue, d32, reinterpret_cast<uint32_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        dest[i] = value;
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

// --- Export functions to C linkage ---
#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(CopyOpaqueImage_HWY);
HWY_EXPORT(FillARGB_HWY);
}  // namespace krkr2

extern "C" {

void TVPCopyOpaqueImage_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(CopyOpaqueImage_HWY)(dest, src, len);
}

void TVPFillARGB_hwy(tjs_uint32 *dest, tjs_int len, tjs_uint32 value) {
    krkr2::HWY_DYNAMIC_DISPATCH(FillARGB_HWY)(dest, len, value);
}

}  // extern "C"
#endif
