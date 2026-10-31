#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#if defined(__aarch64__)
#include <arm_neon.h>
#define STX_CONV_NEON 1
#elif (defined(__SSE2__) || defined(_M_X64))
#include <emmintrin.h>
#define STX_CONV_SSE2 1
#endif
#if defined(__clang__) || defined(__GNUC__)
#define STX_CONV_INLINE inline __attribute__((always_inline))
#else
#define STX_CONV_INLINE inline
#endif
namespace stx_conv::kernels::detail {
template<class Scalar>
struct ScalarOps {
    using Value = Scalar;
    static constexpr std::size_t width = 1;
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept { return sign < Scalar(0) ? negative : positive; }
    static Value load(const Scalar* p) noexcept { return *p; }
    static void store(Scalar* p, Value x) noexcept { *p = x; }
    static Value set(Scalar x) noexcept { return x; }
    static Value add(Value a, Value b) noexcept { const Value r = a + b; return r; }
    static Value sub(Value a, Value b) noexcept { const Value r = a - b; return r; }
    static Value mul(Value a, Value b) noexcept { const Value r = a * b; return r; }
    static Value div(Value a, Value b) noexcept { const Value r = a / b; return r; }
    static Value min(Value a, Value b) noexcept { const Value r = std::fmin(a, b); return r; }
    static Value max(Value a, Value b) noexcept { const Value r = std::fmax(a, b); return r; }
};
template<class Scalar> struct HardwareOps : ScalarOps<Scalar> {};
#ifdef STX_CONV_NEON
template<> struct HardwareOps<float> {
    using Value = float32x4_t;
    static constexpr std::size_t width = 4;
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept { const Value r = vbslq_f32(vcltq_f32(sign, vdupq_n_f32(0)), negative, positive); return r; }
    static Value load(const float* p) noexcept { const Value r = vld1q_f32(p); return r; }
    static void store(float* p, Value x) noexcept { vst1q_f32(p, x); }
    static Value set(float x) noexcept { const Value r = vdupq_n_f32(x); return r; }
    static Value add(Value a, Value b) noexcept { const Value r = vaddq_f32(a, b); return r; }
    static Value sub(Value a, Value b) noexcept { const Value r = vsubq_f32(a, b); return r; }
    static Value mul(Value a, Value b) noexcept { const Value r = vmulq_f32(a, b); return r; }
    static Value div(Value a, Value b) noexcept { const Value r = vdivq_f32(a, b); return r; }
    static Value min(Value a, Value b) noexcept { const Value r = vminq_f32(a, b); return r; }
    static Value max(Value a, Value b) noexcept { const Value r = vmaxq_f32(a, b); return r; }
};
template<> struct HardwareOps<double> {
    using Value = float64x2_t;
    static constexpr std::size_t width = 2;
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept { const Value r = vbslq_f64(vcltq_f64(sign, vdupq_n_f64(0)), negative, positive); return r; }
    static Value load(const double* p) noexcept { const Value r = vld1q_f64(p); return r; }
    static void store(double* p, Value x) noexcept { vst1q_f64(p, x); }
    static Value set(double x) noexcept { const Value r = vdupq_n_f64(x); return r; }
    static Value add(Value a, Value b) noexcept { const Value r = vaddq_f64(a, b); return r; }
    static Value sub(Value a, Value b) noexcept { const Value r = vsubq_f64(a, b); return r; }
    static Value mul(Value a, Value b) noexcept { const Value r = vmulq_f64(a, b); return r; }
    static Value div(Value a, Value b) noexcept { const Value r = vdivq_f64(a, b); return r; }
    static Value min(Value a, Value b) noexcept { const Value r = vminq_f64(a, b); return r; }
    static Value max(Value a, Value b) noexcept { const Value r = vmaxq_f64(a, b); return r; }
};
#elif defined(STX_CONV_SSE2)
template<> struct HardwareOps<float> {
    using Value = __m128;
    static constexpr std::size_t width = 4;
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const Value mask = _mm_cmplt_ps(sign, _mm_setzero_ps());
        const Value r = _mm_or_ps(_mm_and_ps(mask, negative), _mm_andnot_ps(mask, positive));
        return r;
    }
    static Value load(const float* p) noexcept { const Value r = _mm_loadu_ps(p); return r; }
    static void store(float* p, Value x) noexcept { _mm_storeu_ps(p, x); }
    static Value set(float x) noexcept { const Value r = _mm_set1_ps(x); return r; }
    static Value add(Value a, Value b) noexcept { const Value r = _mm_add_ps(a, b); return r; }
    static Value sub(Value a, Value b) noexcept { const Value r = _mm_sub_ps(a, b); return r; }
    static Value mul(Value a, Value b) noexcept { const Value r = _mm_mul_ps(a, b); return r; }
    static Value div(Value a, Value b) noexcept { const Value r = _mm_div_ps(a, b); return r; }
    static Value min(Value a, Value b) noexcept { const Value r = _mm_min_ps(a, b); return r; }
    static Value max(Value a, Value b) noexcept { const Value r = _mm_max_ps(a, b); return r; }
};
template<> struct HardwareOps<double> {
    using Value = __m128d;
    static constexpr std::size_t width = 2;
    static Value choose_negative(Value sign, Value negative, Value positive) noexcept {
        const Value mask = _mm_cmplt_pd(sign, _mm_setzero_pd());
        const Value r = _mm_or_pd(_mm_and_pd(mask, negative), _mm_andnot_pd(mask, positive));
        return r;
    }
    static Value load(const double* p) noexcept { const Value r = _mm_loadu_pd(p); return r; }
    static void store(double* p, Value x) noexcept { _mm_storeu_pd(p, x); }
    static Value set(double x) noexcept { const Value r = _mm_set1_pd(x); return r; }
    static Value add(Value a, Value b) noexcept { const Value r = _mm_add_pd(a, b); return r; }
    static Value sub(Value a, Value b) noexcept { const Value r = _mm_sub_pd(a, b); return r; }
    static Value mul(Value a, Value b) noexcept { const Value r = _mm_mul_pd(a, b); return r; }
    static Value div(Value a, Value b) noexcept { const Value r = _mm_div_pd(a, b); return r; }
    static Value min(Value a, Value b) noexcept { const Value r = _mm_min_pd(a, b); return r; }
    static Value max(Value a, Value b) noexcept { const Value r = _mm_max_pd(a, b); return r; }
};

#endif
#ifdef STX_CONV_FORCE_SCALAR
template<class Scalar> using NativeOps = ScalarOps<Scalar>;
#else
template<class Scalar> using NativeOps = HardwareOps<Scalar>;
#endif

}
