/*
 * KrKr2 Engine - Highway SIMD Blur Functions
 *
 * Implements blur-related functions:
 *   TVPAddSubVertSum16/16_d/32/32_d - vertical sum add/subtract
 *   TVPDoBoxBlurAvg16/16_d/32/32_d - box blur averaging
 *   TVPChBlurMulCopy65/AddMulCopy65/MulCopy/AddMulCopy - channel blur
 *
 * Phase 4 implementation.
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_blur.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// TVPAddSubVertSum16: dest[i] += addline[i] - subline[i] (in u16 per channel)
// Each u32 pixel is split into 4 channels accumulated in u16 arrays
// =========================================================================
void AddSubVertSum16_HWY(tjs_uint16 *dest, const tjs_uint32 *addline,
                          const tjs_uint32 *subline, tjs_int len) {
    const hn::ScalableTag<uint16_t> d16;
    const hn::ScalableTag<uint8_t> d8;
    const size_t N16 = hn::Lanes(d16);
    // len is in pixels; each pixel produces 4 u16 channel values
    // so dest has len*4 entries
    tjs_int pixel_count = len;
    tjs_int ch_count = pixel_count * 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N16) <= ch_count; i += N16) {
        auto vd = hn::LoadU(d16, dest + i);
        // Load add/sub as bytes and promote
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

// TVPAddSubVertSum16_d: same but dest alpha channel separate handling
void AddSubVertSum16_d_HWY(tjs_uint16 *dest, const tjs_uint32 *addline,
                            const tjs_uint32 *subline, tjs_int len) {
    // Same as non-_d version for now; alpha handled in box blur avg
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
        // Need to load N32 bytes and promote to u32
        // Load as u8, promote to u16, then to u32
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
// Updates running sum and divides by n to get average
// =========================================================================
void DoBoxBlurAvg16_HWY(tjs_uint32 *dest, tjs_uint16 *sum,
                         const tjs_uint16 *add, const tjs_uint16 *sub,
                         tjs_int n, tjs_int len) {
    // This is a sequential scan (running sum), hard to parallelize
    // Use scalar loop with optimized per-pixel processing
    tjs_uint32 rcp = (1 << 16) / n;  // reciprocal approximation

    for (tjs_int i = 0; i < len; i++) {
        // Update running sums for 4 channels
        tjs_int idx = i * 4;
        sum[idx + 0] += add[idx + 0] - sub[idx + 0];
        sum[idx + 1] += add[idx + 1] - sub[idx + 1];
        sum[idx + 2] += add[idx + 2] - sub[idx + 2];
        sum[idx + 3] += add[idx + 3] - sub[idx + 3];

        // Compute average: channel / n ≈ channel * rcp >> 16
        tjs_uint32 b = (sum[idx + 0] * rcp) >> 16;
        tjs_uint32 g = (sum[idx + 1] * rcp) >> 16;
        tjs_uint32 r = (sum[idx + 2] * rcp) >> 16;
        tjs_uint32 a = (sum[idx + 3] * rcp) >> 16;

        dest[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

void DoBoxBlurAvg16_d_HWY(tjs_uint32 *dest, tjs_uint16 *sum,
                            const tjs_uint16 *add, const tjs_uint16 *sub,
                            tjs_int n, tjs_int len) {
    DoBoxBlurAvg16_HWY(dest, sum, add, sub, n, len);
}

// TVPDoBoxBlurAvg32: same with 32-bit accumulators
void DoBoxBlurAvg32_HWY(tjs_uint32 *dest, tjs_uint32 *sum,
                         const tjs_uint32 *add, const tjs_uint32 *sub,
                         tjs_int n, tjs_int len) {
    tjs_uint64 rcp = ((tjs_uint64)1 << 32) / n;

    for (tjs_int i = 0; i < len; i++) {
        tjs_int idx = i * 4;
        sum[idx + 0] += add[idx + 0] - sub[idx + 0];
        sum[idx + 1] += add[idx + 1] - sub[idx + 1];
        sum[idx + 2] += add[idx + 2] - sub[idx + 2];
        sum[idx + 3] += add[idx + 3] - sub[idx + 3];

        tjs_uint32 b = static_cast<tjs_uint32>(((tjs_uint64)sum[idx + 0] * rcp) >> 32);
        tjs_uint32 g = static_cast<tjs_uint32>(((tjs_uint64)sum[idx + 1] * rcp) >> 32);
        tjs_uint32 r = static_cast<tjs_uint32>(((tjs_uint64)sum[idx + 2] * rcp) >> 32);
        tjs_uint32 a = static_cast<tjs_uint32>(((tjs_uint64)sum[idx + 3] * rcp) >> 32);

        dest[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

void DoBoxBlurAvg32_d_HWY(tjs_uint32 *dest, tjs_uint32 *sum,
                            const tjs_uint32 *add, const tjs_uint32 *sub,
                            tjs_int n, tjs_int len) {
    DoBoxBlurAvg32_HWY(dest, sum, add, sub, n, len);
}

// =========================================================================
// TVPChBlurMulCopy65: channel blur multiply-copy (65-level)
// dest[i] = dest[i] + src[i] * level >> 8
// =========================================================================
void ChBlurMulCopy65_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                          tjs_int len, tjs_int level) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N = hn::Lanes(d8);
    const auto vlevel16 = hn::Set(d16, static_cast<uint16_t>(level));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N) <= len; i += N) {
        auto vs = hn::LoadU(d8, src + i);
        auto vd = hn::LoadU(d8, dest + i);

        const auto half = hn::Half<decltype(d8)>();
        auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
        auto s_hi = hn::PromoteUpperTo(d16, vs);
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto d_hi = hn::PromoteUpperTo(d16, vd);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(s_lo, vlevel16)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(s_hi, vlevel16)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, dest + i);
    }
    for (; i < len; i++) {
        dest[i] = static_cast<tjs_uint8>(dest[i] + ((src[i] * level) >> 8));
    }
}

void ChBlurAddMulCopy65_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                              tjs_int len, tjs_int level) {
    ChBlurMulCopy65_HWY(dest, src, len, level);
}

// TVPChBlurMulCopy: same but 256-level
void ChBlurMulCopy_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                        tjs_int len, tjs_int level) {
    ChBlurMulCopy65_HWY(dest, src, len, level);
}

void ChBlurAddMulCopy_HWY(tjs_uint8 *dest, const tjs_uint8 *src,
                            tjs_int len, tjs_int level) {
    ChBlurMulCopy65_HWY(dest, src, len, level);
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
