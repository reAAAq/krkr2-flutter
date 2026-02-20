/*
 * KrKr2 Engine - Highway SIMD Const Alpha Blend & AlphaColorMat Implementation
 *
 * FIX: Use signed i16 arithmetic for (s - d) * alpha to avoid u16 underflow.
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

// Blend: result = (s * a + d * (255 - a)) >> 8
// All u16 arithmetic, no overflow (max product 255*255=65025 < 65535)
static HWY_INLINE auto BlendChannel(
    hn::ScalableTag<uint16_t> d16,
    hn::Vec<hn::ScalableTag<uint16_t>> s,
    hn::Vec<hn::ScalableTag<uint16_t>> d,
    hn::Vec<hn::ScalableTag<uint16_t>> a)
    -> hn::Vec<hn::ScalableTag<uint16_t>> {
    const auto v255 = hn::Set(d16, static_cast<uint16_t>(255));
    auto inv_a = hn::Sub(v255, a);
    return hn::ShiftRight<8>(hn::Add(hn::Mul(s, a), hn::Mul(d, inv_a)));
}

void ConstAlphaBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                         tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        const auto half = hn::Half<decltype(d8)>();
        auto r_lo = BlendChannel(d16, hn::PromoteTo(d16, hn::LowerHalf(half, vs)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, vd)), vopa16);
        auto r_hi = BlendChannel(d16, hn::PromoteUpperTo(d16, vs),
                                      hn::PromoteUpperTo(d16, vd), vopa16);

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * opa >> 8)) & 0xff00ff;
        tjs_uint32 d2 = d & 0xff00;
        dest[i] = d1 | ((d2 + (((s & 0xff00) - d2) * opa >> 8)) & 0xff00);
    }
}

void ConstAlphaBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                              tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));
    const auto alpha_mask = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));

        const auto half = hn::Half<decltype(d8)>();
        auto r_lo = BlendChannel(d16, hn::PromoteTo(d16, hn::LowerHalf(half, vs)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, vd)), vopa16);
        auto r_hi = BlendChannel(d16, hn::PromoteUpperTo(d16, vs),
                                      hn::PromoteUpperTo(d16, vd), vopa16);

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vd));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 d1 = d & 0xff00ff;
        d1 = (d1 + (((s & 0xff00ff) - d1) * opa >> 8)) & 0xff00ff;
        d1 += (d & 0xff000000);
        tjs_uint32 d2 = d & 0xff00;
        dest[i] = d1 | ((d2 + (((s & 0xff00) - d2) * opa >> 8)) & 0xff00);
    }
}

// ConstAlphaBlend_d: uses lookup tables, scalar only
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

// ConstAlphaBlend_a: scalar only (complex premul alpha)
void ConstAlphaBlend_a_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                           tjs_int len, tjs_int opa) {
    tjs_uint32 opa_alpha = static_cast<tjs_uint32>(opa) << 24;
    for (; len > 0; len--, dest++, src++) {
        tjs_uint32 s = *src, d = *dest;
        s = (s & 0xffffff) | opa_alpha;
        tjs_uint32 sa = s >> 24;
        tjs_uint32 premul_s = ((((s & 0x00ff00) * sa) & 0x00ff0000) +
                               (((s & 0xff00ff) * sa) & 0xff00ff00)) >> 8;
        premul_s |= (s & 0xff000000);
        tjs_uint32 da = d >> 24;
        tjs_uint32 new_da = da + sa - (da * sa >> 8);
        new_da -= (new_da >> 8);
        tjs_uint32 inv_sa = sa ^ 0xff;
        tjs_uint32 d_scaled = (((d & 0xff00ff) * inv_sa >> 8) & 0xff00ff) +
                              (((d & 0xff00) * inv_sa >> 8) & 0xff00);
        tjs_uint32 a = d_scaled, b = premul_s & 0xffffff;
        tjs_uint32 tmp = ((a & b) + (((a ^ b) >> 1) & 0x7f7f7f7f)) & 0x80808080;
        tmp = (tmp << 1) - (tmp >> 7);
        *dest = ((a + b - tmp) | tmp) & 0xffffff | (new_da << 24);
    }
}

// AlphaColorMat: blend pixels onto solid color background
// result = color + (dest - color) * dest_alpha >> 8, alpha = 0xFF
void AlphaColorMat_HWY(tjs_uint32 *dest, const tjs_uint32 color, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    tjs_uint32 color_px = color & 0xffffff;
    tjs_uint32 color_arr[4] = {color_px, color_px, color_px, color_px};
    const auto vc = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(color_arr));

    const auto alpha_shuffle = hn::Dup128VecFromValues(d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15);
    const auto opaque = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va = hn::TableLookupBytes(vs, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        // result = color + (dest - color) * alpha >> 8
        auto r_lo = BlendChannel(d16, hn::PromoteTo(d16, hn::LowerHalf(half, vs)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, vc)),
                                      hn::PromoteTo(d16, hn::LowerHalf(half, va)));
        auto r_hi = BlendChannel(d16, hn::PromoteUpperTo(d16, vs),
                                      hn::PromoteUpperTo(d16, vc),
                                      hn::PromoteUpperTo(d16, va));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        result = hn::Or(result, opaque);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
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
