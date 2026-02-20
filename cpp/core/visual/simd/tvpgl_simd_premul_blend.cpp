/*
 * KrKr2 Engine - Highway SIMD Pre-multiplied Alpha Blend Implementation
 *
 * Implements AdditiveAlphaBlend (pre-multiplied alpha):
 *   TVPAdditiveAlphaBlend       - base: sat(src, (1-src_alpha)*dest)
 *   TVPAdditiveAlphaBlend_HDA   - hold dest alpha
 *   TVPAdditiveAlphaBlend_o     - with opacity
 *   TVPAdditiveAlphaBlend_HDA_o - hold dest alpha + opacity
 *   TVPAdditiveAlphaBlend_a     - additive alpha output (Da = Sa+Da-Sa*Da)
 *   TVPAdditiveAlphaBlend_ao    - additive alpha output + opacity
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_premul_blend.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// AdditiveAlphaBlend (pre-multiplied alpha):
//   result = saturated_add(src, dest * (1 - src_alpha))
//   Source is already pre-multiplied, so we scale dest by inverse alpha
//   and saturated-add with src.
// =========================================================================

void AdditiveAlphaBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    // Shuffle to broadcast alpha and then invert it
    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        // Get inverse alpha: inv_alpha = ~src_alpha (= 255 - src_alpha)
        auto va = hn::TableLookupBytes(vs, alpha_shuffle);
        auto inv_a = hn::Not(va);

        // Scale dest by inverse alpha: d_scaled = (dest * inv_alpha) >> 8
        const auto half = hn::Half<decltype(d8)>();
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto ia_lo = hn::PromoteTo(d16, hn::LowerHalf(half, inv_a));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto ia_hi = hn::PromoteUpperTo(d16, inv_a);

        auto ds_lo = hn::ShiftRight<8>(hn::Mul(d_lo, ia_lo));
        auto ds_hi = hn::ShiftRight<8>(hn::Mul(d_hi, ia_hi));
        auto d_scaled = hn::OrderedDemote2To(d8, ds_lo, ds_hi);

        // Saturated add: result = sat_add(d_scaled, src)
        auto result = hn::SaturatedAdd(d_scaled, vs);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 sopa = (~s) >> 24;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * sopa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * sopa >> 8) & 0xff00);
        tjs_uint32 a = d_scaled, b = s;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (a + b - tmp) | tmp;
    }
}

void AdditiveAlphaBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
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

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        auto va = hn::TableLookupBytes(vs, alpha_shuffle);
        auto inv_a = hn::Not(va);

        const auto half = hn::Half<decltype(d8)>();
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto ia_lo = hn::PromoteTo(d16, hn::LowerHalf(half, inv_a));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto ia_hi = hn::PromoteUpperTo(d16, inv_a);

        auto ds_lo = hn::ShiftRight<8>(hn::Mul(d_lo, ia_lo));
        auto ds_hi = hn::ShiftRight<8>(hn::Mul(d_hi, ia_hi));
        auto d_scaled = hn::OrderedDemote2To(d8, ds_lo, ds_hi);

        auto result = hn::SaturatedAdd(d_scaled, vs);
        // Preserve dest alpha
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 dopa = d & 0xff000000;
        tjs_uint32 sopa = (~s) >> 24;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * sopa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * sopa >> 8) & 0xff00);
        tjs_uint32 a = d_scaled, b = s;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (((a + b - tmp) | tmp) & 0x00FFFFFF) + dopa;
    }
}

void AdditiveAlphaBlend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                               tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        // Scale src by opacity: s_scaled = (src * opa) >> 8
        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::ShiftRight<8>(hn::Mul(hn::PromoteTo(d16, hn::LowerHalf(half, vs)), vopa16));
        auto s_hi = hn::ShiftRight<8>(hn::Mul(hn::PromoteUpperTo(d16, vs), vopa16));
        auto vs_scaled = hn::OrderedDemote2To(d8, s_lo, s_hi);

        // Get inverse alpha from scaled src
        auto va = hn::TableLookupBytes(vs_scaled, alpha_shuffle);
        auto inv_a = hn::Not(va);

        // Scale dest by inverse alpha
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto ia_lo = hn::PromoteTo(d16, hn::LowerHalf(half, inv_a));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto ia_hi = hn::PromoteUpperTo(d16, inv_a);

        auto ds_lo = hn::ShiftRight<8>(hn::Mul(d_lo, ia_lo));
        auto ds_hi = hn::ShiftRight<8>(hn::Mul(d_hi, ia_hi));
        auto d_scaled = hn::OrderedDemote2To(d8, ds_lo, ds_hi);

        auto result = hn::SaturatedAdd(d_scaled, vs_scaled);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i];
        s = (((s & 0xff00ff) * opa >> 8) & 0xff00ff) +
            (((s >> 8) & 0xff00ff) * opa & 0xff00ff00);
        tjs_uint32 d = dest[i];
        tjs_uint32 sopa = (~s) >> 24;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * sopa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * sopa >> 8) & 0xff00);
        tjs_uint32 a = d_scaled, b = s;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (a + b - tmp) | tmp;
    }
}

void AdditiveAlphaBlend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                                   tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

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

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::ShiftRight<8>(hn::Mul(hn::PromoteTo(d16, hn::LowerHalf(half, vs)), vopa16));
        auto s_hi = hn::ShiftRight<8>(hn::Mul(hn::PromoteUpperTo(d16, vs), vopa16));
        auto vs_scaled = hn::OrderedDemote2To(d8, s_lo, s_hi);

        auto va = hn::TableLookupBytes(vs_scaled, alpha_shuffle);
        auto inv_a = hn::Not(va);

        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto ia_lo = hn::PromoteTo(d16, hn::LowerHalf(half, inv_a));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto ia_hi = hn::PromoteUpperTo(d16, inv_a);

        auto ds_lo = hn::ShiftRight<8>(hn::Mul(d_lo, ia_lo));
        auto ds_hi = hn::ShiftRight<8>(hn::Mul(d_hi, ia_hi));
        auto d_scaled = hn::OrderedDemote2To(d8, ds_lo, ds_hi);

        auto result = hn::SaturatedAdd(d_scaled, vs_scaled);
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    // Scalar fallback
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 dopa = d & 0xff000000;
        s = (((s & 0xff00ff) * opa >> 8) & 0xff00ff) +
            (((s >> 8) & 0xff00ff) * opa & 0xff00ff00);
        tjs_uint32 sopa = (~s) >> 24;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * sopa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * sopa >> 8) & 0xff00);
        tjs_uint32 a = d_scaled, b = s;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (((a + b - tmp) | tmp) & 0x00FFFFFF) + dopa;
    }
}

/*
 * AdditiveAlphaBlend_a: Both src and dest are pre-multiplied alpha.
 * Da = Sa + Da - Sa*Da, Di = sat(Si, (1-Sa)*Di)
 */
void AdditiveAlphaBlend_a_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    // This variant computes output alpha as Da = Sa + Da - Sa*Da
    // Complex alpha computation, use scalar for correctness
    for (tjs_int i = 0; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 da = d >> 24;
        tjs_uint32 sa = s >> 24;
        da = da + sa - (da * sa >> 8);
        da -= (da >> 8); // adjust alpha
        sa ^= 0xff;
        s &= 0xffffff;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * sa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * sa >> 8) & 0xff00);
        tjs_uint32 a = d_scaled, b = s;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (da << 24) + (((a + b - tmp) | tmp) & 0xffffff);
    }
}

/*
 * AdditiveAlphaBlend_ao: Both src and dest pre-multiplied alpha, with opacity.
 * First scale src by opacity, then do additive alpha blend.
 */
void AdditiveAlphaBlend_ao_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                                tjs_int len, tjs_int opa) {
    for (tjs_int i = 0; i < len; i++) {
        tjs_uint32 s = src[i];
        // Scale src by opacity
        s = (((s & 0xff00ff) * opa >> 8) & 0xff00ff) +
            (((s >> 8) & 0xff00ff) * opa & 0xff00ff00);
        tjs_uint32 d = dest[i];
        tjs_uint32 da = d >> 24;
        tjs_uint32 sa = s >> 24;
        da = da + sa - (da * sa >> 8);
        da -= (da >> 8);
        sa ^= 0xff;
        s &= 0xffffff;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * sa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * sa >> 8) & 0xff00);
        tjs_uint32 a = d_scaled, b = s;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (da << 24) + (((a + b - tmp) | tmp) & 0xffffff);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

// --- Export functions ---
#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(AdditiveAlphaBlend_HWY);
HWY_EXPORT(AdditiveAlphaBlend_HDA_HWY);
HWY_EXPORT(AdditiveAlphaBlend_o_HWY);
HWY_EXPORT(AdditiveAlphaBlend_HDA_o_HWY);
HWY_EXPORT(AdditiveAlphaBlend_a_HWY);
HWY_EXPORT(AdditiveAlphaBlend_ao_HWY);
}  // namespace krkr2

extern "C" {

void TVPAdditiveAlphaBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AdditiveAlphaBlend_HWY)(dest, src, len);
}

void TVPAdditiveAlphaBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AdditiveAlphaBlend_HDA_HWY)(dest, src, len);
}

void TVPAdditiveAlphaBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                                  tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AdditiveAlphaBlend_o_HWY)(dest, src, len, opa);
}

void TVPAdditiveAlphaBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                                      tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AdditiveAlphaBlend_HDA_o_HWY)(dest, src, len, opa);
}

void TVPAdditiveAlphaBlend_a_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AdditiveAlphaBlend_a_HWY)(dest, src, len);
}

void TVPAdditiveAlphaBlend_ao_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                                   tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AdditiveAlphaBlend_ao_HWY)(dest, src, len, opa);
}

}  // extern "C"
#endif
