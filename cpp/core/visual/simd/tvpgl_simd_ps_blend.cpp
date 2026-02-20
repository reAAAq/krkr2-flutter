/*
 * KrKr2 Engine - Highway SIMD Photoshop Blend Modes (Part 1)
 *
 * Implements fully SIMD-able PS blend modes:
 *   PsAlphaBlend, PsAddBlend, PsSubBlend, PsMulBlend,
 *   PsScreenBlend, PsLightenBlend, PsDarkenBlend, PsDiffBlend
 *
 * Each with 4 variants: base, _o (opacity), _HDA, _HDA_o
 * Total: 32 function entries
 *
 * All PS blends follow the pattern:
 *   1. Compute blended color 's' per-channel
 *   2. Apply ps_alpha_blend: result_ch = d_ch + (s_ch - d_ch) * alpha >> 8
 *
 * Phase 3 implementation.
 */

#include "tjsTypes.h"
#include "tvpgl.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_ps_blend.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// =========================================================================
// Common helper: ps_alpha_blend step
// Applies: result = d + (s - d) * a >> 8  (per channel, preserving alpha)
// All inputs are u8 vectors. alpha must be broadcast per-pixel.
// =========================================================================
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> PsApplyAlpha(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> vd,
    hn::Vec<hn::ScalableTag<uint8_t>> vs_blended,
    hn::Vec<hn::ScalableTag<uint8_t>> va) {

    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const auto half = hn::Half<decltype(d8)>();
    const auto v255 = hn::Set(d16, static_cast<uint16_t>(255));

    auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs_blended));
    auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
    auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));

    auto s_hi = hn::PromoteUpperTo(d16, vs_blended);
    auto d_hi = hn::PromoteUpperTo(d16, vd);
    auto a_hi = hn::PromoteUpperTo(d16, va);

    // result = (s * a + d * (255 - a)) >> 8, all unsigned, no overflow
    auto inv_a_lo = hn::Sub(v255, a_lo);
    auto inv_a_hi = hn::Sub(v255, a_hi);
    auto r_lo = hn::ShiftRight<8>(hn::Add(hn::Mul(s_lo, a_lo), hn::Mul(d_lo, inv_a_lo)));
    auto r_hi = hn::ShiftRight<8>(hn::Add(hn::Mul(s_hi, a_hi), hn::Mul(d_hi, inv_a_hi)));

    return hn::OrderedDemote2To(d8, r_lo, r_hi);
}

// Helper: Extract alpha from src and broadcast to all channels
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> ExtractAlpha(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> vs) {
    const auto alpha_shuffle = hn::Dup128VecFromValues(
        d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15
    );
    return hn::TableLookupBytes(vs, alpha_shuffle);
}

// Helper: Scale alpha by opacity
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> ScaleAlpha(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> va,
    hn::Vec<hn::ScalableTag<uint16_t>> vopa16) {
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const auto half = hn::Half<decltype(d8)>();
    auto a_lo = hn::ShiftRight<8>(hn::Mul(hn::PromoteTo(d16, hn::LowerHalf(half, va)), vopa16));
    auto a_hi = hn::ShiftRight<8>(hn::Mul(hn::PromoteUpperTo(d16, va), vopa16));
    return hn::OrderedDemote2To(d8, a_lo, a_hi);
}

// Helper: Per-channel multiply (d * s) >> 8 in u16
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> MulChannels(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> va,
    hn::Vec<hn::ScalableTag<uint8_t>> vb) {
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const auto half = hn::Half<decltype(d8)>();
    auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
    auto b_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vb));
    auto a_hi = hn::PromoteUpperTo(d16, va);
    auto b_hi = hn::PromoteUpperTo(d16, vb);
    auto r_lo = hn::ShiftRight<8>(hn::Mul(a_lo, b_lo));
    auto r_hi = hn::ShiftRight<8>(hn::Mul(a_hi, b_hi));
    return hn::OrderedDemote2To(d8, r_lo, r_hi);
}

// Helper: alpha_mask constant
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> GetAlphaMask(
    hn::ScalableTag<uint8_t> d8) {
    return hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);
}

// Helper: Apply HDA
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> ApplyHDA(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> result,
    hn::Vec<hn::ScalableTag<uint8_t>> vd) {
    auto mask = GetAlphaMask(d8);
    return hn::Or(hn::AndNot(mask, result), hn::And(mask, vd));
}

// =========================================================================
// Macro for generating 4 variants of each PS blend mode
// BLEND_CORE(d8, vs, vd) must produce the blended color before alpha application
// =========================================================================

#define DEFINE_PS_BLEND_4VARIANTS(Name, blend_core_body)                      \
                                                                               \
void Ps##Name##Blend_HWY(tjs_uint32 *dest, const tjs_uint32 *src,            \
                          tjs_int len) {                                       \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    tjs_int i = 0;                                                            \
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {        \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ExtractAlpha(d8, vs);                                       \
        auto vs_blended = [&]() { blend_core_body }();                        \
        auto result = PsApplyAlpha(d8, vd, vs_blended, va);                   \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i];                                   \
        tjs_uint32 a = s >> 24;                                               \
        TVPPsAlphaBlend_##Name##_scalar(dest + i, d, s, a);                   \
    }                                                                          \
}                                                                              \
                                                                               \
void Ps##Name##Blend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,          \
                            tjs_int len, tjs_int opa) {                       \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const hn::Repartition<uint16_t, decltype(d8)> d16;                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));             \
    tjs_int i = 0;                                                            \
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {        \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ScaleAlpha(d8, ExtractAlpha(d8, vs), vopa16);               \
        auto vs_blended = [&]() { blend_core_body }();                        \
        auto result = PsApplyAlpha(d8, vd, vs_blended, va);                   \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i];                                   \
        tjs_uint32 a = ((s >> 24) * opa) >> 8;                                \
        TVPPsAlphaBlend_##Name##_scalar(dest + i, d, s, a);                   \
    }                                                                          \
}                                                                              \
                                                                               \
void Ps##Name##Blend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src,        \
                              tjs_int len) {                                   \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    tjs_int i = 0;                                                            \
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {        \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ExtractAlpha(d8, vs);                                       \
        auto vs_blended = [&]() { blend_core_body }();                        \
        auto result = ApplyHDA(d8, PsApplyAlpha(d8, vd, vs_blended, va), vd); \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i];                                   \
        tjs_uint32 a = s >> 24;                                               \
        TVPPsAlphaBlend_##Name##_scalar(dest + i, d, s, a);                   \
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);                  \
    }                                                                          \
}                                                                              \
                                                                               \
void Ps##Name##Blend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,      \
                                tjs_int len, tjs_int opa) {                   \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const hn::Repartition<uint16_t, decltype(d8)> d16;                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));             \
    tjs_int i = 0;                                                            \
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {        \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ScaleAlpha(d8, ExtractAlpha(d8, vs), vopa16);               \
        auto vs_blended = [&]() { blend_core_body }();                        \
        auto result = ApplyHDA(d8, PsApplyAlpha(d8, vd, vs_blended, va), vd); \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i];                                   \
        tjs_uint32 a = ((s >> 24) * opa) >> 8;                                \
        TVPPsAlphaBlend_##Name##_scalar(dest + i, d, s, a);                   \
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);                  \
    }                                                                          \
}

// =========================================================================
// Scalar fallback helpers for each mode
// =========================================================================

// PsAlpha: s is just src itself
static HWY_INLINE void TVPPsAlphaBlend_Alpha_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 d1 = d & 0x00ff00ff;
    tjs_uint32 d2 = d & 0x0000ff00;
    *dest = ((((((s & 0x00ff00ff) - d1) * a) >> 8) + d1) & 0x00ff00ff) |
            ((((((s & 0x0000ff00) - d2) * a) >> 8) + d2) & 0x0000ff00);
}

// PsAdd: saturated add then alpha blend
static HWY_INLINE void TVPPsAlphaBlend_Add_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 n = (((d & s) << 1) + ((d ^ s) & 0x00fefefe)) & 0x01010100;
    n = ((n >> 8) + 0x007f7f7f) ^ 0x007f7f7f;
    s = (d + s - n) | n;
    TVPPsAlphaBlend_Alpha_scalar(dest, d, s, a);
}

// PsSub: linear burn
static HWY_INLINE void TVPPsAlphaBlend_Sub_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 si = ~s;
    tjs_uint32 n = (((~d & si) << 1) + ((~d ^ si) & 0x00fefefe)) & 0x01010100;
    n = ((n >> 8) + 0x007f7f7f) ^ 0x007f7f7f;
    s = (d | n) - (si | n);
    TVPPsAlphaBlend_Alpha_scalar(dest, d, s, a);
}

// PsMul: multiply
static HWY_INLINE void TVPPsAlphaBlend_Mul_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    s = (((((d >> 16) & 0xff) * (s & 0x00ff0000)) & 0xff000000) |
         ((((d >> 8) & 0xff) * (s & 0x0000ff00)) & 0x00ff0000) |
         ((((d >> 0) & 0xff) * (s & 0x000000ff)))) >> 8;
    TVPPsAlphaBlend_Alpha_scalar(dest, d, s, a);
}

// PsScreen: s - s*d/255
static HWY_INLINE void TVPPsAlphaBlend_Screen_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 sd1 = (((((d >> 16) & 0xff) * (s & 0x00ff0000)) & 0xff000000) |
                       ((((d >> 0) & 0xff) * (s & 0x000000ff)))) >> 8;
    tjs_uint32 sd2 = (((((d >> 8) & 0xff) * (s & 0x0000ff00)) & 0x00ff0000)) >> 8;
    *dest = ((((((s & 0x00ff00ff) - sd1) * a) >> 8) + (d & 0x00ff00ff)) & 0x00ff00ff) |
            ((((((s & 0x0000ff00) - sd2) * a) >> 8) + (d & 0x0000ff00)) & 0x0000ff00);
}

// PsLighten: max(d, s)
static HWY_INLINE void TVPPsAlphaBlend_Lighten_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 n = (((~d & s) << 1) + ((~d ^ s) & 0x00fefefe)) & 0x01010100;
    n = ((n >> 8) + 0x007f7f7f) ^ 0x007f7f7f;
    s = (s & n) | (d & ~n);
    TVPPsAlphaBlend_Alpha_scalar(dest, d, s, a);
}

// PsDarken: min(d, s)
static HWY_INLINE void TVPPsAlphaBlend_Darken_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 n = (((~d & s) << 1) + ((~d ^ s) & 0x00fefefe)) & 0x01010100;
    n = ((n >> 8) + 0x007f7f7f) ^ 0x007f7f7f;
    s = (d & n) | (s & ~n);
    TVPPsAlphaBlend_Alpha_scalar(dest, d, s, a);
}

// PsDiff: |d - s|
static HWY_INLINE void TVPPsAlphaBlend_Diff_scalar(
    tjs_uint32 *dest, tjs_uint32 d, tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 n = (((~d & s) << 1) + ((~d ^ s) & 0x00fefefe)) & 0x01010100;
    n = ((n >> 8) + 0x007f7f7f) ^ 0x007f7f7f;
    s = ((s & n) - (d & n)) | ((d & ~n) - (s & ~n));
    TVPPsAlphaBlend_Alpha_scalar(dest, d, s, a);
}

// =========================================================================
// 1. PsAlphaBlend: blended = src (identity, just alpha blend)
// =========================================================================
DEFINE_PS_BLEND_4VARIANTS(Alpha,
    return vs;
)

// =========================================================================
// 2. PsAddBlend: blended = SaturatedAdd(d, s)
// =========================================================================
DEFINE_PS_BLEND_4VARIANTS(Add,
    return hn::SaturatedAdd(vd, vs);
)

// =========================================================================
// 3. PsSubBlend: Linear Burn = d - ~s (clamped)
//    Original: n = carry; s = (d | n) - (~s | n)
//    Simplified SIMD: SaturatedSub(d, ~s) then the result goes through alpha
//    Actually from the scalar: s_new = (d|n) - (~s|n) which is clamp(d - ~s, 0)
// =========================================================================
DEFINE_PS_BLEND_4VARIANTS(Sub,
    auto si = hn::Not(vs);
    return hn::SaturatedSub(vd, si);
)

// =========================================================================
// 4. PsMulBlend: blended = (d * s) >> 8 per channel
// =========================================================================
DEFINE_PS_BLEND_4VARIANTS(Mul,
    return MulChannels(d8, vd, vs);
)

// =========================================================================
// 5. PsScreenBlend: result_ch = d + (s - s*d/255) * a / 256
//    The screen blend color is s - s*d/255, but for PsScreen the final
//    formula is: result = d + (s - s*d/255) * a >> 8
//    Which is different from standard screen blend.
// =========================================================================
void PsScreenBlend_HWY(tjs_uint32 *dest, const tjs_uint32 *src, tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va = ExtractAlpha(d8, vs);

        // Compute s*d/255 per channel
        auto sd = MulChannels(d8, vs, vd);
        // Blend: s_effective = s - s*d/255
        // Final: result = d + (s - sd - d) * a >> 8 = d + ((s - sd) - d) * a >> 8
        // But PsScreen formula is: result_ch = d + (s - sd) * a >> 8 - d * a >> 8 + d
        // Actually: result = ((s - sd - d) * a >> 8) + d
        // Which is just: PsApplyAlpha(d, s - sd, a)
        auto s_minus_sd = hn::SaturatedSub(vs, sd);

        // ps_alpha_blend: result = d + (s_eff - d) * a >> 8
        // where s_eff = s - s*d/255
        // We need to compute this differently because s_eff is not simply s
        // Actually the correct formula from scalar:
        //   result = (((s - sd) * a >> 8) + d) for each channel
        // This is equivalent to PsApplyAlpha with s_effective = s - sd
        // But s - sd can exceed 255, but since both are u8, saturated sub handles it

        const auto half = hn::Half<decltype(d8)>();
        auto se_lo = hn::PromoteTo(d16, hn::LowerHalf(half, s_minus_sd));
        auto se_hi = hn::PromoteUpperTo(d16, s_minus_sd);
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
        auto a_hi = hn::PromoteUpperTo(d16, va);

        // result = d + (s_eff * a >> 8) where s_eff = s - sd
        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(se_lo, a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(se_hi, a_hi)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = s >> 24;
        TVPPsAlphaBlend_Screen_scalar(dest + i, d, s, a);
    }
}

void PsScreenBlend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                          tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va = ScaleAlpha(d8, ExtractAlpha(d8, vs), vopa16);
        auto sd = MulChannels(d8, vs, vd);
        auto s_minus_sd = hn::SaturatedSub(vs, sd);

        const auto half = hn::Half<decltype(d8)>();
        auto se_lo = hn::PromoteTo(d16, hn::LowerHalf(half, s_minus_sd));
        auto se_hi = hn::PromoteUpperTo(d16, s_minus_sd);
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
        auto a_hi = hn::PromoteUpperTo(d16, va);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(se_lo, a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(se_hi, a_hi)));

        auto result = hn::OrderedDemote2To(d8, r_lo, r_hi);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = ((s >> 24) * opa) >> 8;
        TVPPsAlphaBlend_Screen_scalar(dest + i, d, s, a);
    }
}

void PsScreenBlend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                            tjs_int len) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va = ExtractAlpha(d8, vs);
        auto sd = MulChannels(d8, vs, vd);
        auto s_minus_sd = hn::SaturatedSub(vs, sd);

        const auto half = hn::Half<decltype(d8)>();
        auto se_lo = hn::PromoteTo(d16, hn::LowerHalf(half, s_minus_sd));
        auto se_hi = hn::PromoteUpperTo(d16, s_minus_sd);
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
        auto a_hi = hn::PromoteUpperTo(d16, va);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(se_lo, a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(se_hi, a_hi)));

        auto result = ApplyHDA(d8, hn::OrderedDemote2To(d8, r_lo, r_hi), vd);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = s >> 24;
        TVPPsAlphaBlend_Screen_scalar(dest + i, d, s, a);
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);
    }
}

void PsScreenBlend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,
                              tjs_int len, tjs_int opa) {
    const hn::ScalableTag<uint8_t> d8;
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const size_t N_PIXELS = hn::Lanes(d8) / 4;
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));

    tjs_int i = 0;
    for (; i + static_cast<tjs_int>(N_PIXELS) <= len; i += N_PIXELS) {
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i));
        auto va = ScaleAlpha(d8, ExtractAlpha(d8, vs), vopa16);
        auto sd = MulChannels(d8, vs, vd);
        auto s_minus_sd = hn::SaturatedSub(vs, sd);

        const auto half = hn::Half<decltype(d8)>();
        auto se_lo = hn::PromoteTo(d16, hn::LowerHalf(half, s_minus_sd));
        auto se_hi = hn::PromoteUpperTo(d16, s_minus_sd);
        auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
        auto d_hi = hn::PromoteUpperTo(d16, vd);
        auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
        auto a_hi = hn::PromoteUpperTo(d16, va);

        auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(se_lo, a_lo)));
        auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(se_hi, a_hi)));

        auto result = ApplyHDA(d8, hn::OrderedDemote2To(d8, r_lo, r_hi), vd);
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));
    }

    for (; i < len; i++) {
        tjs_uint32 s = src[i], d = dest[i];
        tjs_uint32 a = ((s >> 24) * opa) >> 8;
        TVPPsAlphaBlend_Screen_scalar(dest + i, d, s, a);
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);
    }
}

// =========================================================================
// 6. PsLightenBlend: blended = max(d, s) per channel
// =========================================================================
DEFINE_PS_BLEND_4VARIANTS(Lighten,
    return hn::Max(vd, vs);
)

// =========================================================================
// 7. PsDarkenBlend: blended = min(d, s) per channel
// =========================================================================
DEFINE_PS_BLEND_4VARIANTS(Darken,
    return hn::Min(vd, vs);
)

// =========================================================================
// 8. PsDiffBlend: blended = |d - s| per channel
// =========================================================================
DEFINE_PS_BLEND_4VARIANTS(Diff,
    // |d - s| = max(d,s) - min(d,s) for unsigned
    auto mx = hn::Max(vd, vs);
    auto mn = hn::Min(vd, vs);
    return hn::Sub(mx, mn);
)

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

// --- Export functions ---
#if HWY_ONCE
namespace krkr2 {
// PsAlpha
HWY_EXPORT(PsAlphaBlend_HWY);
HWY_EXPORT(PsAlphaBlend_o_HWY);
HWY_EXPORT(PsAlphaBlend_HDA_HWY);
HWY_EXPORT(PsAlphaBlend_HDA_o_HWY);
// PsAdd
HWY_EXPORT(PsAddBlend_HWY);
HWY_EXPORT(PsAddBlend_o_HWY);
HWY_EXPORT(PsAddBlend_HDA_HWY);
HWY_EXPORT(PsAddBlend_HDA_o_HWY);
// PsSub
HWY_EXPORT(PsSubBlend_HWY);
HWY_EXPORT(PsSubBlend_o_HWY);
HWY_EXPORT(PsSubBlend_HDA_HWY);
HWY_EXPORT(PsSubBlend_HDA_o_HWY);
// PsMul
HWY_EXPORT(PsMulBlend_HWY);
HWY_EXPORT(PsMulBlend_o_HWY);
HWY_EXPORT(PsMulBlend_HDA_HWY);
HWY_EXPORT(PsMulBlend_HDA_o_HWY);
// PsScreen
HWY_EXPORT(PsScreenBlend_HWY);
HWY_EXPORT(PsScreenBlend_o_HWY);
HWY_EXPORT(PsScreenBlend_HDA_HWY);
HWY_EXPORT(PsScreenBlend_HDA_o_HWY);
// PsLighten
HWY_EXPORT(PsLightenBlend_HWY);
HWY_EXPORT(PsLightenBlend_o_HWY);
HWY_EXPORT(PsLightenBlend_HDA_HWY);
HWY_EXPORT(PsLightenBlend_HDA_o_HWY);
// PsDarken
HWY_EXPORT(PsDarkenBlend_HWY);
HWY_EXPORT(PsDarkenBlend_o_HWY);
HWY_EXPORT(PsDarkenBlend_HDA_HWY);
HWY_EXPORT(PsDarkenBlend_HDA_o_HWY);
// PsDiff
HWY_EXPORT(PsDiffBlend_HWY);
HWY_EXPORT(PsDiffBlend_o_HWY);
HWY_EXPORT(PsDiffBlend_HDA_HWY);
HWY_EXPORT(PsDiffBlend_HDA_o_HWY);
}  // namespace krkr2

extern "C" {

#define EXPORT_PS_BLEND(Name)                                                  \
void TVPPs##Name##Blend_hwy(tjs_uint32 *dest, const tjs_uint32 *src,          \
                            tjs_int len) {                                     \
    krkr2::HWY_DYNAMIC_DISPATCH(Ps##Name##Blend_HWY)(dest, src, len);         \
}                                                                              \
void TVPPs##Name##Blend_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,        \
                              tjs_int len, tjs_int opa) {                      \
    krkr2::HWY_DYNAMIC_DISPATCH(Ps##Name##Blend_o_HWY)(dest, src, len, opa);  \
}                                                                              \
void TVPPs##Name##Blend_HDA_hwy(tjs_uint32 *dest, const tjs_uint32 *src,      \
                                tjs_int len) {                                 \
    krkr2::HWY_DYNAMIC_DISPATCH(Ps##Name##Blend_HDA_HWY)(dest, src, len);     \
}                                                                              \
void TVPPs##Name##Blend_HDA_o_hwy(tjs_uint32 *dest, const tjs_uint32 *src,    \
                                  tjs_int len, tjs_int opa) {                  \
    krkr2::HWY_DYNAMIC_DISPATCH(Ps##Name##Blend_HDA_o_HWY)(dest, src, len, opa); \
}

EXPORT_PS_BLEND(Alpha)
EXPORT_PS_BLEND(Add)
EXPORT_PS_BLEND(Sub)
EXPORT_PS_BLEND(Mul)
EXPORT_PS_BLEND(Screen)
EXPORT_PS_BLEND(Lighten)
EXPORT_PS_BLEND(Darken)
EXPORT_PS_BLEND(Diff)

}  // extern "C"
#endif
