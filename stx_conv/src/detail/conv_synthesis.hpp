#pragma once
#include "conv_simd.hpp"
namespace stx_conv::kernels::detail {
template<class Ops,class Scalar,bool Bubble,bool Accumulate>
STX_CONV_INLINE void synthesis_group(const Scalar* anchors,const Scalar* currents,std::size_t stride,
    const std::array<Scalar,5>& weights,Scalar anchor_weight,const Scalar* bubble,Scalar bubble_weight,
    Scalar* output,std::size_t i) noexcept {
    using V=typename Ops::Value;
    V value=Ops::mul(Ops::set(anchor_weight),Ops::load(anchors+i));
    for (std::size_t j=0;j<5;++j) {
        value=Ops::add(value,Ops::mul(Ops::set(weights[j]),Ops::load(currents+j*stride+i)));
    }
    if constexpr (Bubble) { value=Ops::add(value,Ops::mul(Ops::set(bubble_weight),Ops::load(bubble+i))); }
    if constexpr (Accumulate) { value=Ops::add(Ops::load(output+i),value); }
    Ops::store(output+i,value);
}
template<class Scalar,bool Bubble,bool Accumulate>
void synthesis_lanes(const Scalar* anchors,const Scalar* currents,std::size_t stride,
    const std::array<Scalar,5>& weights,Scalar anchor_weight,const Scalar* bubble,Scalar bubble_weight,
    Scalar* output,std::size_t first,std::size_t last) noexcept {
    using Ops=NativeOps<Scalar>;
    std::size_t i=first;
    for (;i+Ops::width<=last;i+=Ops::width) { synthesis_group<Ops,Scalar,Bubble,Accumulate>(anchors,currents,stride,weights,anchor_weight,bubble,bubble_weight,output,i); }
    for (;i<last;++i) { synthesis_group<ScalarOps<Scalar>,Scalar,Bubble,Accumulate>(anchors,currents,stride,weights,anchor_weight,bubble,bubble_weight,output,i); }
}
}
