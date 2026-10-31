#pragma once
#include "conv_simd.hpp"
namespace stx_conv::kernels::detail {
// Five source streams; outputs are first/5, 2*first/5, second/20.
template<class Ops,class Scalar>
STX_CONV_INLINE void jet_group(const Scalar* const source[5],Scalar* fifths,
    Scalar* two_fifths,Scalar* twentieths,std::size_t i) noexcept {
    using V=typename Ops::Value;
    const V m2=Ops::load(source[0]+i),m1=Ops::load(source[1]+i),c=Ops::load(source[2]+i);
    const V p1=Ops::load(source[3]+i),p2=Ops::load(source[4]+i);
    V first=Ops::sub(m2,Ops::mul(Ops::set(8),m1));
    first=Ops::add(first,Ops::mul(Ops::set(8),p1));
    first=Ops::div(Ops::sub(first,p2),Ops::set(12));
    V second=Ops::sub(Ops::mul(Ops::set(16),p1),p2);
    second=Ops::sub(second,Ops::mul(Ops::set(30),c));
    second=Ops::add(second,Ops::mul(Ops::set(16),m1));
    second=Ops::div(Ops::sub(second,m2),Ops::set(12));
    Ops::store(fifths+i,Ops::div(first,Ops::set(5)));
    Ops::store(two_fifths+i,Ops::div(Ops::mul(Ops::set(2),first),Ops::set(5)));
    Ops::store(twentieths+i,Ops::div(second,Ops::set(20)));
}
template<class Scalar>
void jet_lanes(const Scalar* const source[5],Scalar* fifths,Scalar* two_fifths,
    Scalar* twentieths,std::size_t first,std::size_t last) noexcept {
    using Ops=NativeOps<Scalar>;
    std::size_t i=first;
    for (;i+Ops::width<=last;i+=Ops::width) { jet_group<Ops>(source,fifths,two_fifths,twentieths,i); }
    for (;i<last;++i) { jet_group<ScalarOps<Scalar>>(source,fifths,two_fifths,twentieths,i); }
}
}
