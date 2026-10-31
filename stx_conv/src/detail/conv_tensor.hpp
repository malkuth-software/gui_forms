#pragma once
#include "conv_simd.hpp"
namespace stx_conv::kernels::detail {
template<class Ops>
STX_CONV_INLINE void quintic_basis(typename Ops::Value t, std::array<typename Ops::Value,6>& b) noexcept {
    using V=typename Ops::Value;
    const V s=Ops::sub(Ops::set(1),t);
    const V t2=Ops::mul(t,t),t3=Ops::mul(t2,t);
    const V s2=Ops::mul(s,s),s3=Ops::mul(s2,s);
    b[0]=Ops::mul(s3,s2);
    b[1]=Ops::mul(Ops::mul(Ops::set(5),Ops::mul(s3,s)),t);
    b[2]=Ops::mul(Ops::mul(Ops::set(10),s3),t2);
    b[3]=Ops::mul(Ops::mul(Ops::set(10),s2),t3);
    b[4]=Ops::mul(Ops::mul(Ops::set(5),s),Ops::mul(t3,t));
    b[5]=Ops::mul(t3,t2);
}
template<class Ops,class Scalar>
STX_CONV_INLINE void tensor_group(const Scalar* const controls[36],const Scalar* u,
    const Scalar* v,Scalar* output,std::size_t lane) noexcept {
    using V=typename Ops::Value;
    std::array<V,6> bx{},by{};
    quintic_basis<Ops>(Ops::load(u+lane),bx);
    quintic_basis<Ops>(Ops::load(v+lane),by);
    V sum=Ops::set(0);
    for (std::size_t y=0;y<6;++y) {
        V row=Ops::set(0);
        for (std::size_t x=0;x<6;++x) {
            row=Ops::add(row,Ops::mul(bx[x],Ops::load(controls[y*6+x]+lane)));
        }
        sum=Ops::add(sum,Ops::mul(by[y],row));
    }
    Ops::store(output+lane,sum);
}
// Exact grid-boundary queries reduce to one six-control curve.
template<class Scalar>
void curve_rgba(const Scalar* origin,std::size_t control_stride,Scalar t,Scalar* output) noexcept {
    using Ops=NativeOps<Scalar>;
    using V=typename Ops::Value;
    std::array<Scalar,6> basis{};
    quintic_basis<ScalarOps<Scalar>>(t,basis);
    for (std::size_t channel=0;channel<4;channel+=Ops::width) {
        V sum=Ops::set(0);
        for (std::size_t i=0;i<6;++i) { sum=Ops::add(sum,Ops::mul(Ops::set(basis[i]),Ops::load(origin+i*control_stride+channel))); }
        Ops::store(output+channel,sum);
    }
}
template<class Scalar>
STX_CONV_INLINE void patch_full_rgba(const Scalar* origin,std::size_t row_stride,Scalar u,Scalar v,Scalar* output) noexcept {
    using Ops=NativeOps<Scalar>;
    using V=typename Ops::Value;
    std::array<Scalar,6> bx{},by{};
    quintic_basis<ScalarOps<Scalar>>(u,bx);quintic_basis<ScalarOps<Scalar>>(v,by);
    std::array<V,4/Ops::width> sums{};
    for (std::size_t c=0;c<sums.size();++c) { sums[c]=Ops::set(0); }
    for (std::size_t y=0;y<6;++y) {
        std::array<V,4/Ops::width> rows{};
        for (std::size_t c=0;c<rows.size();++c) { rows[c]=Ops::set(0); }
        for (std::size_t x=0;x<6;++x) {
            const V weight=Ops::set(bx[x]);
            const Scalar* control=origin+y*row_stride+x*4;
            for (std::size_t c=0;c<rows.size();++c) {
                rows[c]=Ops::add(rows[c],Ops::mul(weight,Ops::load(control+c*Ops::width)));
            }
        }
        const V weight=Ops::set(by[y]);
        for (std::size_t c=0;c<sums.size();++c) { sums[c]=Ops::add(sums[c],Ops::mul(weight,rows[c])); }
    }
    for (std::size_t c=0;c<sums.size();++c) { Ops::store(output+c*Ops::width,sums[c]); }
}
// One known atlas patch, RGBA contiguous at each control. The cell lookup and
// bounds checks belong to the caller; no 36-pointer gather table is built.
template<class Scalar>
void patch_rgba(const Scalar* origin,std::size_t row_stride,Scalar u,Scalar v,Scalar* output) noexcept {
    if (u==Scalar(0) || u==Scalar(1)) {
        const Scalar* column=origin+(u==Scalar(1) ? 20 : 0);
        if (v==Scalar(0) || v==Scalar(1)) {
            const Scalar* node=column+(v==Scalar(1) ? 5*row_stride : 0);
            std::copy_n(node,4,output);
        } else { curve_rgba(column,row_stride,v,output); }
        return;
    }
    if (v==Scalar(0) || v==Scalar(1)) {
        curve_rgba(origin+(v==Scalar(1) ? 5*row_stride : 0),4,u,output);
        return;
    }
    patch_full_rgba(origin,row_stride,u,v,output);
}

}
