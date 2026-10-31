#pragma once
#include "conv_simd.hpp"
namespace stx_conv::kernels::detail {
// Input slots: y0,y1,m0/5,2m0/5,q0/20,m1/5,2m1/5,q1/20.
template<class Ops,class Scalar>
STX_CONV_INLINE void raw_group(const Scalar* const input[8],Scalar* const output[5],std::size_t i) noexcept {
    using V=typename Ops::Value;
    const V y0=Ops::load(input[0]+i),y1=Ops::load(input[1]+i);
    const V c1=Ops::add(y0,Ops::load(input[2]+i));
    const V c2=Ops::add(Ops::add(y0,Ops::load(input[3]+i)),Ops::load(input[4]+i));
    const V c3=Ops::add(Ops::sub(y1,Ops::load(input[6]+i)),Ops::load(input[7]+i));
    const V c4=Ops::sub(y1,Ops::load(input[5]+i));
    Ops::store(output[0]+i,Ops::sub(c1,y0));Ops::store(output[1]+i,Ops::sub(c2,c1));
    Ops::store(output[2]+i,Ops::sub(c3,c2));Ops::store(output[3]+i,Ops::sub(c4,c3));
    Ops::store(output[4]+i,Ops::sub(y1,c4));
}
template<class Scalar>
void raw_lanes(const Scalar* const input[8],Scalar* const output[5],std::size_t first,std::size_t last) noexcept {
    using Ops=NativeOps<Scalar>;
    std::size_t i=first;
    for (;i+Ops::width<=last;i+=Ops::width) { raw_group<Ops>(input,output,i); }
    for (;i<last;++i) { raw_group<ScalarOps<Scalar>>(input,output,i); }
}
}
