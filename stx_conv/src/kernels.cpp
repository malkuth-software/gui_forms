#include <stx_conv/kernels.hpp>
#include "detail/conv_box_admission.hpp"
#include "detail/conv_bubble.hpp"
#include "detail/conv_tensor.hpp"
#include "detail/conv_jets.hpp"
#include "detail/conv_raw.hpp"
#include "detail/conv_mapping.hpp"
#include "detail/conv_synthesis.hpp"
#include "internal.hpp"
#include <algorithm>
#include <cmath>

namespace stx_conv::kernels {
const char* backend() noexcept {
#if defined(STX_CONV_FORCE_SCALAR)
    return "scalar";
#elif defined(STX_CONV_NEON)
    return "neon-128";
#elif defined(STX_CONV_SSE2)
    return "sse2-128";
#else
    return "scalar";
#endif
}
template<Precision Scalar>
void capacity(const Scalar* const capacities[5],const Scalar* mass,Scalar* output,std::size_t count) noexcept {
    using Ops=detail::NativeOps<Scalar>;
    std::size_t i=0;
    for (;i+Ops::width<=count;i+=Ops::width) {
        std::array<typename Ops::Value,5> c{};
        for (std::size_t j=0;j<5;++j) { c[j]=Ops::load(capacities[j]+i); }
        Ops::store(output+i,detail::capacity_threshold<Ops>(c,Ops::load(mass+i)));
    }
    for (;i<count;++i) {
        std::array<Scalar,5> c{};
        for (std::size_t j=0;j<5;++j) { c[j]=capacities[j][i]; }
        output[i]=detail::capacity_threshold<detail::ScalarOps<Scalar>>(c,mass[i]);
    }
}
template<Precision Scalar>
void jet_terms(const Scalar* const source[5],Scalar* fifths,Scalar* two_fifths,Scalar* twentieths,std::size_t count) noexcept {
    detail::jet_lanes(source,fifths,two_fifths,twentieths,0,count);
}
template<Precision Scalar>
void raw_currents(const Scalar* const input[8],Scalar* const output[5],std::size_t count) noexcept {
    detail::raw_lanes(input,output,0,count);
}
template<Precision Scalar>
void bubble(const Scalar* raw,const Scalar* admitted,Scalar* output,std::size_t stride,std::size_t count,bool interior) noexcept {
    if (interior) { detail::bubble_lanes<Scalar,true>(raw,admitted,output,stride,0,count); }
    else { detail::bubble_lanes<Scalar,false>(raw,admitted,output,stride,0,count); }
}
template<Precision Scalar>
void admit_cell(const Scalar* const raw[5],Scalar* const output[5],const Scalar* y0,
    const Scalar* y1,const Scalar* y2,const Scalar* next,Scalar* left_delta,Scalar* left_last,std::size_t count) noexcept {
    using Ops=detail::NativeOps<Scalar>;
    std::size_t i=0;
    for (;i+Ops::width<=count;i+=Ops::width) { detail::admit_vector<Ops>(raw,output,y0,y1,y2,next,left_delta,left_last,i); }
    for (;i<count;++i) { detail::admit_vector<detail::ScalarOps<Scalar>>(raw,output,y0,y1,y2,next,left_delta,left_last,i); }
}
template<Precision Scalar>
void synthesize(const Scalar* anchors,const Scalar* currents,std::size_t stride,
    const std::array<Scalar,5>& weights,Scalar anchor_weight,const Scalar* bubble,
    Scalar bubble_weight,Scalar* output,std::size_t count,bool accumulate) noexcept {
    if (bubble_weight==Scalar(0)) {
        if (accumulate) { detail::synthesis_lanes<Scalar,false,true>(anchors,currents,stride,weights,anchor_weight,bubble,bubble_weight,output,0,count); }
        else { detail::synthesis_lanes<Scalar,false,false>(anchors,currents,stride,weights,anchor_weight,bubble,bubble_weight,output,0,count); }
    } else {
        if (accumulate) { detail::synthesis_lanes<Scalar,true,true>(anchors,currents,stride,weights,anchor_weight,bubble,bubble_weight,output,0,count); }
        else { detail::synthesis_lanes<Scalar,true,false>(anchors,currents,stride,weights,anchor_weight,bubble,bubble_weight,output,0,count); }
    }
}
template<Precision Scalar>
void tensor_quintic(const Scalar* const controls[36],const Scalar* u,const Scalar* v,Scalar* output,std::size_t count) noexcept {
    using Ops=detail::NativeOps<Scalar>;
    std::size_t i=0;
    for (;i+Ops::width<=count;i+=Ops::width) { detail::tensor_group<Ops>(controls,u,v,output,i); }
    for (;i<count;++i) { detail::tensor_group<detail::ScalarOps<Scalar>>(controls,u,v,output,i); }
}
template<Precision Scalar>
void tensor_patch_rgba(const Scalar* origin,std::size_t row_stride,Scalar u,Scalar v,Scalar* output) noexcept {
    detail::patch_rgba(origin,row_stride,u,v,output);
}
template<Precision Scalar>
void integrate_patch_rgba(const Scalar* origin,std::size_t row_stride,const Scalar* u,
    const Scalar* v,const Scalar* weights,std::size_t count,Scalar* sums) noexcept {
    if (count==0) { return; }
    using Ops=detail::NativeOps<Scalar>;
    using V=typename Ops::Value;
    std::array<V,4/Ops::width> accum{};
    for (std::size_t c=0;c<4;c+=Ops::width) { accum[c/Ops::width]=Ops::load(sums+c); }
    for (std::size_t i=0;i<count;++i) {
        Scalar sample[4]={};
        detail::patch_full_rgba(origin,row_stride,u[i],v[i],sample);
        const V alpha=Ops::set(std::clamp(sample[3],Scalar(0),Scalar(1)));
        const V weight=Ops::set(weights[i]);
        for (std::size_t c=0;c<4;c+=Ops::width) {
            const V value=Ops::min(alpha,Ops::max(Ops::load(sample+c),Ops::set(0)));
            accum[c/Ops::width]=Ops::add(accum[c/Ops::width],Ops::mul(weight,value));
        }
    }
    for (std::size_t c=0;c<4;c+=Ops::width) { Ops::store(sums+c,accum[c/Ops::width]); }
}
namespace {
template<class Scalar>
bool aliases(std::span<const Scalar> a,std::span<Scalar> b) noexcept {
    return stx_conv::detail::overlaps(a,b);
}
}
template<Precision Scalar>
Status map_points(const std::array<Scalar,9>& h,std::span<const Scalar> x,std::span<const Scalar> y,
    std::span<Scalar> sx,std::span<Scalar> sy) noexcept {
    if (y.size()!=x.size() || sx.size()<x.size() || sy.size()<x.size()) { return Status::insufficient_storage; }
    if (aliases(std::span<const Scalar>(h),sx)||aliases(std::span<const Scalar>(h),sy)||aliases(x,sx)||aliases(x,sy)||aliases(y,sx)||aliases(y,sy)||aliases(std::span<const Scalar>(sx),sy)) { return Status::overlap; }
    for (const Scalar v:h) { if (!std::isfinite(v)) { return Status::nonfinite_input; } }
    for (std::size_t i=0;i<x.size();++i) {
        if (!std::isfinite(x[i])||!std::isfinite(y[i])) { return Status::nonfinite_input; }
        const Scalar d=h[6]*x[i]+h[7]*y[i]+h[8];
        if (d==Scalar(0)) { return Status::invalid_shape; }
        if (!std::isfinite(d)) { return Status::numerical_failure; }
        const Scalar a=(h[0]*x[i]+h[1]*y[i]+h[2])/d;
        const Scalar b=(h[3]*x[i]+h[4]*y[i]+h[5])/d;
        if (!std::isfinite(a)||!std::isfinite(b)) { return Status::numerical_failure; }
    }
    if (h[6]==Scalar(0) && h[7]==Scalar(0) && h[8]==Scalar(1)) {
        detail::mapping_lanes<Scalar,true>(h,x.data(),y.data(),sx.data(),sy.data(),x.size());
    } else {
        detail::mapping_lanes<Scalar,false>(h,x.data(),y.data(),sx.data(),sy.data(),x.size());
    }
    return Status::ok;
}
template<Precision Scalar>
Status sample_rgba(TensorAtlasView<Scalar> atlas,std::span<const Scalar> sx,std::span<const Scalar> sy,std::span<Scalar> rgba,ColorProjection projection) noexcept {
    if (projection!=ColorProjection::none && projection!=ColorProjection::premultiplied) { return Status::invalid_shape; }
    constexpr std::size_t limit=1048576;
    if (atlas.source_width<2 || atlas.source_height<2 || atlas.source_width>limit || atlas.source_height>limit) { return Status::invalid_shape; }
    const std::size_t width=5*(atlas.source_width-1)+1,height=5*(atlas.source_height-1)+1;
    if (sx.size()>std::numeric_limits<std::size_t>::max()/4 || sy.size()!=sx.size() || rgba.size()<4*sx.size()
        || !stx_conv::detail::fits(height,atlas.lattice_row_stride,width*4,atlas.controls.size())) { return Status::insufficient_storage; }
    if (aliases(atlas.controls,rgba)||aliases(sx,rgba)||aliases(sy,rgba)) { return Status::overlap; }
    for (std::size_t p=0;p<sx.size();++p) {
        Scalar* out=rgba.data()+4*p;
        if (!std::isfinite(sx[p])||!std::isfinite(sy[p])||sx[p]<Scalar(-0.5)||sy[p]<Scalar(-0.5)
            ||sx[p]>=Scalar(atlas.source_width)-Scalar(0.5)||sy[p]>=Scalar(atlas.source_height)-Scalar(0.5)) {
            std::fill_n(out,4,Scalar(0)); continue;
        }
        const Scalar x=std::clamp(sx[p],Scalar(0),Scalar(atlas.source_width-1));
        const Scalar y=std::clamp(sy[p],Scalar(0),Scalar(atlas.source_height-1));
        const std::size_t cx=std::min(static_cast<std::size_t>(x),atlas.source_width-2);
        const std::size_t cy=std::min(static_cast<std::size_t>(y),atlas.source_height-2);
        const Scalar* origin=atlas.controls.data()+cy*5*atlas.lattice_row_stride+cx*5*4;
        detail::patch_rgba(origin,atlas.lattice_row_stride,x-Scalar(cx),y-Scalar(cy),out);
        if (projection==ColorProjection::premultiplied) {
            out[3]=std::clamp(out[3],Scalar(0),Scalar(1));
            for (std::size_t c=0;c<3;++c) { out[c]=std::clamp(out[c],Scalar(0),out[3]); }
        }
    }
    return Status::ok;
}
#define STX_INSTANTIATE(T) \
template void raw_currents<T>(const T* const[8],T* const[5],std::size_t) noexcept; \
template void admit_cell<T>(const T* const[5],T* const[5],const T*,const T*,const T*,const T*,T*,T*,std::size_t) noexcept; \
template void synthesize<T>(const T*,const T*,std::size_t,const std::array<T,5>&,T,const T*,T,T*,std::size_t,bool) noexcept; \
template void capacity<T>(const T* const[5],const T*,T*,std::size_t) noexcept; \
template void jet_terms<T>(const T* const[5],T*,T*,T*,std::size_t) noexcept; \
template void bubble<T>(const T*,const T*,T*,std::size_t,std::size_t,bool) noexcept; \
template void integrate_patch_rgba<T>(const T*,std::size_t,const T*,const T*,const T*,std::size_t,T*) noexcept; \
template void tensor_patch_rgba<T>(const T*,std::size_t,T,T,T*) noexcept; \
template void tensor_quintic<T>(const T* const[36],const T*,const T*,T*,std::size_t) noexcept; \
template Status map_points<T>(const std::array<T,9>&,std::span<const T>,std::span<const T>,std::span<T>,std::span<T>) noexcept; \
template Status sample_rgba<T>(TensorAtlasView<T>,std::span<const T>,std::span<const T>,std::span<T>,ColorProjection) noexcept;
STX_INSTANTIATE(float)
STX_INSTANTIATE(double)
#undef STX_INSTANTIATE
}
