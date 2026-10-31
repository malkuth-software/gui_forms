#include "detail/conv_box_admission.hpp"
#include "detail/conv_bubble.hpp"
#include "detail/conv_jets.hpp"
#include "detail/conv_raw.hpp"
#include "detail/conv_mapping.hpp"
#include "detail/conv_tensor.hpp"
#include "detail/conv_synthesis.hpp"
namespace kd=stx_conv::kernels::detail;
#define STX_PROBES(T,SUFFIX) \
extern "C" void stx_patch_rgba_##SUFFIX(const T* p,std::size_t stride,T u,T v,T* output) { kd::patch_rgba(p,stride,u,v,output); } \
extern "C" void stx_affine_##SUFFIX(const std::array<T,9>& h,const T* x,const T* y,T* sx,T* sy) { kd::mapping_group<kd::NativeOps<T>,T,true>(h,x,y,sx,sy,0); } \
extern "C" void stx_projective_##SUFFIX(const std::array<T,9>& h,const T* x,const T* y,T* sx,T* sy) { kd::mapping_group<kd::NativeOps<T>,T,false>(h,x,y,sx,sy,0); } \
extern "C" void stx_jet_##SUFFIX(const T* const input[5],T* m,T* t,T* q) { kd::jet_group<kd::NativeOps<T>>(input,m,t,q,0); } \
extern "C" void stx_raw_##SUFFIX(const T* const input[8],T* const output[5]) { kd::raw_group<kd::NativeOps<T>>(input,output,0); } \
extern "C" void stx_capacity_##SUFFIX(const T* const input[5],const T* mass,T* output) { \
 std::array<kd::NativeOps<T>::Value,5> c{}; \
 for (std::size_t j=0;j<5;++j) { c[j]=kd::NativeOps<T>::load(input[j]); } \
 kd::NativeOps<T>::store(output,kd::capacity_threshold<kd::NativeOps<T>>(c,kd::NativeOps<T>::load(mass))); } \
extern "C" void stx_admit_##SUFFIX(const T* const r[5],T* const out[5],const T* y0,const T* y1,const T* y2,const T* next,T* ld,T* ll) { kd::admit_vector<kd::NativeOps<T>>(r,out,y0,y1,y2,next,ld,ll,0); } \
extern "C" void stx_bubble_##SUFFIX(const T* raw,const T* admitted,T* output,std::size_t stride) { kd::bubble_group<kd::NativeOps<T>,T,true>(raw,admitted,output,stride,0); } \
extern "C" void stx_tensor_##SUFFIX(const T* const input[36],const T* u,const T* v,T* output) { kd::tensor_group<kd::NativeOps<T>>(input,u,v,output,0); } \
extern "C" void stx_synthesis_##SUFFIX(const T* anchors,const T* c,std::size_t stride,const std::array<T,5>& weights,T aw,const T* bubble,T bw,T* output) { kd::synthesis_group<kd::NativeOps<T>,T,true,false>(anchors,c,stride,weights,aw,bubble,bw,output,0); }
STX_PROBES(float,f32)
STX_PROBES(double,f64)
#undef STX_PROBES
