/*
 * KrKr2 Engine - Highway SIMD Const Alpha Blend & AlphaColorMat Implementation
 *
 * Implements:
 *   TVPConstAlphaBlend     - blend with constant opacity
 *   TVPConstAlphaBlend_HDA - blend with constant opacity, hold dest alpha
 *   TVPConstAlphaBlend_d   - blend with constant opacity, dest-alpha aware
 *   TVPConstAlphaBlend_a   - blend with constant opacity, additive-alpha aware
 *   TVPAlphaColorMat       - alpha color matte
 */

#include "tjsTypes.h"
#include "tvpgl.h"

extern "C" {
extern unsigned char TVPOpacityOnOpacityTable[256 * 256];
extern unsigned char TVPNegativeMulTable[256 * 256];
}

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_const_blend.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// ConstAlphaBlend: dest = dest + (src - dest) * opa / 256
// opa is a fixed constant for all pixels (no per-pixel alpha)
// =========================================================================

void ConstAlphaBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                         tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_hi = hn::PromoteUpperTo(d16, vd);

        // result = d + (s - d) * opa >> 8
        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, d_lo), vopa16)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, d_hi), vopa16)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * opa >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        tjs_uint32 s2 = s & 0xff00;
        dest[i] = d1 | ((d2 + ((s2 - d2) * opa >> 8)) & 0xff00);
    }
}

void ConstAlphaBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                              tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));
    const auto alpha_mask = hn::Dup128VecFromValues(
        d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_hi = hn::PromoteUpperTo(d16, vd);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, d_lo), vopa16)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, d_hi), vopa16)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        // Preserve dest alpha
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * opa >> 8)) & 0xff00ff;
        d1 += (d & 0xff000000); // preserve dest alpha
        tjs_uint32 d2 = d & 0xff00;
        tjs_uint32 s2 = s & 0xff00;
        dest[i] = d1 | ((d2 + ((s2 - d2) * opa >> 8)) & 0xff00);
    }
}

/*
 * ConstAlphaBlend_d: dest-alpha aware constant alpha blend.
 * Uses TVPOpacityOnOpacityTable and TVPNegativeMulTable lookup tables.
 * These are not easily vectorized so we use scalar implementation.
 */
void ConstAlphaBlend_d_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                           tjs_int len, tjs_int opa) {
    tjs_int opa_shifted = opa << 8;
    for (tjs_int i = 0; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 addr = opa_shifted + (d >> 24);
        tjs_uint32 alpha = TVPOpacityOnOpacityTable[addr];
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = ((d1 + (((s & 0xff00ff) - d1) * alpha >> 8)) & 0xff00ff) +
             (TVPNegativeMulTable[addr] << 24);
        d &= 0xff00;
        s &= 0xff00;
        dest[i] = d1 | ((d + ((s - d) * alpha >> 8)) & 0xff00);
    }
}

/*
 * ConstAlphaBlend_a: additive-alpha (pre-multiplied) constant alpha blend.
 * Converts src to pre-multiplied alpha with constant opa, then blends.
 */
void ConstAlphaBlend_a_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                           tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    // Pre-compute opa << 24 for alpha channel
    tjs_uint32 opa_alpha = static_cast<tjs_uint32>(opa) << 24;

    tjs_int i = 0;
    // Scalar fallback for _a variant (involves pre-multiplied alpha computation)
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        // Set source alpha to opa, then do premul alpha blend
        s = (s & 0xffffff) | opa_alpha;
        // Convert to pre-multiplied alpha
        tjs_uint32 sa = s >> 24;
        tjs_uint32 premul_s = ((((s & 0x00ff00) * sa) & 0x00ff0000) +
                               (((s & 0xff00ff) * sa) & 0xff00ff00)) >> 8;
        premul_s |= (s & 0xff000000);
        // Additive alpha blend: Da = Sa + Da - Sa*Da
        tjs_uint32 da = d >> 24;
        tjs_uint32 new_da = da + sa - (da * sa >> 8);
        new_da -= (new_da >> 8);
        // Color: sat(Si, (1-Sa)*Di)
        tjs_uint32 inv_sa = sa ^ 0xff;
        tjs_uint32 s_color = premul_s & 0xffffff;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * inv_sa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * inv_sa >> 8) & 0xff00);
        // Saturated add
        tjs_uint32 a = d_scaled, b = s_color;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = ((a + b - tmp) | tmp) & 0xffffff | (new_da << 24);
    }
}

// =========================================================================
// AlphaColorMat: blend pixels onto a solid color background
// dest = color + (dest - color) * dest_alpha / 256, result alpha = 0xFF
// =========================================================================

void AlphaColorMat_HWY(tjs_uint32 *dest, const tjs_uint32 color, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    // Broadcast color to all pixel slots, with alpha = 0
    tjs_uint32 color_pixels[4] = {color & 0xffffff, color & 0xffffff,
                                   color & 0xffffff, color & 0xffffff};
    // For wider vectors, replicate more
    const auto vc = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(color_pixels));

    // Alpha broadcast shuffle: extract alpha from each pixel
    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );
    // Opaque alpha mask
    const auto opaque = hn::Dup128VecFromValues(
        d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        // Extract dest alpha and broadcast
        auto va = hn::TableLookupBytes(vs, alpha_shuffle);

        // Widen to u16
        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto c_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vc));
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));

        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto c_hi = hn::PromoteUpperTo(d16, vc);
        auto a_hi = hn::PromoteUpperTo(d16, va);

        // result = color + (src - color) * src_alpha >> 8
        auto r_lo = hn::Add(c_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, c_lo), a_lo)));
        auto r_hi = hn::Add(c_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, c_hi), a_hi)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        // Set alpha to 0xFF
        result = hn::Or(result, opaque);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = dest[i];
        tjs_uint32 d = color;
        tjs_uint32 sopa = s >> 24;
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * sopa >> 8)) & 0xff00ff;
        d &= 0xff00;
        s &= 0xff00;
        dest[i] = d1 + ((d + ((s - d) * sopa >> 8)) & 0xff00) + 0xff000000;
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

// --- Export functions ---
#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(ConstAlphaBlend_HWY);
HWY_EXPORT(ConstAlphaBlend_HDA_HWY);
HWY_EXPORT(ConstAlphaBlend_d_HWY);
HWY_EXPORT(ConstAlphaBlend_a_HWY);
HWY_EXPORT(AlphaColorMat_HWY);
}  // namespace krkr2

extern "C" {

void TVPConstAlphaBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                            tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(ConstAlphaBlend_HWY)(dest, src, len, opa);
}

void TVPConstAlphaBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                                tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(ConstAlphaBlend_HDA_HWY)(dest, src, len, opa);
}

void TVPConstAlphaBlend_d_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                              tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(ConstAlphaBlend_d_HWY)(dest, src, len, opa);
}

void TVPConstAlphaBlend_a_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                              tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(ConstAlphaBlend_a_HWY)(dest, src, len, opa);
}

void TVPAlphaColorMat_hwy(tjs_uint32 *dest, const tjs_uint32 color,
                          tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AlphaColorMat_HWY)(dest, color, len);
}

}  // extern "C"
#endif
