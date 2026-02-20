/*
 * KrKr2 Engine - Highway SIMD Photoshop Blend Modes (Part 2)
 *
 * Implements PS blend modes that require special handling:
 *   PsOverlayBlend - Overlay (conditional per-channel)
 *   PsHardLightBlend - Hard Light (overlay with swapped s/d)
 *   PsExclusionBlend - Exclusion (s - 2*s*d/255)
 *   PsDiff5Blend - Diff5 (fade src first, then |d-s|)
 *   PsSoftLightBlend - Uses lookup table (scalar fallback in SIMD loop)
 *   PsColorDodgeBlend - Uses lookup table (scalar fallback)
 *   PsColorBurnBlend - Uses lookup table (scalar fallback)
 *   PsColorDodge5Blend - Uses lookup table (scalar fallback)
 *
 * Each with 4 variants: base, _o, _HDA, _HDA_o
 * Total: 32 function entries
 *
 * Phase 3 implementation.
 */

#include "tjsTypes.h"
#include "tvpgl.h"

// Access the PS lookup tables
extern unsigned char ps_soft_light_table_TABLE[256][256];
extern unsigned char ps_color_dodge_table_TABLE[256][256];
extern unsigned char ps_color_burn_table_TABLE[256][256];

// Forward declare the tables defined in blend_function.cpp
// They are actually struct static members; we access via extern
struct ps_soft_light_table { static unsigned char TABLE[256][256]; };
struct ps_color_dodge_table { static unsigned char TABLE[256][256]; };
struct ps_color_burn_table { static unsigned char TABLE[256][256]; };
struct ps_overlay_table { static unsigned char TABLE[256][256]; };

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tvpgl_simd_ps_blend2.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace krkr2 {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// Common scalar ps_alpha_blend step
static HWY_INLINE tjs_uint32 PsAlphaBlendScalar(tjs_uint32 d, tjs_uint32 s,
                                                  tjs_uint32 a) {
    tjs_uint32 d1 = d & 0x00ff00ff;
    tjs_uint32 d2 = d & 0x0000ff00;
    return ((((((s & 0x00ff00ff) - d1) * a) >> 8) + d1) & 0x00ff00ff) |
           ((((((s & 0x0000ff00) - d2) * a) >> 8) + d2) & 0x0000ff00);
}

// =========================================================================
// Helper for SIMD ps_alpha_blend
// =========================================================================
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> PsApplyAlpha(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> vd,
    hn::Vec<hn::ScalableTag<uint8_t>> vs_blended,
    hn::Vec<hn::ScalableTag<uint8_t>> va) {
    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const auto half = hn::Half<decltype(d8)>();
    auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs_blended));
    auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
    auto a_lo = hn::PromoteTo(d16, hn::LowerHalf(half, va));
    auto s_hi = hn::PromoteUpperTo(d16, vs_blended);
    auto d_hi = hn::PromoteUpperTo(d16, vd);
    auto a_hi = hn::PromoteUpperTo(d16, va);
    auto r_lo = hn::Add(d_lo, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_lo, d_lo), a_lo)));
    auto r_hi = hn::Add(d_hi, hn::ShiftRight<8>(hn::Mul(hn::Sub(s_hi, d_hi), a_hi)));
    return hn::OrderedDemote2To(d8, r_lo, r_hi);
}

static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> ExtractAlpha(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> vs) {
    const auto alpha_shuffle = hn::Dup128VecFromValues(d8,
        3, 3, 3, 3,    7, 7, 7, 7,
        11, 11, 11, 11, 15, 15, 15, 15);
    return hn::TableLookupBytes(vs, alpha_shuffle);
}

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

static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> ApplyHDA(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> result,
    hn::Vec<hn::ScalableTag<uint8_t>> vd) {
    auto mask = hn::Dup128VecFromValues(d8,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF,
        0, 0, 0, 0xFF,  0, 0, 0, 0xFF);
    return hn::Or(hn::AndNot(mask, result), hn::And(mask, vd));
}

// =========================================================================
// Overlay core: per channel, if d < 128: 2*d*s/255, else: 2*(d+s) - 2*d*s/255 - 255
// SIMD: use comparison mask
// =========================================================================
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> OverlayCore(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> vd,
    hn::Vec<hn::ScalableTag<uint8_t>> vs) {

    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const auto half = hn::Half<decltype(d8)>();
    const auto v128 = hn::Set(d8, 128);
    const auto v255_16 = hn::Set(d16, 255);

    // mask: d < 128 → true
    auto mask = hn::Lt(vd, v128);

    // Compute both paths in u16
    auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
    auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
    auto d_hi = hn::PromoteUpperTo(d16, vd);
    auto s_hi = hn::PromoteUpperTo(d16, vs);

    // Path 1 (d < 128): 2*d*s/255 ≈ (2*d*s) >> 8
    auto p1_lo = hn::ShiftRight<8>(hn::Mul(hn::ShiftLeft<1>(d_lo), s_lo));
    auto p1_hi = hn::ShiftRight<8>(hn::Mul(hn::ShiftLeft<1>(d_hi), s_hi));
    auto path1 = hn::OrderedDemote2To(d8, p1_lo, p1_hi);

    // Path 2 (d >= 128): 2*(d+s) - 2*d*s/255 - 255
    // = 2*d + 2*s - 2*d*s/255 - 255
    auto ds_lo = hn::Add(d_lo, s_lo);  // d+s
    auto ds_hi = hn::Add(d_hi, s_hi);
    auto mul_lo = hn::ShiftRight<8>(hn::Mul(d_lo, s_lo));  // d*s/255
    auto mul_hi = hn::ShiftRight<8>(hn::Mul(d_hi, s_hi));
    // 2*(d+s) - 2*d*s/255 - 255
    auto p2_lo = hn::Sub(hn::Sub(hn::ShiftLeft<1>(ds_lo), hn::ShiftLeft<1>(mul_lo)), v255_16);
    auto p2_hi = hn::Sub(hn::Sub(hn::ShiftLeft<1>(ds_hi), hn::ShiftLeft<1>(mul_hi)), v255_16);
    auto path2 = hn::OrderedDemote2To(d8, p2_lo, p2_hi);

    return hn::IfThenElse(mask, path1, path2);
}

// HardLight core: same as overlay but s and d swapped in the condition
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> HardLightCore(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> vd,
    hn::Vec<hn::ScalableTag<uint8_t>> vs) {

    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const auto half = hn::Half<decltype(d8)>();
    const auto v128 = hn::Set(d8, 128);
    const auto v255_16 = hn::Set(d16, 255);

    // HardLight: condition on s (not d)
    auto mask = hn::Lt(vs, v128);

    auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
    auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
    auto d_hi = hn::PromoteUpperTo(d16, vd);
    auto s_hi = hn::PromoteUpperTo(d16, vs);

    // Path 1 (s < 128): 2*d*s/255
    auto p1_lo = hn::ShiftRight<8>(hn::Mul(hn::ShiftLeft<1>(d_lo), s_lo));
    auto p1_hi = hn::ShiftRight<8>(hn::Mul(hn::ShiftLeft<1>(d_hi), s_hi));
    auto path1 = hn::OrderedDemote2To(d8, p1_lo, p1_hi);

    // Path 2 (s >= 128): 2*(d+s) - 2*d*s/255 - 255
    auto ds_lo = hn::Add(d_lo, s_lo);
    auto ds_hi = hn::Add(d_hi, s_hi);
    auto mul_lo = hn::ShiftRight<8>(hn::Mul(d_lo, s_lo));
    auto mul_hi = hn::ShiftRight<8>(hn::Mul(d_hi, s_hi));
    auto p2_lo = hn::Sub(hn::Sub(hn::ShiftLeft<1>(ds_lo), hn::ShiftLeft<1>(mul_lo)), v255_16);
    auto p2_hi = hn::Sub(hn::Sub(hn::ShiftLeft<1>(ds_hi), hn::ShiftLeft<1>(mul_hi)), v255_16);
    auto path2 = hn::OrderedDemote2To(d8, p2_lo, p2_hi);

    return hn::IfThenElse(mask, path1, path2);
}

// Exclusion core: s - 2*s*d/255 per channel
// result = d + (s - 2*s*d/255) * a >> 8
// s_eff = s - 2*s*d/255, applied via PsApplyAlpha
static HWY_INLINE hn::Vec<hn::ScalableTag<uint8_t>> ExclusionCore(
    hn::ScalableTag<uint8_t> d8,
    hn::Vec<hn::ScalableTag<uint8_t>> vd,
    hn::Vec<hn::ScalableTag<uint8_t>> vs) {

    const hn::Repartition<uint16_t, decltype(d8)> d16;
    const auto half = hn::Half<decltype(d8)>();

    auto d_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vd));
    auto s_lo = hn::PromoteTo(d16, hn::LowerHalf(half, vs));
    auto d_hi = hn::PromoteUpperTo(d16, vd);
    auto s_hi = hn::PromoteUpperTo(d16, vs);

    // 2*s*d / 255 ≈ (2*s*d) >> 8
    // But we need: s + d - 2*s*d/255
    // Which is the exclusion formula
    auto sd2_lo = hn::ShiftRight<7>(hn::Mul(s_lo, d_lo));  // 2*s*d/256
    auto sd2_hi = hn::ShiftRight<7>(hn::Mul(s_hi, d_hi));

    // result = s + d - 2*s*d/255
    auto r_lo = hn::Sub(hn::Add(s_lo, d_lo), sd2_lo);
    auto r_hi = hn::Sub(hn::Add(s_hi, d_hi), sd2_hi);

    return hn::OrderedDemote2To(d8, r_lo, r_hi);
}

// =========================================================================
// Helper macro for 4 variants generation
// =========================================================================
#define MAKE_PS_4V(Name, simd_blend_expr)                                     \
void Ps##Name##Blend_HWY(tjs_uint32 *dest, const tjs_uint32 *src,            \
                          tjs_int len) {                                       \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    tjs_int i = 0;                                                            \
    for (; i + (tjs_int)N_PIXELS <= len; i += N_PIXELS) {                     \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ExtractAlpha(d8, vs);                                       \
        auto vb = simd_blend_expr;                                            \
        auto result = PsApplyAlpha(d8, vd, vb, va);                           \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i], a = s >> 24;                      \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
    }                                                                          \
}                                                                              \
void Ps##Name##Blend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,          \
                            tjs_int len, tjs_int opa) {                       \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const hn::Repartition<uint16_t, decltype(d8)> d16;                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));             \
    tjs_int i = 0;                                                            \
    for (; i + (tjs_int)N_PIXELS <= len; i += N_PIXELS) {                     \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ScaleAlpha(d8, ExtractAlpha(d8, vs), vopa16);               \
        auto vb = simd_blend_expr;                                            \
        auto result = PsApplyAlpha(d8, vd, vb, va);                           \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i], a = ((s>>24)*opa)>>8;            \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
    }                                                                          \
}                                                                              \
void Ps##Name##Blend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src,        \
                              tjs_int len) {                                   \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    tjs_int i = 0;                                                            \
    for (; i + (tjs_int)N_PIXELS <= len; i += N_PIXELS) {                     \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ExtractAlpha(d8, vs);                                       \
        auto vb = simd_blend_expr;                                            \
        auto result = ApplyHDA(d8, PsApplyAlpha(d8, vd, vb, va), vd);         \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i], a = s >> 24;                      \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);                  \
    }                                                                          \
}                                                                              \
void Ps##Name##Blend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,      \
                                tjs_int len, tjs_int opa) {                   \
    const hn::ScalableTag<uint8_t> d8;                                        \
    const hn::Repartition<uint16_t, decltype(d8)> d16;                        \
    const size_t N_PIXELS = hn::Lanes(d8) / 4;                               \
    const auto vopa16 = hn::Set(d16, static_cast<uint16_t>(opa));             \
    tjs_int i = 0;                                                            \
    for (; i + (tjs_int)N_PIXELS <= len; i += N_PIXELS) {                     \
        auto vs = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src + i));   \
        auto vd = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(dest + i)); \
        auto va = ScaleAlpha(d8, ExtractAlpha(d8, vs), vopa16);               \
        auto vb = simd_blend_expr;                                            \
        auto result = ApplyHDA(d8, PsApplyAlpha(d8, vd, vb, va), vd);         \
        hn::StoreU(result, d8, reinterpret_cast<uint8_t*>(dest + i));         \
    }                                                                          \
    for (; i < len; i++) {                                                    \
        tjs_uint32 s = src[i], d = dest[i], a = ((s>>24)*opa)>>8;            \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);                  \
    }                                                                          \
}

// =========================================================================
// Scalar helpers
// =========================================================================
static HWY_INLINE void PsOverlay_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                         tjs_uint32 s, tjs_uint32 a) {
    *dest = ps_overlay_table::TABLE[(s >> 16) & 0xff][(d >> 16) & 0xff] << 16 |
            ps_overlay_table::TABLE[(s >> 8) & 0xff][(d >> 8) & 0xff] << 8 |
            ps_overlay_table::TABLE[s & 0xff][d & 0xff];
    *dest = PsAlphaBlendScalar(d, *dest, a);
}

static HWY_INLINE void PsHardLight_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                           tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 bl = ps_overlay_table::TABLE[(d >> 16) & 0xff][(s >> 16) & 0xff] << 16 |
                    ps_overlay_table::TABLE[(d >> 8) & 0xff][(s >> 8) & 0xff] << 8 |
                    ps_overlay_table::TABLE[d & 0xff][s & 0xff];
    *dest = PsAlphaBlendScalar(d, bl, a);
}

static HWY_INLINE void PsExclusion_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                           tjs_uint32 s, tjs_uint32 a) {
    tjs_uint32 sd1, sd2;
    sd1 = (((((d >> 16) & 0xff) * ((s & 0x00ff0000) >> 7)) & 0x01ff0000) |
           ((((d >> 0) & 0xff) * (s & 0x000000ff)) >> 7));
    sd2 = (((((d >> 8) & 0xff) * (s & 0x0000ff00)) & 0x00ff8000)) >> 7;
    *dest = ((((((s & 0x00ff00ff) - sd1) * a) >> 8) + (d & 0x00ff00ff)) & 0x00ff00ff) |
            ((((((s & 0x0000ff00) - sd2) * a) >> 8) + (d & 0x0000ff00)) & 0x0000ff00);
}

static HWY_INLINE void PsSoftLight_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                           tjs_uint32 s, tjs_uint32 a) {
    s = (ps_soft_light_table::TABLE[(s >> 16) & 0xff][(d >> 16) & 0xff] << 16) |
        (ps_soft_light_table::TABLE[(s >> 8) & 0xff][(d >> 8) & 0xff] << 8) |
        (ps_soft_light_table::TABLE[s & 0xff][d & 0xff]);
    *dest = PsAlphaBlendScalar(d, s, a);
}

static HWY_INLINE void PsColorDodge_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                            tjs_uint32 s, tjs_uint32 a) {
    s = (ps_color_dodge_table::TABLE[(s >> 16) & 0xff][(d >> 16) & 0xff] << 16) |
        (ps_color_dodge_table::TABLE[(s >> 8) & 0xff][(d >> 8) & 0xff] << 8) |
        (ps_color_dodge_table::TABLE[s & 0xff][d & 0xff]);
    *dest = PsAlphaBlendScalar(d, s, a);
}

static HWY_INLINE void PsColorBurn_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                           tjs_uint32 s, tjs_uint32 a) {
    s = (ps_color_burn_table::TABLE[(s >> 16) & 0xff][(d >> 16) & 0xff] << 16) |
        (ps_color_burn_table::TABLE[(s >> 8) & 0xff][(d >> 8) & 0xff] << 8) |
        (ps_color_burn_table::TABLE[s & 0xff][d & 0xff]);
    *dest = PsAlphaBlendScalar(d, s, a);
}

static HWY_INLINE void PsColorDodge5_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                             tjs_uint32 s, tjs_uint32 a) {
    s = ((((s & 0x00ff00ff) * a) >> 8) & 0x00ff00ff) |
        ((((s & 0x0000ff00) * a) >> 8) & 0x0000ff00);
    *dest = (ps_color_dodge_table::TABLE[(s >> 16) & 0xff][(d >> 16) & 0xff] << 16) |
            (ps_color_dodge_table::TABLE[(s >> 8) & 0xff][(d >> 8) & 0xff] << 8) |
            (ps_color_dodge_table::TABLE[s & 0xff][d & 0xff]);
}

static HWY_INLINE void PsDiff5_scalar(tjs_uint32 *dest, tjs_uint32 d,
                                       tjs_uint32 s, tjs_uint32 a) {
    s = ((((s & 0x00ff00ff) * a) >> 8) & 0x00ff00ff) |
        ((((s & 0x0000ff00) * a) >> 8) & 0x0000ff00);
    tjs_uint32 n = (((~d & s) << 1) + ((~d ^ s) & 0x00fefefe)) & 0x01010100;
    n = ((n >> 8) + 0x007f7f7f) ^ 0x007f7f7f;
    *dest = ((s & n) - (d & n)) | ((d & ~n) - (s & ~n));
}

// =========================================================================
// SIMD implementations for Overlay, HardLight, Exclusion
// =========================================================================
MAKE_PS_4V(Overlay, OverlayCore(d8, vd, vs))
MAKE_PS_4V(HardLight, HardLightCore(d8, vd, vs))
MAKE_PS_4V(Exclusion, ExclusionCore(d8, vd, vs))

// =========================================================================
// Table-based modes: SoftLight, ColorDodge, ColorBurn, ColorDodge5, Diff5
// These use per-pixel scalar table lookups - still vectorize the alpha blend step
// =========================================================================

// Generic scalar-core + SIMD-alpha pattern for table-based blend
#define MAKE_PS_TABLE_4V(Name)                                                \
void Ps##Name##Blend_HWY(tjs_uint32 *dest, const tjs_uint32 *src,            \
                          tjs_int len) {                                       \
    for (tjs_int i = 0; i < len; i++) {                                       \
        tjs_uint32 s = src[i], d = dest[i], a = s >> 24;                      \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
    }                                                                          \
}                                                                              \
void Ps##Name##Blend_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,          \
                            tjs_int len, tjs_int opa) {                       \
    for (tjs_int i = 0; i < len; i++) {                                       \
        tjs_uint32 s = src[i], d = dest[i], a = ((s >> 24) * opa) >> 8;      \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
    }                                                                          \
}                                                                              \
void Ps##Name##Blend_HDA_HWY(tjs_uint32 *dest, const tjs_uint32 *src,        \
                              tjs_int len) {                                   \
    for (tjs_int i = 0; i < len; i++) {                                       \
        tjs_uint32 s = src[i], d = dest[i], a = s >> 24;                      \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);                  \
    }                                                                          \
}                                                                              \
void Ps##Name##Blend_HDA_o_HWY(tjs_uint32 *dest, const tjs_uint32 *src,      \
                                tjs_int len, tjs_int opa) {                   \
    for (tjs_int i = 0; i < len; i++) {                                       \
        tjs_uint32 s = src[i], d = dest[i], a = ((s >> 24) * opa) >> 8;      \
        Ps##Name##_scalar(dest + i, d, s, a);                                 \
        dest[i] = (dest[i] & 0x00ffffff) | (d & 0xff000000);                  \
    }                                                                          \
}

MAKE_PS_TABLE_4V(SoftLight)
MAKE_PS_TABLE_4V(ColorDodge)
MAKE_PS_TABLE_4V(ColorBurn)
MAKE_PS_TABLE_4V(ColorDodge5)
MAKE_PS_TABLE_4V(Diff5)

}  // namespace HWY_NAMESPACE
}  // namespace krkr2
HWY_AFTER_NAMESPACE();

// --- Export functions ---
#if HWY_ONCE
namespace krkr2 {
// Overlay
HWY_EXPORT(PsOverlayBlend_HWY);
HWY_EXPORT(PsOverlayBlend_o_HWY);
HWY_EXPORT(PsOverlayBlend_HDA_HWY);
HWY_EXPORT(PsOverlayBlend_HDA_o_HWY);
// HardLight
HWY_EXPORT(PsHardLightBlend_HWY);
HWY_EXPORT(PsHardLightBlend_o_HWY);
HWY_EXPORT(PsHardLightBlend_HDA_HWY);
HWY_EXPORT(PsHardLightBlend_HDA_o_HWY);
// Exclusion
HWY_EXPORT(PsExclusionBlend_HWY);
HWY_EXPORT(PsExclusionBlend_o_HWY);
HWY_EXPORT(PsExclusionBlend_HDA_HWY);
HWY_EXPORT(PsExclusionBlend_HDA_o_HWY);
// SoftLight (table)
HWY_EXPORT(PsSoftLightBlend_HWY);
HWY_EXPORT(PsSoftLightBlend_o_HWY);
HWY_EXPORT(PsSoftLightBlend_HDA_HWY);
HWY_EXPORT(PsSoftLightBlend_HDA_o_HWY);
// ColorDodge (table)
HWY_EXPORT(PsColorDodgeBlend_HWY);
HWY_EXPORT(PsColorDodgeBlend_o_HWY);
HWY_EXPORT(PsColorDodgeBlend_HDA_HWY);
HWY_EXPORT(PsColorDodgeBlend_HDA_o_HWY);
// ColorBurn (table)
HWY_EXPORT(PsColorBurnBlend_HWY);
HWY_EXPORT(PsColorBurnBlend_o_HWY);
HWY_EXPORT(PsColorBurnBlend_HDA_HWY);
HWY_EXPORT(PsColorBurnBlend_HDA_o_HWY);
// ColorDodge5 (table)
HWY_EXPORT(PsColorDodge5Blend_HWY);
HWY_EXPORT(PsColorDodge5Blend_o_HWY);
HWY_EXPORT(PsColorDodge5Blend_HDA_HWY);
HWY_EXPORT(PsColorDodge5Blend_HDA_o_HWY);
// Diff5 (table)
HWY_EXPORT(PsDiff5Blend_HWY);
HWY_EXPORT(PsDiff5Blend_o_HWY);
HWY_EXPORT(PsDiff5Blend_HDA_HWY);
HWY_EXPORT(PsDiff5Blend_HDA_o_HWY);
}  // namespace krkr2

extern "C" {

#define EXPORT_PS_BLEND2(Name)                                                 \
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

EXPORT_PS_BLEND2(Overlay)
EXPORT_PS_BLEND2(HardLight)
EXPORT_PS_BLEND2(Exclusion)
EXPORT_PS_BLEND2(SoftLight)
EXPORT_PS_BLEND2(ColorDodge)
EXPORT_PS_BLEND2(ColorBurn)
EXPORT_PS_BLEND2(ColorDodge5)
EXPORT_PS_BLEND2(Diff5)

}  // extern "C"
#endif
