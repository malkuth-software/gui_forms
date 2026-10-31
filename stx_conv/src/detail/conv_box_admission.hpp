#pragma once
#include "conv_simd.hpp"
namespace stx_conv::kernels::detail {
// Five capacities: for a fixed subset size, the smallest-capacity subset
// maximizes the threshold form. A fixed min/max network replaces 31 forms
// by five prefix forms. It selects no data-dependent control path.
template<class Ops>
void order_pair(typename Ops::Value& a, typename Ops::Value& b) noexcept {
    const typename Ops::Value lo = Ops::min(a, b);
    const typename Ops::Value hi = Ops::max(a, b);
    a = lo; b = hi;
}
template<class Ops>
typename Ops::Value capacity_threshold(std::array<typename Ops::Value, 5> w, typename Ops::Value mass) noexcept {
    using V = typename Ops::Value;
    order_pair<Ops>(w[0], w[1]); order_pair<Ops>(w[3], w[4]);
    order_pair<Ops>(w[2], w[4]); order_pair<Ops>(w[2], w[3]);
    order_pair<Ops>(w[1], w[4]); order_pair<Ops>(w[0], w[3]);
    order_pair<Ops>(w[0], w[2]); order_pair<Ops>(w[1], w[3]); order_pair<Ops>(w[1], w[2]);
    V result = Ops::div(mass, Ops::set(5));
    V sum = w[0];
    result = Ops::max(result, Ops::div(Ops::sub(mass, sum), Ops::set(4)));
    sum = Ops::add(sum, w[1]);
    result = Ops::max(result, Ops::div(Ops::sub(mass, sum), Ops::set(3)));
    sum = Ops::add(sum, w[2]);
    result = Ops::max(result, Ops::div(Ops::sub(mass, sum), Ops::set(2)));
    sum = Ops::add(sum, w[3]);
    result = Ops::max(result, Ops::sub(mass, sum));
    return result;
}
// Each vector lane is a distinct line/channel. Array slots are polynomial
// currents, not SIMD lanes. Only the two neighbouring scalar terms survive
// to the next cell. All pointers borrow disjoint stream-owned lane arrays.
template<class Ops, class Scalar>
STX_CONV_INLINE void admit_vector(const Scalar* const raw[5], Scalar* const output[5], const Scalar* y0, const Scalar* y1,
                  const Scalar* y2, const Scalar* next, Scalar* left_delta, Scalar* left_last, std::size_t i) noexcept {
    using V = typename Ops::Value;
    const V zero = Ops::set(0);
    std::array<V, 5> a{}, p{}, n{};
    for (std::size_t j = 0; j < 5; ++j) { a[j] = Ops::load(raw[j] + i); }
    const V delta = Ops::sub(Ops::load(y1 + i), Ops::load(y0 + i));
    const V following_delta = Ops::sub(Ops::load(y2 + i), Ops::load(y1 + i));
    const V preceding_delta = Ops::load(left_delta + i);
    const V preceding_last = Ops::load(left_last + i);
    const V following_first = Ops::load(next + i);
    V hp = Ops::min(Ops::max(preceding_delta, zero), Ops::max(Ops::sub(Ops::max(a[0], zero), Ops::max(Ops::sub(zero, preceding_last), zero)), zero));
    V hn = Ops::min(Ops::max(Ops::sub(zero, preceding_delta), zero), Ops::max(Ops::sub(Ops::max(Ops::sub(zero, a[0]), zero), Ops::max(preceding_last, zero)), zero));
    for (std::size_t j = 0; j < 5; ++j) {
        hp = Ops::min(hp, Ops::max(a[j], zero));
        hn = Ops::min(hn, Ops::max(Ops::sub(zero, a[j]), zero));
        p[j] = hp; n[j] = hn;
    }
    V tp = Ops::min(Ops::max(following_delta, zero), Ops::max(Ops::sub(Ops::max(a[4], zero), Ops::max(Ops::sub(zero, following_first), zero)), zero));
    V tn = Ops::min(Ops::max(Ops::sub(zero, following_delta), zero), Ops::max(Ops::sub(Ops::max(Ops::sub(zero, a[4]), zero), Ops::max(following_first, zero)), zero));
    for (std::size_t j = 5; j != 0; --j) {
        tp = Ops::min(tp, Ops::max(a[j - 1], zero));
        tn = Ops::min(tn, Ops::max(Ops::sub(zero, a[j - 1]), zero));
        p[j - 1] = Ops::add(p[j - 1], tp); n[j - 1] = Ops::add(n[j - 1], tn);
    }
    V positive_room = zero, negative_room = zero;
    for (std::size_t j = 0; j < 5; ++j) {
        positive_room = Ops::add(positive_room, p[j]); negative_room = Ops::add(negative_room, n[j]);
    }
    const V plus = Ops::max(delta, zero), minus = Ops::max(Ops::sub(zero, delta), zero);
    const V ub = Ops::add(plus, Ops::min(negative_room, Ops::mul(Ops::set(4), plus)));
    const V lb = Ops::add(minus, Ops::min(positive_room, Ops::mul(Ops::set(4), minus)));
    Ops::store(left_delta + i, delta); Ops::store(left_last + i, a[4]);
    V sum = zero;
    for (std::size_t j = 0; j < 5; ++j) {
        const V hi = Ops::add(p[j], ub), lo = Ops::sub(zero, Ops::add(n[j], lb));
        a[j] = Ops::min(hi, Ops::max(lo, a[j]));
        p[j] = Ops::sub(a[j], lo); n[j] = Ops::sub(hi, a[j]);
        sum = Ops::add(sum, a[j]);
    }
    const V residual = Ops::sub(sum, delta);
    // Only one correction direction is active per lane. Retain the selected
    // capacities in their original slots; threshold sorting works on a copy.
    for (std::size_t j = 0; j < 5; ++j) {
        p[j] = Ops::choose_negative(residual, n[j], p[j]);
    }
    const V mass = Ops::max(residual, Ops::sub(zero, residual));
    const V threshold = capacity_threshold<Ops>(p, mass);
    for (std::size_t j = 0; j < 5; ++j) {
        const V correction = Ops::min(threshold, p[j]);
        const V down = Ops::choose_negative(residual, zero, correction);
        const V up = Ops::choose_negative(residual, correction, zero);
        const V result = Ops::add(Ops::sub(a[j], down), up);
        Ops::store(output[j] + i, result);
    }
}
}
