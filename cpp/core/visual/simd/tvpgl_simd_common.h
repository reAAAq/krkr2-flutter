/*
 * KrKr2 Engine - Highway SIMD Common Utilities
 *
 * Provides vectorized helper functions for pixel blending operations
 * using Google Highway's portable SIMD abstraction.
 */

#ifndef __TVPGL_SIMD_COMMON_H__
#define __TVPGL_SIMD_COMMON_H__

#include "tjsTypes.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_common.h"

#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// Tag types used throughout
using D8 = hn::ScalableTag<uint8_t>;
using D16 = hn::ScalableTag<uint16_t>;
using D32 = hn::ScalableTag<uint32_t>;

/// Get the number of pixels per SIMD vector (4 bytes per pixel)
HWY_INLINE size_t PixelsPerVector() {
    D8 d8;
    return hn::Lanes(d8) / 4;
}

/// Broadcast alpha channel from BGRA pixels to all channels of each pixel.
/// Input:  {B0,G0,R0,A0, B1,G1,R1,A1, B2,G2,R2,A2, B3,G3,R3,A3}
/// Output: {A0,A0,A0,A0, A1,A1,A1,A1, A2,A2,A2,A2, A3,A3,A3,A3}
/// Works for 128-bit SIMD (4 pixels).
HWY_INLINE hn::Vec<D8> BroadcastAlpha(D8 d8, hn::Vec<D8> pixels) {
    // Use Dup128VecFromValues to create shuffle indices that replicate
    // byte [3] to [0..3], byte [7] to [4..7], etc.
    const auto indices = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,     // pixel 0: replicate A0
        7, 7, 7, 7,     // pixel 1: replicate A1
        11, 11, 11, 11,  // pixel 2: replicate A2
        15, 15, 15, 15   // pixel 3: replicate A3
    );
    return hn::TableLookupBytes(pixels, indices);
}

/// Blend operation in u16: result = d + (s - d) * a >> 8
/// All vectors are u16 (widened from u8).
/// Returns the blended result in u16.
HWY_INLINE hn::Vec<D16> BlendChannelU16(
    D16 d16, hn::Vec<D16> vd, hn::Vec<D16> vs, hn::Vec<D16> va) {
    // diff = s - d  (may be negative, but u16 wrapping is fine with the mask)
    auto diff = hn::Sub(vs, vd);
    // blended = d + (diff * a) >> 8
    auto product = hn::Mul(diff, va);
    auto shifted = hn::ShiftRight<8>(product);
    return hn::Add(vd, shifted);
}

/// Full pixel blend: dest = dest + (src - dest) * alpha >> 8
/// Operates on packed u8 BGRA pixels.
/// alpha_vec must already be broadcast (all 4 channels of each pixel = alpha).
HWY_INLINE hn::Vec<D8> BlendPixels(
    D8 d8, hn::Vec<D8> dest, hn::Vec<D8> src, hn::Vec<D8> alpha) {

    const hn::Repartition<uint16_t, D8> d16;
    const auto half = hn::Half<D8>();

    // Process lower half
    auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, src));
    auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, dest));
    auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, alpha));
    auto r_lo = BlendChannelU16(d16, d_lo, s_lo, a_lo);

    // Process upper half
    auto s_hi = hn::PromoteUpperTo(d16, src);
    auto d_hi = hn::PromoteUpperTo(d16, dest);
    auto a_hi = hn::PromoteUpperTo(d16, alpha);
    auto r_hi = BlendChannelU16(d16, d_hi, s_hi, a_hi);

    // Narrow back to u8
    return hn::OrderedDemote2To(d8, r_lo, r_hi);
}

/// Mask for alpha channel bytes in BGRA packed format
/// Returns {0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF}
HWY_INLINE hn::Vec<D8> AlphaChannelMask(D8 d8) {
    return hn::Dup128VecFromValues(
        d8,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,
        0, 0, 0, 0xFF
    );
}

/// Apply HDA (Hold Dest Alpha): preserve destination alpha channel
/// result = (blended & 0x00FFFFFF) | (dest & 0xFF000000)
HWY_INLINE hn::Vec<D8> ApplyHDA(D8 d8, hn::Vec<D8> blended, hn::Vec<D8> dest) {
    auto mask = AlphaChannelMask(d8);
    return hn::Or(hn::AndNot(mask, blended), hn::And(mask, dest));
}

/// Scale alpha by opacity: effective_alpha = (src_alpha * opa) >> 8
/// src must be BGRA pixels, opa_vec must be broadcast opacity in u8
HWY_INLINE hn::Vec<D8> ScaleAlphaByOpacity(
    D8 d8, hn::Vec<D8> alpha_broadcast, hn::Vec<D8> opa_vec) {

    const hn::Repartition<uint16_t, D8> d16;
    const auto half = hn::Half<D8>();

    auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, alpha_broadcast));
    auto o_lo = hn::PromoteTo(d16, hn::LowerHalf(half, opa_vec));
    auto r_lo = hn::ShiftRight<8>(hn::Mul(a_lo, o_lo));

    auto a_hi = hn::PromoteUpperTo(d16, alpha_broadcast);
    auto o_hi = hn::PromoteUpperTo(d16, opa_vec);
    auto r_hi = hn::ShiftRight<8>(hn::Mul(a_hi, o_hi));

    return hn::OrderedDemote2To(d8, r_lo, r_hi);
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

#endif  // __TVPGL_SIMD_COMMON_H__
