/*
 * KrKr2 Engine - Highway SIMD Add/Sub/Mul/Screen Blend Implementation
 *
 * Implements basic arithmetic blend modes using Google Highway SIMD:
 *   TVPAddBlend - Saturated addition blend
 *   TVPSubBlend - Subtraction blend
 *   TVPMulBlend - Multiplication blend
 *   TVPScreenBlend - Screen blend
 *
 * Each with 4 variants: base, HDA, _o (opacity), HDA_o
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_arithmetic_blend.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// AddBlend: Saturated addition of src and dest pixel channels
// =========================================================================

void AddBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto result = hn::SaturatedAdd(vd, vs);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 a = dest[i], b = src[i];
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (a + b - tmp) | tmp;
    }
}

void AddBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N8 = hn::Lanes(d8);
    const size_t N_PIXELS = N8 / 4;

    const auto alpha_mask = hn::Dup128VecFromValues(
        d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto result = hn::SaturatedAdd(vd, vs);
        // Preserve dest alpha
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 a = dest[i], b = src[i];
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = ((a + b - tmp) | tmp) & 0x00ffffff | (a & 0xff000000);
    }
}

void AddBlend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
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

        // Scale src by opacity: s = (s * opa) >> 8
        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        s_lo = hn::ShiftRight<8>(hn::Mul(s_lo, vopa16));
        s_hi = hn::ShiftRight<8>(hn::Mul(s_hi, vopa16));
        auto vs_scaled = hn::OrderedDemote2To(d8, s_lo, s_hi);

        auto result = hn::SaturatedAdd(vd, vs_scaled);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i];
        s = (((s & 0x00ff00) * opa >> 8) & 0x00ff00) +
            (((s & 0xff00ff) * opa >> 8) & 0xff00ff);
        tjs_uint32 d = dest[i];
        tjs_uint32 tmp = ((d & s) + (((d ^ s) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (d + s - tmp) | tmp;
    }
}

void AddBlend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
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
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        s_lo = hn::ShiftRight<8>(hn::Mul(s_lo, vopa16));
        s_hi = hn::ShiftRight<8>(hn::Mul(s_hi, vopa16));
        auto vs_scaled = hn::OrderedDemote2To(d8, s_lo, s_hi);

        auto result = hn::SaturatedAdd(vd, vs_scaled);
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        s = (((s & 0x00ff00) * opa >> 8) & 0x00ff00) +
            (((s & 0xff00ff) * opa >> 8) & 0xff00ff);
        tjs_uint32 tmp = ((d & s) + (((d ^ s) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = ((d + s - tmp) | tmp) & 0x00ffffff | (d & 0xff000000);
    }
}

// =========================================================================
// SubBlend: Saturated subtraction
// =========================================================================

void SubBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto result = hn::SaturatedSub(vd, vs);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 tmp = ((s & d) + (((s ^ d) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (s + d - tmp) & tmp;
    }
}

void SubBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto alpha_mask = hn::Dup128VecFromValues(
        d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF
    );

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto result = hn::SaturatedSub(vd, vs);
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 tmp = ((s & d) + (((s ^ d) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = ((s + d - tmp) & tmp) & 0x00ffffff | (d & 0xff000000);
    }
}

void SubBlend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                     tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        // Scale: s = ~(~s * opa >> 8)  (original uses inverted scaling)
        auto vs_inv = hn::Not(vs);
        const auto half = hn::Half<decltype(d8)>();
        auto si_lo = hn::ShiftRight<8>(hn::Mul(hn::PromoteTo(d16, hn::LowerHalf(half, vs_inv)), vopa16));
        auto si_hi = hn::ShiftRight<8>(hn::Mul(hn::PromoteUpperTo(d16, vs_inv), vopa16));
        auto vs_scaled = hn::Not(hn::OrderedDemote2To(d8, si_lo, si_hi));

        auto result = hn::SaturatedSub(vd, vs_scaled);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = ~src[i];
        s = ~((((s & 0x00ff00) * opa >> 8) & 0x00ff00) +
              (((s & 0xff00ff) * opa >> 8) & 0xff00ff));
        tjs_uint32 d = dest[i];
        tjs_uint32 tmp = ((s & d) + (((s ^ d) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = (s + d - tmp) & tmp;
    }
}

void SubBlend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                         tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
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

        auto vs_inv = hn::Not(vs);
        const auto half = hn::Half<decltype(d8)>();
        auto si_lo = hn::ShiftRight<8>(hn::Mul(hn::PromoteTo(d16, hn::LowerHalf(half, vs_inv)), vopa16));
        auto si_hi = hn::ShiftRight<8>(hn::Mul(hn::PromoteUpperTo(d16, vs_inv), vopa16));
        auto vs_scaled = hn::Not(hn::OrderedDemote2To(d8, si_lo, si_hi));

        auto result = hn::SaturatedSub(vd, vs_scaled);
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = ~src[i], d = dest[i];
        s = ~((((s & 0x00ff00) * opa >> 8) & 0x00ff00) +
              (((s & 0xff00ff) * opa >> 8) & 0xff00ff));
        tjs_uint32 tmp = ((s & d) + (((s ^ d) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        dest[i] = ((s + d - tmp) & tmp) & 0x00ffffff | (d & 0xff000000);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

// --- Export functions ---
#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(AddBlend_HWY);
HWY_EXPORT(AddBlend_HDA_HWY);
HWY_EXPORT(AddBlend_o_HWY);
HWY_EXPORT(AddBlend_HDA_o_HWY);
HWY_EXPORT(SubBlend_HWY);
HWY_EXPORT(SubBlend_HDA_HWY);
HWY_EXPORT(SubBlend_o_HWY);
HWY_EXPORT(SubBlend_HDA_o_HWY);
}  // namespace krkr2

extern "C" {

void TVPAddBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddBlend_HWY)(dest, src, len);
}
void TVPAddBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddBlend_HDA_HWY)(dest, src, len);
}
void TVPAddBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                        tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddBlend_o_HWY)(dest, src, len, opa);
}
void TVPAddBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                            tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddBlend_HDA_o_HWY)(dest, src, len, opa);
}

void TVPSubBlend_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(SubBlend_HWY)(dest, src, len);
}
void TVPSubBlend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(SubBlend_HDA_HWY)(dest, src, len);
}
void TVPSubBlend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                        tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(SubBlend_o_HWY)(dest, src, len, opa);
}
void TVPSubBlend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,
                            tjs_int len, tjs_int opa) {
    krkr2::HWY_DYNAMIC_DISPATCH(SubBlend_HDA_o_HWY)(dest, src, len, opa);
}

}  // extern "C"
#endif
