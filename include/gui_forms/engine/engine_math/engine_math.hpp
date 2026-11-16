#pragma once

#include "gui_forms/export/application_export.hpp"

namespace gui_forms {

// Deterministic elementary functions for engine kernels (T2 plan, "Numerics
// and identical pixels"). The host libraries' pow, exp and log differ in
// their last bits between macOS, glibc and the Windows runtime, so an engine
// kernel never calls them. These are built only from + − × ÷ and sqrt, which
// IEEE 754 rounds exactly, and frexp and ldexp, which are exact, and are
// compiled without fused multiply-add. The same inputs give the same bits on
// every host.
//
// Error bounds, against the exact mathematical value:
// - engine_logarithm: absolute error at most 2^-50 · (1 + |ln x|);
// - engine_exponential: relative error at most 2^-49 for results from
//   2^-1000 to 2^1000;
// - engine_power: relative error at most 2^-49 · (1 + |exponent · ln base|)
//   for results in the same range;
// - engine_sine_cosine: absolute error at most 2^-50 for |x| ≤ 2^20;
// - engine_arctangent2: absolute error at most 2^-48;
// - engine_sine_cosine_degrees: absolute error at most 2^-49, and exact at
//   every multiple of 90 degrees;
// - engine_tangent: absolute error at most 2^-48 for |x| ≤ π/4;
// - engine_hypotenuse: relative error at most 2^-52 (two roundings).
// The tests check them against the host library with a margin.

// Natural logarithm of `x`. Requires x > 0 and finite; returns NaN for
// x < 0 or NaN, −infinity for 0, and +infinity for +infinity.
[[nodiscard]] GUI_FORMS_API double engine_logarithm(double x) noexcept;

// e raised to `x`. Returns 0 below about −745, +infinity above about 709.8,
// and NaN for NaN.
[[nodiscard]] GUI_FORMS_API double engine_exponential(double x) noexcept;

// `base` raised to `exponent`, for base ≥ 0. Exact cases: base^0 is 1,
// 1^exponent is 1, and 0^exponent is 0 for exponent > 0. Returns NaN for a
// negative or NaN base and for a NaN exponent.
[[nodiscard]] GUI_FORMS_API double engine_power(double base, double exponent) noexcept;

// The sine and cosine of `x` radians, computed together (E3's springs). For
// x of magnitude at most 2^20 the absolute error of each is at most 2^-50; beyond that,
// and for infinities and NaN, both are NaN. Exact case: x = 0 gives sine 0
// and cosine 1. The argument is reduced by the nearest multiple of π/2,
// with π/2 split in three parts (Cody and Waite) so that the reduction is
// exact to about 2^-100 relative.
struct GUI_FORMS_API EngineSineCosine final {
    double sine{};
    double cosine{};
};
[[nodiscard]] GUI_FORMS_API EngineSineCosine engine_sine_cosine(double x) noexcept;

// The angle of the point (x, y) from the positive x axis, in radians in
// [−π, π], as std::atan2(y, x) (E9.6's conic gradient). Exact cases: (0, 0)
// gives 0, and a point on an axis gives 0, ±π/2 or π. NaN in either
// argument gives NaN. The ratio of the smaller to the larger magnitude is
// reduced twice (by π/4 and by the half-angle formula) to at most 0.2,
// where a 13-term series converges past 2^-60.
[[nodiscard]] GUI_FORMS_API double engine_arctangent2(double y, double x) noexcept;

// The sine and cosine of `degrees`, for every finite angle (the theme
// language's rotate() and arc rotations, drawing replay's gradient
// directions). Exact at every multiple of 90 degrees: 0, 90, 180 and 270
// give (0, 1), (1, 0), (0, −1) and (−1, 0) with no rounding. The angle is
// reduced exactly (fmod by 360, then the nearest multiple of 90, both
// without rounding), the remainder of at most 45 degrees is turned into
// radians (one rounding) and passed to engine_sine_cosine, and the quarter
// turns are applied by swapping and negating. Absolute error at most
// 2^-49. Infinities and NaN give NaN in both.
//
// This and the radian functions above are the only trigonometry the raster
// and theme paths call, so a later move to STXlib's certified kernels
// (stxlib/math.hpp's sincos and atan2, once gui_forms_core links
// stxlib::algorithms at a pin that carries them) changes their bodies only;
// STXlib's glyph seat needs the same exact quarter turns for rotated text.
[[nodiscard]] GUI_FORMS_API EngineSineCosine engine_sine_cosine_degrees(double degrees) noexcept;

// The tangent of `x` radians, as engine_sine_cosine's sine over its cosine
// (the theme language's arc handles, 4/3 tan(piece/4) with |piece/4| at most
// π/8). For |x| ≤ π/4 the cosine is at least 0.7071, so the absolute error
// is at most 2^-48; beyond that it grows as 1/cos²(x). Exact case: 0 gives
// 0. Outside engine_sine_cosine's range, infinities and NaN give NaN.
[[nodiscard]] GUI_FORMS_API double engine_tangent(double x) noexcept;

// The length of (x, y), as sqrt(x² + y²): each square, the sum and the
// root round once, so the result has the same bits on every host, where
// the host's hypot need not. Relative error at most 2^-52 (about one unit
// in the last place). No scaling: the squares overflow for magnitudes above
// about 1e154 and lose precision below about 1e-154, far outside the
// coordinates and colours this serves. NaN in either gives NaN.
[[nodiscard]] GUI_FORMS_API double engine_hypotenuse(double x, double y) noexcept;

} // namespace gui_forms
