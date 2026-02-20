/*
 * KrKr2 Engine - Highway SIMD Blur Functions
 *
 * Implements blur-related functions:
 *   TVPAddSubVertSum16/16_d/32/32_d - vertical sum add/subtract (SIMD)
 *   TVPDoBoxBlurAvg16/16_d/32/32_d - box blur averaging (scalar, sequential)
 *   TVPChBlurMulCopy65/AddMulCopy65/MulCopy/AddMulCopy - channel blur
 *
 * Phase 4 implementation.
 * FIX: DoBoxBlurAvg - sum[] is a 4-element running accumulator, not per-pixel.
 *      Output before update. Added half_n rounding.
 * FIX: DoBoxBlurAvg16_d uses TVPDivTable for alpha-aware output.
 * FIX: ChBlurMulCopy65 - correct formula: dest = min(src*level >> 18, 255)
 *      ChBlurAddMulCopy65: dest = min(dest + src*level >> 18, 255)
 */

#include "tjsTypes.h"
#include "tvpgl.h"

extern "C" {
extern tjs_uint8 TVPDivTable[256 * 256];
}

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_blur.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// TVPAddSubVertSum16: dest[ch] += addline_ch - subline_ch for all channels
// =========================================================================
void AddSubVertSum16_HWY(tjs_uint16 *dest, const tjs_uint32 *addline,
                          const tjs_uint32 *subline, tjs_int len) {
    const hn::ScalableTag<uint16_t> d16;
    const hn::ScalableTag<uint8_t> d8;
    const size_t N16 = hn::Lanes(d16);
    tjs_int ch_count = len * 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N16) <= ch_count; i += N16) {
        auto vd = hn::LoadU(d16, dest + i);
        auto va8 = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(addline) + i);
        auto vs8 = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(subline) + i);
        auto va = hn::PromoteTo(d16, hn::LowerHalf(hn::Half<decltype(d8)>(), va8));
        auto vs = hn::PromoteTo(d16, hn::LowerHalf(hn::Half<decltype(d8)>(), vs8));
        auto result = hn::Add(hn::Sub(vd, vs), va);
        hn::StoreU(result, d16, dest + i);
    }
    for (; i < ch_count; i++) {
        dest[i] += reinterpret_cast<const uint8_t*>(addline)[i]
                 - reinterpret_cast<const uint8_t*>(subline)[i];
    }
}

void AddSubVertSum16_d_HWY(tjs_uint16 *dest, const tjs_uint32 *addline,
                            const tjs_uint32 *subline, tjs_int len) {
    AddSubVertSum16_HWY(dest, addline, subline, len);
}

// =========================================================================
// TVPAddSubVertSum32: same but with u32 accumulators
// =========================================================================
void AddSubVertSum32_HWY(tjs_uint32 *dest, const tjs_uint32 *addline,
                          const tjs_uint32 *subline, tjs_int len) {
    const hn::ScalableTag<uint32_t> d32;
    const hn::ScalableTag<uint8_t> d8;
    const hn::ScalableTag<uint16_t> d16;
    const size_t N32 = hn::Lanes(d32);
    tjs_int ch_count = len * 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N32) <= ch_count; i += N32) {
        auto vd = hn::LoadU(d32, dest + i);
        auto va_u8 = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(addline) + i);
        auto vs_u8 = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(subline) + i);

        auto va_lo16 = hn::PromoteTo(d16, hn::LowerHalf(hn::Half<decltype(d8)>(), va_u8));
        auto vs_lo16 = hn::PromoteTo(d16, hn::LowerHalf(hn::Half<decltype(d8)>(), vs_u8));

        auto va32 = hn::PromoteTo(d32, hn::LowerHalf(hn::Half<decltype(d16)>(), va_lo16));
        auto vs32 = hn::PromoteTo(d32, hn::LowerHalf(hn::Half<decltype(d16)>(), vs_lo16));

        auto result = hn::Add(hn::Sub(vd, vs32), va32);
        hn::StoreU(result, d32, dest + i);
    }
    for (; i < ch_count; i++) {
        dest[i] += reinterpret_cast<const uint8_t*>(addline)[i]
                 - reinterpret_cast<const uint8_t*>(subline)[i];
    }
}

void AddSubVertSum32_d_HWY(tjs_uint32 *dest, const tjs_uint32 *addline,
                            const tjs_uint32 *subline, tjs_int len) {
    AddSubVertSum32_HWY(dest, addline, subline, len);
}

// =========================================================================
// TVPDoBoxBlurAvg16: box blur horizontal pass with 16-bit accumulators
// sum[] is a 4-element running accumulator (B, G, R, A).
// For each pixel: output = (sum + half_n) / n, then update sum.
// =========================================================================
void DoBoxBlurAvg16_HWY(tjs_uint32 *dest, tjs_uint16 *sum,
                         const tjs_uint16 *add, const tjs_uint16 *sub,
                         tjs_int n, tjs_int len) {
    tjs_int rcp = (1 << 16) / n;
    tjs_int half_n = n >> 1;

    for (tjs_int i = 0; i < len; i++) {
        // Output FIRST using current sum (with rounding)
        dest[i] = (((sum[0] + half_n) * rcp >> 16)) +
                  (((sum[1] + half_n) * rcp >> 16) << 8) +
                  (((sum[2] + half_n) * rcp >> 16) << 16) +
                  (((sum[3] + half_n) * rcp >> 16) << 24);

        // THEN update running sum
        sum[0] += add[i * 4 + 0] - sub[i * 4 + 0];
        sum[1] += add[i * 4 + 1] - sub[i * 4 + 1];
        sum[2] += add[i * 4 + 2] - sub[i * 4 + 2];
        sum[3] += add[i * 4 + 3] - sub[i * 4 + 3];
    }
}

// TVPDoBoxBlurAvg16_d: alpha-aware version using TVPDivTable
void DoBoxBlurAvg16_d_HWY(tjs_uint32 *dest, tjs_uint16 *sum,
                            const tjs_uint16 *add, const tjs_uint16 *sub,
                            tjs_int n, tjs_int len) {
    tjs_int rcp = (1 << 16) / n;
    tjs_int half_n = n >> 1;

    for (tjs_int i = 0; i < len; i++) {
        tjs_int a = ((sum[3] + half_n) * rcp >> 16);
        tjs_uint8 *t = ::TVPDivTable + (a << 8);
        dest[i] = (t[(sum[0] + half_n) * rcp >> 16]) +
                  (t[(sum[1] + half_n) * rcp >> 16] << 8) +
                  (t[(sum[2] + half_n) * rcp >> 16] << 16) +
                  (a << 24);

        sum[0] += add[i * 4 + 0] - sub[i * 4 + 0];
        sum[1] += add[i * 4 + 1] - sub[i * 4 + 1];
        sum[2] += add[i * 4 + 2] - sub[i * 4 + 2];
        sum[3] += add[i * 4 + 3] - sub[i * 4 + 3];
    }
}

// TVPDoBoxBlurAvg32: same with 32-bit accumulators
void DoBoxBlurAvg32_HWY(tjs_uint32 *dest, tjs_uint32 *sum,
                         const tjs_uint32 *add, const tjs_uint32 *sub,
                         tjs_int n, tjs_int len) {
    tjs_uint64 rcp = ((tjs_uint64)1 << 32) / n;
    tjs_int half_n = n >> 1;

    for (tjs_int i = 0; i < len; i++) {
        tjs_uint32 b = static_cast<tjs_uint32>(((tjs_uint64)(sum[0] + half_n) * rcp) >> 32);
        tjs_uint32 g = static_cast<tjs_uint32>(((tjs_uint64)(sum[1] + half_n) * rcp) >> 32);
        tjs_uint32 r = static_cast<tjs_uint32>(((tjs_uint64)(sum[2] + half_n) * rcp) >> 32);
        tjs_uint32 a = static_cast<tjs_uint32>(((tjs_uint64)(sum[3] + half_n) * rcp) >> 32);

        dest[i] = (a << 24) | (r << 16) | (g << 8) | b;

        sum[0] += add[i * 4 + 0] - sub[i * 4 + 0];
        sum[1] += add[i * 4 + 1] - sub[i * 4 + 1];
        sum[2] += add[i * 4 + 2] - sub[i * 4 + 2];
        sum[3] += add[i * 4 + 3] - sub[i * 4 + 3];
    }
}

void DoBoxBlurAvg32_d_HWY(tjs_uint32 *dest, tjs_uint32 *sum,
                            const tjs_uint32 *add, const tjs_uint32 *sub,
                            tjs_int n, tjs_int len) {
    tjs_uint64 rcp = ((tjs_uint64)1 << 32) / n;
    tjs_int half_n = n >> 1;

    for (tjs_int i = 0; i < len; i++) {
        tjs_int a = static_cast<tjs_int>(((tjs_uint64)(sum[3] + half_n) * rcp) >> 32);
        tjs_uint8 *t = ::TVPDivTable + (a << 8);

        tjs_uint32 b_avg = static_cast<tjs_uint32>(((tjs_uint64)(sum[0] + half_n) * rcp) >> 32);
        tjs_uint32 g_avg = static_cast<tjs_uint32>(((tjs_uint64)(sum[1] + half_n) * rcp) >> 32);
        tjs_uint32 r_avg = static_cast<tjs_uint32>(((tjs_uint64)(sum[2] + half_n) * rcp) >> 32);

        dest[i] = t[b_avg] + (t[g_avg] << 8) + (t[r_avg] << 16) + (a << 24);

        sum[0] += add[i * 4 + 0] - sub[i * 4 + 0];
        sum[1] += add[i * 4 + 1] - sub[i * 4 + 1];
        sum[2] += add[i * 4 + 2] - sub[i * 4 + 2];
        sum[3] += add[i * 4 + 3] - sub[i * 4 + 3];
    }
}

// =========================================================================
// TVPChBlurMulCopy65: dest = min(src * level >> 18, 255)  (overwrite)
// =========================================================================
void ChBlurMulCopy65_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                          tjs_int len, tjs_int level) {
    for (tjs_int i = 0; i < len; i++) {
        tjs_int a = (src[i] * level) >> 18;
        if (a >= 255) a = 255;
        dest[i] = static_cast<tjs_uint8>(a);
    }
}

// TVPChBlurAddMulCopy65: dest = min(dest + src * level >> 18, 255) (accumulate + saturate)
void ChBlurAddMulCopy65_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                              tjs_int len, tjs_int level) {
    for (tjs_int i = 0; i < len; i++) {
        tjs_int a = dest[i] + (src[i] * level >> 18);
        if (a >= 255) a = 255;
        dest[i] = static_cast<tjs_uint8>(a);
    }
}

// TVPChBlurMulCopy: 256-level version, dest = min(src * level >> 8, 255) (overwrite)
void ChBlurMulCopy_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                        tjs_int len, tjs_int level) {
    for (tjs_int i = 0; i < len; i++) {
        tjs_int a = (src[i] * level) >> 8;
        if (a >= 255) a = 255;
        dest[i] = static_cast<tjs_uint8>(a);
    }
}

// TVPChBlurAddMulCopy: 256-level version, dest = min(dest + src * level >> 8, 255)
void ChBlurAddMulCopy_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                            tjs_int len, tjs_int level) {
    for (tjs_int i = 0; i < len; i++) {
        tjs_int a = dest[i] + (src[i] * level >> 8);
        if (a >= 255) a = 255;
        dest[i] = static_cast<tjs_uint8>(a);
    }
}

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace krkr2 {
HWY_EXPORT(AddSubVertSum16_HWY);
HWY_EXPORT(AddSubVertSum16_d_HWY);
HWY_EXPORT(AddSubVertSum32_HWY);
HWY_EXPORT(AddSubVertSum32_d_HWY);
HWY_EXPORT(DoBoxBlurAvg16_HWY);
HWY_EXPORT(DoBoxBlurAvg16_d_HWY);
HWY_EXPORT(DoBoxBlurAvg32_HWY);
HWY_EXPORT(DoBoxBlurAvg32_d_HWY);
HWY_EXPORT(ChBlurMulCopy65_HWY);
HWY_EXPORT(ChBlurAddMulCopy65_HWY);
HWY_EXPORT(ChBlurMulCopy_HWY);
HWY_EXPORT(ChBlurAddMulCopy_HWY);
}  // namespace krkr2

extern "C" {
void TVPAddSubVertSum16_hwy(tjs_uint16 *dest, const tjs_uint32 *addline,
                             const tjs_uint32 *subline, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddSubVertSum16_HWY)(dest, addline, subline, len);
}
void TVPAddSubVertSum16_d_hwy(tjs_uint16 *dest, const tjs_uint32 *addline,
                               const tjs_uint32 *subline, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddSubVertSum16_d_HWY)(dest, addline, subline, len);
}
void TVPAddSubVertSum32_hwy(tjs_uint32 *dest, const tjs_uint32 *addline,
                             const tjs_uint32 *subline, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddSubVertSum32_HWY)(dest, addline, subline, len);
}
void TVPAddSubVertSum32_d_hwy(tjs_uint32 *dest, const tjs_uint32 *addline,
                               const tjs_uint32 *subline, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(AddSubVertSum32_d_HWY)(dest, addline, subline, len);
}
void TVPDoBoxBlurAvg16_hwy(tjs_uint32 *dest, tjs_uint16 *sum,
                             const tjs_uint16 *add, const tjs_uint16 *sub,
                             tjs_int n, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(DoBoxBlurAvg16_HWY)(dest, sum, add, sub, n, len);
}
void TVPDoBoxBlurAvg16_d_hwy(tjs_uint32 *dest, tjs_uint16 *sum,
                               const tjs_uint16 *add, const tjs_uint16 *sub,
                               tjs_int n, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(DoBoxBlurAvg16_d_HWY)(dest, sum, add, sub, n, len);
}
void TVPDoBoxBlurAvg32_hwy(tjs_uint32 *dest, tjs_uint32 *sum,
                             const tjs_uint32 *add, const tjs_uint32 *sub,
                             tjs_int n, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(DoBoxBlurAvg32_HWY)(dest, sum, add, sub, n, len);
}
void TVPDoBoxBlurAvg32_d_hwy(tjs_uint32 *dest, tjs_uint32 *sum,
                               const tjs_uint32 *add, const tjs_uint32 *sub,
                               tjs_int n, tjs_int len) {
    krkr2::HWY_DYNAMIC_DISPATCH(DoBoxBlurAvg32_d_HWY)(dest, sum, add, sub, n, len);
}
void TVPChBlurMulCopy65_hwy(tjs_uint8 *dest, const tjs_uint8 *src,
                              tjs_int len, tjs_int level) {
    krkr2::HWY_DYNAMIC_DISPATCH(ChBlurMulCopy65_HWY)(dest, src, len, level);
}
void TVPChBlurAddMulCopy65_hwy(tjs_uint8 *dest, const tjs_uint8 *src,
                                tjs_int len, tjs_int level) {
    krkr2::HWY_DYNAMIC_DISPATCH(ChBlurAddMulCopy65_HWY)(dest, src, len, level);
}
void TVPChBlurMulCopy_hwy(tjs_uint8 *dest, const tjs_uint8 *src,
                            tjs_int len, tjs_int level) {
    krkr2::HWY_DYNAMIC_DISPATCH(ChBlurMulCopy_HWY)(dest, src, len, level);
}
void TVPChBlurAddMulCopy_hwy(tjs_uint8 *dest, const tjs_uint8 *src,
                               tjs_int len, tjs_int level) {
    krkr2::HWY_DYNAMIC_DISPATCH(ChBlurAddMulCopy_HWY)(dest, src, len, level);
}
}  // extern "C"
#endif
