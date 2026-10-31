#pragma once
#include "conv_simd.hpp"
namespace stx_conv::kernels::detail {
template<class Ops,class Scalar,bool Affine>
STX_CONV_INLINE void mapping_group(const std::array<Scalar,9>& h,const Scalar* x,const Scalar* y,
    Scalar* sx,Scalar* sy,std::size_t i) noexcept {
    using V=typename Ops::Value;
    const V px=Ops::load(x+i),py=Ops::load(y+i);
    V a=Ops::add(Ops::add(Ops::mul(Ops::set(h[0]),px),Ops::mul(Ops::set(h[1]),py)),Ops::set(h[2]));
    V b=Ops::add(Ops::add(Ops::mul(Ops::set(h[3]),px),Ops::mul(Ops::set(h[4]),py)),Ops::set(h[5]));
    if constexpr (!Affine) {
        const V d=Ops::add(Ops::add(Ops::mul(Ops::set(h[6]),px),Ops::mul(Ops::set(h[7]),py)),Ops::set(h[8]));
        a=Ops::div(a,d);b=Ops::div(b,d);
    }
    Ops::store(sx+i,a);Ops::store(sy+i,b);
}
template<class Scalar,bool Affine>
void mapping_lanes(const std::array<Scalar,9>& h,const Scalar* x,const Scalar* y,
    Scalar* sx,Scalar* sy,std::size_t count) noexcept {
    using Ops=NativeOps<Scalar>;
    std::size_t i=0;
    for (;i+Ops::width<=count;i+=Ops::width) { mapping_group<Ops,Scalar,Affine>(h,x,y,sx,sy,i); }
    for (;i<count;++i) { mapping_group<ScalarOps<Scalar>,Scalar,Affine>(h,x,y,sx,sy,i); }
}
}
