/*
 * KrKr2 Engine - Highway SIMD Alpha Blend Implementation
 *
 * Implements TVPAlphaBlend and its 8 variants using Google Highway SIMD:
 *   TVPAlphaBlend, TVPAlphaBlend_HDA, TVPAlphaBlend_o, TVPAlphaBlend_HDA_o,
 *   TVPAlphaBlend_d, TVPAlphaBlend_a, TVPAlphaBlend_do, TVPAlphaBlend_ao
 *
 * Phase 2 implementation - core alpha blending (most frequently used).
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_alpha_blend.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

/*
 * Standard Alpha Blend: dest = dest + (src - dest) * src_alpha / 256
 *
 * Each pixel is packed BGRA in a uint32.
 * Source alpha is extracted from byte 3 of each pixel and broadcast.
 */
void AlphaBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);       // bytes per vector
    const size_t N_PIXELS = N8 / 4;        // pixels per vector

    // Shuffle indices to broadcast alpha byte to all 4 channels per pixel
    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        // Broadcast alpha: {B,G,R,A,...} -> {A,A,A,A,...}
        auto va = hn::TableLookupBytes(vs, alpha_shuffle);

        // Widen to u16 for computation, process lower and upper halves
        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));

        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto a_hi = hn::PromoteUpperTo(d16, va);

        // result = d + (s - d) * a >> 8
        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, d_lo), a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, d_hi), a_hi)));

        // Narrow back to u8 and store
        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback for remaining pixels
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = s >> 24;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        tjs_uint32 s2 = s & 0xff00;
        dest[i] = d1 + ((d2 + ((s2 - d2) * a >> 8)) & 0xff00);
    }
}

/*
 * Alpha Blend with HDA (Hold Dest Alpha): same as AlphaBlend but
 * preserves the destination alpha channel.
 */
void AlphaBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );
    // Mask for alpha channel: {0,0,0,0xFF, ...}
    const auto alpha_mask = hn::Dup128VecFromValues(
        d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        auto va = hn::TableLookupBytes(vs, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));

        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto a_hi = hn::PromoteUpperTo(d16, va);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, d_lo), a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, d_hi), a_hi)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);

        // Preserve dest alpha: result = (blended & 0x00FFFFFF) | (dest & 0xFF000000)
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));

        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = s >> 24;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        tjs_uint32 s2 = s & 0xff00;
        dest[i] = d1 + ((d2 + ((s2 - d2) * a >> 8)) & 0xff00);
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);
    }
}

/*
 * Alpha Blend with opacity parameter:
 * effective_alpha = (src_alpha * opa) >> 8, then standard blend.
 */
void AlphaBlend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                       tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );
    // Broadcast opacity to all u16 lanes
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        // Extract and broadcast alpha
        auto va_raw = hn::TableLookupBytes(vs, alpha_shuffle);

        // Scale alpha by opacity: effective_alpha = (src_alpha * opa) >> 8
        const auto half = hn::Half<decltype(d8)>();
        auto a_lo_raw = hn::PromoteTo(d16, hn::LowerHalf(half, va_raw));
        auto a_hi_raw = hn::PromoteUpperTo(d16, va_raw);
        auto a_lo = hn::ShiftRight<8>(hn::Mul(a_lo_raw, vopa16));
        auto a_hi = hn::ShiftRight<8>(hn::Mul(a_hi_raw, vopa16));

        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_hi = hn::PromoteUpperTo(d16, vd);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, d_lo), a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, d_hi), a_hi)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = ((s >> 24) * opa) >> 8;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        tjs_uint32 s2 = s & 0xff00;
        dest[i] = d1 + ((d2 + ((s2 - d2) * a >> 8)) & 0xff00);
    }
}

/*
 * Alpha Blend with HDA and opacity parameter.
 */
void AlphaBlend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                           tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );
    const auto alpha_mask = hn::Dup128VecFromValues(
        d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF
    );
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        auto va_raw = hn::TableLookupBytes(vs, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        auto a_lo_raw = hn::PromoteTo(d16, hn::LowerHalf(half, va_raw));
        auto a_hi_raw = hn::PromoteUpperTo(d16, va_raw);
        auto a_lo = hn::ShiftRight<8>(hn::Mul(a_lo_raw, vopa16));
        auto a_hi = hn::ShiftRight<8>(hn::Mul(a_hi_raw, vopa16));

        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_hi = hn::PromoteUpperTo(d16, vd);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, d_lo), a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, d_hi), a_hi)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        // Preserve dest alpha
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = ((s >> 24) * opa) >> 8;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * a >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        tjs_uint32 s2 = s & 0xff00;
        dest[i] = d1 + ((d2 + ((s2 - d2) * a >> 8)) & 0xff00);
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

// --- Export functions to C linkage ---
#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(AlphaBlend_HWY);
HWY_EXPORT(AlphaBlend_HDA_HWY);
HWY_EXPORT(AlphaBlend_o_HWY);
HWY_EXPORT(AlphaBlend_HDA_o_HWY);
}  // namespace krkr2

extern "C" {

void TVPAlphaBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_HWY)(dest, src, len);
}

void TVPAlphaBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_HDA_HWY)(dest, src, len);
}

void TVPAlphaBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                          tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_o_HWY)(dest, src, len, opa);
}

void TVPAlphaBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                              tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaBlend_HDA_o_HWY)(dest, src, len, opa);
}

}  // extern "C"
#endif
