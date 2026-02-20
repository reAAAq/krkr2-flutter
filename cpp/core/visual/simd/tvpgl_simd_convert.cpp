/*
 * KrKr2 Engine - Highway SIMD Convert Functions
 *
 * Implements format conversion functions:
 *   TVPConvertAdditiveAlphaToAlpha
 *   TVPConvertAlphaToAdditiveAlpha
 *   TVPConvert24BitTo32Bit
 *   TVPConvert32BitTo24Bit
 *   TVPReverseRGB
 *
 * Phase 4 implementation.
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_convert.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// TVPConvertAdditiveAlphaToAlpha
// Pre-multiplied → straight alpha
// For each pixel: if alpha > 0, ch = min(ch * 255 / alpha, 255)
// =========================================================================
void ConvertAdditiveAlphaToAlpha_HWY(tjs_uint32 *buf, tjs_int len) {
    // This involves per-pixel division by alpha, not easily vectorizable.
    // Use scalar implementation.
    for (tjs_int i = 0; i < len; i++) {
        tjs_uint32 px = buf[i];
        tjs_uint32 a = px >> 24;
        if (a == 0) {
            buf[i] = 0;
        } else if (a < 255) {
            tjs_uint32 r = ((px >> 16) & 0xff) * 255 / a;
            tjs_uint32 g = ((px >> 8) & 0xff) * 255 / a;
            tjs_uint32 b = (px & 0xff) * 255 / a;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            buf[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        // a == 255: no change needed
    }
}

// =========================================================================
// TVPConvertAlphaToAdditiveAlpha
// Straight → pre-multiplied alpha
// For each pixel: ch = ch * alpha / 255
// =========================================================================
void ConvertAlphaToAdditiveAlpha_HWY(tjs_uint32 *buf, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto alpha_shuffle = hn::Dup128VecFromValues(d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15);
    const auto alpha_mask = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vp = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(buf + i));
        auto va = hn::TableLookupBytes(vp, alpha_shuffle);

        const auto half = hn::Half<decltype(d8)>();
        auto p_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vp));
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
        auto p_hi = hn::PromoteUpperTo(d16, vp);
        auto a_hi = hn::PromoteUpperTo(d16, va);

        auto r_lo = hn::ShiftRight<8>(hn::Mul(p_lo, a_lo));
        auto r_hi = hn::ShiftRight<8>(hn::Mul(p_hi, a_hi));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        // Preserve original alpha channel
        result = hn::Or(hn::AndNot(alpha_mask, result), hn::And(alpha_mask, vp));
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(buf + i));
    }
    for (; i < len; i++) {
        tjs_uint32 px = buf[i];
        tjs_uint32 a = px >> 24;
        buf[i] = (a << 24) |
                 ((((px >> 16) & 0xff) * a / 255) << 16) |
                 ((((px >> 8) & 0xff) * a / 255) << 8) |
                 (((px) & 0xff) * a / 255);
    }
}

// =========================================================================
// TVPConvert24BitTo32Bit
// Convert 24-bit RGB packed to 32-bit ARGB (alpha = 0xFF)
// =========================================================================
void Convert24BitTo32Bit_HWY(tjs_uint32 *dest, const tjs_uint8 *buf,
                              tjs_int len) {
    // Process 4 pixels at a time: read 12 bytes, write 16 bytes
    tjs_int i = 0;
    for (; i + 4 <= len; i += 4) {
        const tjs_uint8 *s = buf + i * 3;
        dest[i + 0] = 0xFF000000 | (s[2] << 16) | (s[1] << 8) | s[0];
        dest[i + 1] = 0xFF000000 | (s[5] << 16) | (s[4] << 8) | s[3];
        dest[i + 2] = 0xFF000000 | (s[8] << 16) | (s[7] << 8) | s[6];
        dest[i + 3] = 0xFF000000 | (s[11] << 16) | (s[10] << 8) | s[9];
    }
    for (; i < len; i++) {
        const tjs_uint8 *s = buf + i * 3;
        dest[i] = 0xFF000000 | (s[2] << 16) | (s[1] << 8) | s[0];
    }
}

// =========================================================================
// TVPConvert32BitTo24Bit
// Convert 32-bit ARGB to 24-bit RGB packed (strip alpha)
// =========================================================================
void Convert32BitTo24Bit_HWY(tjs_uint8 *dest, const tjs_uint8 *buf,
                              tjs_int len) {
    tjs_int i = 0;
    const tjs_uint32 *src32 = reinterpret_cast<const tjs_uint32*>(buf);
    for (; i + 4 <= len; i += 4) {
        tjs_uint8 *d = dest + i * 3;
        tjs_uint32 p0 = src32[i + 0];
        tjs_uint32 p1 = src32[i + 1];
        tjs_uint32 p2 = src32[i + 2];
        tjs_uint32 p3 = src32[i + 3];
        d[0]  = p0 & 0xff; d[1]  = (p0 >> 8) & 0xff; d[2]  = (p0 >> 16) & 0xff;
        d[3]  = p1 & 0xff; d[4]  = (p1 >> 8) & 0xff; d[5]  = (p1 >> 16) & 0xff;
        d[6]  = p2 & 0xff; d[7]  = (p2 >> 8) & 0xff; d[8]  = (p2 >> 16) & 0xff;
        d[9]  = p3 & 0xff; d[10] = (p3 >> 8) & 0xff; d[11] = (p3 >> 16) & 0xff;
    }
    for (; i < len; i++) {
        tjs_uint8 *d = dest + i * 3;
        tjs_uint32 p = src32[i];
        d[0] = p & 0xff; d[1] = (p >> 8) & 0xff; d[2] = (p >> 16) & 0xff;
    }
}

// =========================================================================
// TVPReverseRGB
// Swap R and B channels: BGRA → RGBA (or vice versa)
// =========================================================================
void ReverseRGB_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    // Shuffle: swap byte 0 ↔ byte 2 within each 4-byte pixel
    const auto shuffle = hn::Dup128VecFromValues(d8,
        2, 1, 0, 3,    6, 5, 4, 7,
        10, 9, 8, 11,  14, 13, 12, 15);

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto result = hn::TableLookupBytes(vs, shuffle);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }
    for (; i < len; i++) {
        tjs_uint32 p = src[i];
        dest[i] = (p & 0xFF00FF00) |
                  ((p & 0x00FF0000) >> 16) |
                  ((p & 0x000000FF) << 16);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(ConvertAdditiveAlphaToAlpha_HWY);
HWY_EXPORT(ConvertAlphaToAdditiveAlpha_HWY);
HWY_EXPORT(Convert24BitTo32Bit_HWY);
HWY_EXPORT(Convert32BitTo24Bit_HWY);
HWY_EXPORT(ReverseRGB_HWY);
}  // namespace krkr2

extern "C" {
void TVPConvertAdditiveAlphaToAlpha_hwy(tjs_uint32 *buf, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(ConvertAdditiveAlphaToAlpha_HWY)(buf, len);
}
void TVPConvertAlphaToAdditiveAlpha_hwy(tjs_uint32 *buf, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(ConvertAlphaToAdditiveAlpha_HWY)(buf, len);
}
void TVPConvert24BitTo32Bit_hwy(tjs_uint32 *dest, const tjs_uint8 *buf,
                                 tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(Convert24BitTo32Bit_HWY)(dest, buf, len);
}
void TVPConvert32BitTo24Bit_hwy(tjs_uint8 *dest, const tjs_uint8 *buf,
                                 tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(Convert32BitTo24Bit_HWY)(dest, buf, len);
}
void TVPReverseRGB_hwy(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(ReverseRGB_HWY)(dest, src, len);
}
}  // extern "C"
#endif
