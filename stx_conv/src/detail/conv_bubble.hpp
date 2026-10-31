#pragma once
#include "conv_simd.hpp"
#include <limits>
namespace stx_conv::kernels::detail {
// Scale the bubble amplitude by 3/5. Mass equality eliminates the outer
// raw-minus-admitted terms, and the sign tests require no divisions.
template<class Ops, class Scalar, bool Interior>
STX_CONV_INLINE void bubble_group(const Scalar* raw,
    const Scalar* c, Scalar* output, std::size_t stride, std::size_t lane) noexcept {
    using V = typename Ops::Value;
    const V zero = Ops::set(0);
    const V infinity = Ops::set(std::numeric_limits<Scalar>::infinity());
    const V negative_infinity = Ops::sub(zero, infinity);
    V w = zero;
    if constexpr (Interior) {
        const V outer = Ops::add(Ops::load(raw+lane),Ops::load(raw+4*stride+lane));
        const V inner = Ops::add(Ops::load(raw+stride+lane),Ops::load(raw+3*stride+lane));
        const V fourth = Ops::add(Ops::sub(outer,Ops::mul(Ops::set(4),inner)),Ops::mul(Ops::set(6),Ops::load(raw+2*stride+lane)));
        w = Ops::mul(Ops::set(Scalar(-492.0/125.0)),fourth);
    }
    const V c0=Ops::load(c+lane),c1=Ops::load(c+stride+lane),c2=Ops::load(c+2*stride+lane);
    const V c3=Ops::load(c+3*stride+lane),c4=Ops::load(c+4*stride+lane);
    const V d1=Ops::sub(Ops::load(raw+stride+lane),c1);
    const V d2=Ops::sub(Ops::load(raw+2*stride+lane),c2);
    const V d3=Ops::sub(Ops::load(raw+3*stride+lane),c3);
    w=Ops::add(w,Ops::mul(Ops::set(31.5),Ops::add(d1,d3)));
    w=Ops::add(w,Ops::mul(Ops::set(46.5),d2));
    const V q0=Ops::add(Ops::add(c0,Ops::mul(Ops::set(8),c1)),Ops::mul(Ops::set(6),c2));
    const V q1=Ops::mul(Ops::set(-1.5),Ops::add(Ops::add(c1,Ops::mul(Ops::set(3),c2)),c3));
    const V q2=Ops::add(Ops::add(Ops::mul(Ops::set(6),c2),Ops::mul(Ops::set(8),c3)),c4);
    const V q[3]={q0,q1,q2};
    V lower=negative_infinity,upper=infinity;
    for (std::size_t j=0;j<3;++j) {
        lower=Ops::max(lower,Ops::choose_negative(q[j],q[j],negative_infinity));
        upper=Ops::min(upper,Ops::choose_negative(Ops::sub(zero,q[j]),q[j],infinity));
    }
    w=Ops::min(upper,Ops::max(lower,w));
    for (std::size_t j=0;j<3;++j) {
        const V negative_absolute=Ops::sub(zero,Ops::max(q[j],Ops::sub(zero,q[j])));
        w=Ops::choose_negative(negative_absolute,w,zero);
    }
    Ops::store(output+lane,w);
}
template<class Scalar, bool Interior>
void bubble_lanes(const Scalar* raw,const Scalar* currents,
    Scalar* output,std::size_t stride,std::size_t first,std::size_t last) noexcept {
    using Ops=NativeOps<Scalar>;
    std::size_t lane=first;
    for (;lane+Ops::width<=last;lane+=Ops::width) {
        bubble_group<Ops,Scalar,Interior>(raw,currents,output,stride,lane);
    }
    for (;lane<last;++lane) {
        bubble_group<ScalarOps<Scalar>,Scalar,Interior>(raw,currents,output,stride,lane);
    }
}
}
