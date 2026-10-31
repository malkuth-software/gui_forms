#include <stx_conv/kernels.hpp>
#include <stx_conv/stream.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace {
void require(bool value,const char* message) { if (!value) { throw std::runtime_error(message); } }
template<class Scalar>
void leaf_kernels() {
    constexpr std::size_t n=19,stride=n+2;
    std::array<std::array<Scalar,stride>,5> source{},admitted{},caps{};
    std::array<const Scalar*,5> nodes{},raw{},capacities{};
    std::array<Scalar*,5> output{};
    std::array<Scalar,stride> mass{},threshold{},f{},tw{},q{},y0{},y1{},y2{},next{},ld{},ll{},a{},value{};
    const Scalar epsilon=Scalar(64)*std::numeric_limits<Scalar>::epsilon();
    for (std::size_t j=0;j<5;++j) {
        for (std::size_t i=0;i<stride;++i) { source[j][i]=Scalar(j)+Scalar(i)*Scalar(.1); caps[j][i]=Scalar(1); admitted[j][i]=Scalar(.2); }
        nodes[j]=source[j].data()+1;capacities[j]=caps[j].data()+1;
        raw[j]=admitted[j].data()+1;output[j]=source[j].data()+1;
    }
    for (std::size_t i=0;i<stride;++i) { mass[i]=Scalar(5)*Scalar(i)/Scalar(stride);y1[i]=1;y2[i]=2;next[i]=Scalar(.2);ld[i]=1;ll[i]=Scalar(.2); }
    stx_conv::kernels::capacity<Scalar>(capacities.data(),mass.data()+1,threshold.data()+1,n);
    stx_conv::kernels::jet_terms<Scalar>(nodes.data(),f.data()+1,tw.data()+1,q.data()+1,n);
    for (std::size_t i=1;i<=n;++i) {
        require(std::abs(threshold[i]-mass[i]/Scalar(5))<epsilon,"capacity wrapper failed");
        require(std::abs(f[i]-Scalar(.2))<epsilon && std::abs(tw[i]-Scalar(.4))<epsilon && std::abs(q[i])<epsilon,"jet wrapper failed");
    }
    stx_conv::kernels::admit_cell<Scalar>(raw.data(),output.data(),y0.data()+1,y1.data()+1,y2.data()+1,next.data()+1,ld.data()+1,ll.data()+1,n);
    for (std::size_t j=0;j<5;++j) { for (std::size_t i=1;i<=n;++i) { require(std::abs(source[j][i]-Scalar(.2))<epsilon,"admission wrapper failed"); } }
    std::array<Scalar,5*stride> packed{};
    packed.fill(Scalar(.2));
    stx_conv::kernels::bubble<Scalar>(packed.data(),packed.data(),a.data()+1,stride,n,true);
    for (std::size_t i=1;i<=n;++i) { require(std::abs(a[i])<epsilon,"bubble wrapper failed"); }
    const std::array<Scalar,5> weights={Scalar(31)/32,Scalar(26)/32,Scalar(16)/32,Scalar(6)/32,Scalar(1)/32};
    stx_conv::kernels::synthesize<Scalar>(y0.data()+1,packed.data()+1,stride,weights,Scalar(1),nullptr,Scalar(0),value.data()+1,n,false);
    stx_conv::kernels::synthesize<Scalar>(y0.data()+1,packed.data()+1,stride,weights,Scalar(1),nullptr,Scalar(0),value.data()+1,n,true);
    for (std::size_t i=1;i<=n;++i) { require(std::abs(value[i]-Scalar(1))<epsilon,"synthesis wrapper failed"); }
    require(threshold.front()==Scalar(0) && threshold.back()==Scalar(0) && value.front()==Scalar(0) && value.back()==Scalar(0),"kernel overwrote padding");
}
template<class Scalar>
void tensor_and_geometry() {
    constexpr std::size_t n=19;
    std::array<std::array<Scalar,n>,36> controls{};
    std::array<const Scalar*,36> views{};
    std::array<Scalar,n> u{},v{},out{},x{},y{},sx{},sy{};
    for (std::size_t p=0;p<n;++p) { u[p]=Scalar(p)/Scalar(n-1); v[p]=Scalar(n-1-p)/Scalar(n-1); x[p]=u[p];y[p]=v[p]; }
    for (std::size_t j=0;j<36;++j) {
        views[j]=controls[j].data();
        for (std::size_t p=0;p<n;++p) { controls[j][p]=Scalar(std::sin(double(j*7+p*3))*.4+.5); }
    }
    stx_conv::kernels::tensor_quintic<Scalar>(views.data(),u.data(),v.data(),out.data(),n);
    const long double tolerance=Scalar(128)*std::numeric_limits<Scalar>::epsilon();
    for (std::size_t p=0;p<n;++p) {
        std::array<long double,6> rows{};
        for (std::size_t j=0;j<6;++j) {
            std::array<long double,6> c{};
            for (std::size_t i=0;i<6;++i) { c[i]=controls[j*6+i][p]; }
            for (std::size_t k=5;k>0;--k) { for (std::size_t i=0;i<k;++i) { c[i]=(1-static_cast<long double>(u[p]))*c[i]+static_cast<long double>(u[p])*c[i+1]; } }
            rows[j]=c[0];
        }
        for (std::size_t k=5;k>0;--k) { for (std::size_t j=0;j<k;++j) { rows[j]=(1-static_cast<long double>(v[p]))*rows[j]+static_cast<long double>(v[p])*rows[j+1]; } }
        require(std::abs(static_cast<long double>(out[p])-rows[0])<tolerance,"tensor disagrees with independent de Casteljau");
    }
    std::array<Scalar,144> patch{};
    std::array<const Scalar*,36> patch_views{};
    for (std::size_t i=0;i<patch.size();++i) { patch[i]=Scalar(.5+.4*std::sin(double(i)*.31)); }
    for (std::size_t i=0;i<36;++i) { patch_views[i]=patch.data()+i*4; }
    const std::array<std::array<Scalar,2>,5> fractions={{{0,0},{1,Scalar(.3)},{Scalar(.2),1},{Scalar(.4),Scalar(.6)},{0,1}}};
    for (const std::array<Scalar,2>& t:fractions) {
        const Scalar ux[4]={t[0],t[0],t[0],t[0]},vy[4]={t[1],t[1],t[1],t[1]};
        Scalar generic[4]={},specialized[4]={};
        stx_conv::kernels::tensor_quintic<Scalar>(patch_views.data(),ux,vy,generic,4);
        stx_conv::kernels::tensor_patch_rgba<Scalar>(patch.data(),24,t[0],t[1],specialized);
        for (std::size_t i=0;i<4;++i) { require(std::abs(generic[i]-specialized[i])<tolerance,"patch boundary specialization differs"); }
    }
    for (std::size_t j=0;j<6;++j) { for (std::size_t i=0;i<6;++i) {
        const Scalar fraction=Scalar(i)/Scalar(5);
        const std::size_t offset=(j*6+i)*4;
        patch[offset]=Scalar(.7)-Scalar(.4)*fraction;patch[offset+1]=Scalar(.05);
        patch[offset+2]=Scalar(.1);patch[offset+3]=Scalar(.2)+Scalar(.6)*fraction;
    } }
    const Scalar qu[2]={0,1},qv[2]={Scalar(.5),Scalar(.5)},qw[2]={Scalar(.5),Scalar(.5)};
    Scalar sums[4]={};
    stx_conv::kernels::integrate_patch_rgba<Scalar>(patch.data(),24,qu,qv,qw,2,sums);
    require(std::abs(sums[0]-Scalar(.25))<tolerance && std::abs(sums[3]-Scalar(.5))<tolerance,
            "physical projection was moved after quadrature");
    stx_conv::kernels::integrate_patch_rgba<Scalar>(nullptr,0,nullptr,nullptr,nullptr,0,nullptr);
    // Resize/stretch, rotate, skew, translation, and perspective inverse maps.
    const std::array<std::array<Scalar,9>,5> maps={{{Scalar(.5),0,0,0,Scalar(.75),0,0,0,1},
        {0,-1,1,1,0,0,0,0,1},{1,Scalar(.2),0,Scalar(-.1),1,0,0,0,1},
        {1,0,Scalar(.1),0,1,Scalar(.2),0,0,1},{1,Scalar(.1),0,0,1,0,Scalar(.2),Scalar(.1),1}}};
    std::vector<Scalar> atlas(11*11*4);
    for (std::size_t j=0;j<11;++j) {
        for (std::size_t i=0;i<11;++i) {
            const std::size_t k=(j*11+i)*4;
            atlas[k]=Scalar(.1)+Scalar(.05)*Scalar(i)/Scalar(5)+Scalar(.1)*Scalar(j)/Scalar(5);
            atlas[k+1]=Scalar(.2);atlas[k+2]=Scalar(.3);atlas[k+3]=Scalar(.8);
        }
    }
    std::array<Scalar,n*4> rgba{};
    for (const std::array<Scalar,9>& h:maps) {
        require(stx_conv::kernels::map_points<Scalar>(h,x,y,sx,sy)==stx_conv::Status::ok,"map failed");
        require(stx_conv::kernels::sample_rgba<Scalar>({atlas,3,3,44},sx,sy,rgba)==stx_conv::Status::ok,"atlas failed");
        for (std::size_t p=0;p<n;++p) {
            const long double den=static_cast<long double>(h[6])*x[p]+static_cast<long double>(h[7])*y[p]+h[8];
            const long double px=(static_cast<long double>(h[0])*x[p]+static_cast<long double>(h[1])*y[p]+h[2])/den;
            const long double py=(static_cast<long double>(h[3])*x[p]+static_cast<long double>(h[4])*y[p]+h[5])/den;
            require(std::abs(static_cast<long double>(sx[p])-px)<tolerance,"map x inaccurate");
            const long double expected=.1L+.05L*std::max(0.L,px)+.1L*std::max(0.L,py);
            require(std::abs(static_cast<long double>(rgba[p*4])-expected)<tolerance,"transformed ramp inaccurate");
        }
    }
    sx.fill(Scalar(-1));sy.fill(Scalar(1));
    require(stx_conv::kernels::sample_rgba<Scalar>({atlas,3,3,44},sx,sy,rgba)==stx_conv::Status::ok,"outside sampling failed");
    for (const Scalar value:rgba) { require(value==Scalar(0),"outside not transparent"); }
    std::array<Scalar,9> horizon={1,0,0,0,1,0,0,0,0};sx.fill(Scalar(17));
    require(stx_conv::kernels::map_points<Scalar>(horizon,x,y,sx,sy)==stx_conv::Status::invalid_shape,"horizon accepted");
    for (const Scalar value:sx) { require(value==Scalar(17),"failed mapping changed output"); }
    require(stx_conv::kernels::map_points<Scalar>(maps[0],x,y,x,sy)==stx_conv::Status::overlap,"map alias accepted");
    std::array<Scalar,9> huge={1,0,0,0,1,0,std::numeric_limits<Scalar>::max(),0,1};
    std::array<Scalar,1> large_x={Scalar(2)},zero_y={Scalar(0)},protected_x={Scalar(19)},protected_y={Scalar(23)};
    require(stx_conv::kernels::map_points<Scalar>(huge,large_x,zero_y,protected_x,protected_y)==stx_conv::Status::numerical_failure,"overflowed denominator accepted");
    require(protected_x[0]==Scalar(19) && protected_y[0]==Scalar(23),"overflow changed output");
}
template<class Scalar>
void toggle() {
    constexpr std::size_t sw=17,sh=13,tw=25,th=19;
    std::vector<Scalar> input(sw*sh),on(tw*th),off(tw*th),again(tw*th);
    for (std::size_t i=0;i<input.size();++i) { input[i]=Scalar(.5+.3*std::sin(double(i%sw)*.7)*std::cos(double(i/sw)*.9)); }
    stx_conv::stream::Workspace<Scalar> work(sw,sh,tw,th,1,stx_conv::Sampling::nodal);
    require(work.is_bubble_enabled(),"bubble not enabled by default");
    require(stx_conv::stream::resize<Scalar>(work,{input,sw},{on,tw})==stx_conv::Status::ok,"default resize failed");
    work.set_bubble_enabled(false);
    require(stx_conv::stream::resize<Scalar>(work,{input,sw},{off,tw})==stx_conv::Status::ok,"disabled resize failed");
    require(on!=off,"toggle has no effect on non-midpoint targets");
    work.set_bubble_enabled(true);
    require(stx_conv::stream::resize<Scalar>(work,{input,sw},{again,tw})==stx_conv::Status::ok,"reenabled resize failed");
    require(on==again,"workspace toggle is not repeatable");
}
}
int main() {
    try { leaf_kernels<float>();leaf_kernels<double>();tensor_and_geometry<float>();tensor_and_geometry<double>();toggle<float>();toggle<double>(); }
    catch (const std::exception& error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
    std::printf("kernels and transforms passed: %s\n",stx_conv::kernels::backend());return 0;
}
