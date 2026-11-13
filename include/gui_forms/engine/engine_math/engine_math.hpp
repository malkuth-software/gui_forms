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
// - engine_arctangent2: absolute error at most 2^-48.
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

} // namespace gui_forms
