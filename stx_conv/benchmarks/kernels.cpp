#include <stx_conv/kernels.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace {
enum class Task { jets, raw, capacity, admission, bubble, synthesis, tensor, mapping };
template<class Scalar>
struct Work {
    static constexpr std::size_t n=65536;
    std::array<std::vector<Scalar>,5> source;
    std::array<std::vector<Scalar>,36> controls;
    std::vector<Scalar> first,twice,second,raw,admitted,mass,threshold,amplitude,value,ld,ll,u,v,sx,sy;
    std::array<const Scalar*,5> sources{},raw_views{};
    std::array<Scalar*,5> raw_outputs{},admitted_outputs{};
    std::array<const Scalar*,8> terms{};
    std::array<const Scalar*,36> tensor_views{};
    std::array<Scalar,5> weights={Scalar(31)/32,Scalar(26)/32,Scalar(16)/32,Scalar(6)/32,Scalar(1)/32};
    std::array<Scalar,9> map={1,Scalar(.2),0,0,1,0,Scalar(.001),0,1};
    Work():first(n),twice(n),second(n),raw(n*5,Scalar(.002)),admitted(n*5,Scalar(.002)),mass(n,Scalar(.005)),threshold(n),amplitude(n),value(n),ld(n,Scalar(.01)),ll(n,Scalar(.002)),u(n),v(n),sx(n),sy(n) {
        for (std::size_t j=0;j<5;++j) {
            source[j].resize(n);
            for (std::size_t i=0;i<n;++i) { source[j][i]=Scalar(.5+.01*double(j)+.05*std::sin(double(i)*.01)); }
            sources[j]=source[j].data();raw_views[j]=raw.data()+j*n;raw_outputs[j]=raw.data()+j*n;admitted_outputs[j]=admitted.data()+j*n;
        }
        terms={sources[2],sources[3],first.data(),twice.data(),second.data(),first.data(),twice.data(),second.data()};
        for (std::size_t j=0;j<36;++j) {
            controls[j].resize(n);
            for (std::size_t i=0;i<n;++i) { controls[j][i]=Scalar(.5+.2*std::sin(double(j+i)*.03)); }
            tensor_views[j]=controls[j].data();
        }
        for (std::size_t i=0;i<n;++i) { u[i]=Scalar(i%101)/Scalar(100);v[i]=Scalar(i%79)/Scalar(78); }
    }
};
template<Task task,class Scalar>
void call(Work<Scalar>& w) {
    if constexpr (task==Task::jets) { stx_conv::kernels::jet_terms<Scalar>(w.sources.data(),w.first.data(),w.twice.data(),w.second.data(),w.n); }
    if constexpr (task==Task::raw) { stx_conv::kernels::raw_currents<Scalar>(w.terms.data(),w.raw_outputs.data(),w.n); }
    if constexpr (task==Task::capacity) { stx_conv::kernels::capacity<Scalar>(w.sources.data(),w.mass.data(),w.threshold.data(),w.n); }
    if constexpr (task==Task::admission) { stx_conv::kernels::admit_cell<Scalar>(w.raw_views.data(),w.admitted_outputs.data(),w.sources[2],w.sources[3],w.sources[4],w.raw_views[0],w.ld.data(),w.ll.data(),w.n); }
    if constexpr (task==Task::bubble) { stx_conv::kernels::bubble<Scalar>(w.raw.data(),w.admitted.data(),w.amplitude.data(),w.n,w.n,true); }
    if constexpr (task==Task::synthesis) { stx_conv::kernels::synthesize<Scalar>(w.sources[2],w.admitted.data(),w.n,w.weights,Scalar(1),w.amplitude.data(),Scalar(.001),w.value.data(),w.n,false); }
    if constexpr (task==Task::tensor) { stx_conv::kernels::tensor_quintic<Scalar>(w.tensor_views.data(),w.u.data(),w.v.data(),w.value.data(),w.n); }
    if constexpr (task==Task::mapping) { const stx_conv::Status status=stx_conv::kernels::map_points<Scalar>(w.map,w.u,w.v,w.sx,w.sy); if (status!=stx_conv::Status::ok) { std::abort(); } }
}
template<Task task,class Scalar>
void measure(Work<Scalar>& w,const char* name) {
    call<task>(w);
    std::array<double,5> times{};
    for (std::size_t sample=0;sample<5;++sample) {
        const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        for (std::size_t repeat=0;repeat<8;++repeat) { call<task>(w); }
        times[sample]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()*1e9/(8*double(w.n));
    }
    std::sort(times.begin(),times.end());
    std::printf("%s,%zu,%s,%zu,%.6f\n",stx_conv::kernels::backend(),sizeof(Scalar)*8,name,w.n,times[2]);
}
template<class Scalar>
void run() {
    Work<Scalar> w;
    measure<Task::jets>(w,"jets");measure<Task::raw>(w,"raw");measure<Task::capacity>(w,"capacity");
    measure<Task::admission>(w,"admission");measure<Task::bubble>(w,"bubble");
    measure<Task::synthesis>(w,"synthesis");measure<Task::tensor>(w,"tensor");measure<Task::mapping>(w,"mapping_checked");
    const double checksum=double(w.value[133])+double(w.sx[177])+double(w.threshold[11]);
    if (!std::isfinite(checksum)) { std::abort(); }
    std::fprintf(stderr,"f%zu checksum %.17g\n",sizeof(Scalar)*8,checksum);
}
}
int main() { std::puts("backend,bits,kernel,positions,ns_per_position");run<float>();run<double>();return 0; }
