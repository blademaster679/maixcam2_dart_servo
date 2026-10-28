#include "../imu/bias.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace dual_imu;
static void expect(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool threw=false;try{f();}catch(const std::exception &){threw=true;}expect(threw,"expected rejection");}
static StationaryWindow fixture(int kind=0) {
    StationaryWindow window;
    for(int i=0;i<4000;++i) {
        Sample s;s.values={0,0,1,1,-2,0.5};s.temperature=40;
        for(size_t axis=3;axis<6;++axis)s.values[axis]+=(i%2?0.02:-0.02);
        if(kind==1)s.values[3]+=(i%2?1:-1); // Vibration / motion.
        if(kind==2)s.temperature+=i/1000.0; // Warm-up drift.
        if(kind==3)s.valid=i!=500;
        if(kind==4)s.values[3]+=i<2000?-0.06:0.06; // Stable overall std, drifting block means.
        if(kind==5)s.values[2]=0; // Free fall / invalid gravity magnitude.
        window.add(s,i/200.0);
    }
    return window;
}
int main() {
    auto window=fixture();auto bias=window.estimate(20);
    expect(std::abs(bias.dps[0]-1)<1e-12 && std::abs(bias.dps[1]+2)<1e-12,"known stationary bias");
    auto loaded=GyroBias::from_json(bias.json());
    Sample holdout;holdout.values={0,0,1,1.02,-2.01,0.505};holdout.temperature=40;
    auto raw=holdout.values;auto corrected=loaded.correct(holdout);
    expect(std::abs(corrected[0]-0.02)<1e-12 && std::abs(corrected[1]+0.01)<1e-12 &&
           std::abs(corrected[2]-0.005)<1e-12,"independent residual and subtraction sign");
    expect(raw==holdout.values,"preserve original readings");
    holdout.values[3]+=5;
    expect(std::abs(loaded.correct(holdout)[0]-5.02)<1e-12,"real rotation must not be learned away");
    holdout.temperature=44;rejects([&]{loaded.correct(holdout);});
    for(int kind=1;kind<=5;++kind)rejects([&]{fixture(kind).estimate(20);});
    rejects([&]{StationaryWindow{}.estimate(20);});
    rejects([&]{window.estimate(40);}); // Too few samples for declared duration.
    auto bad=bias.json();bad["bias_dps"]={1,2};rejects([&]{GyroBias::from_json(bad);});
    bad=bias.json();bad["bias_dps"][0]=11;rejects([&]{GyroBias::from_json(bad);});
    bad=bias.json();bad["reference_temperature_c"]=nullptr;rejects([&]{GyroBias::from_json(bad);});
    std::cout<<"Stationary gates, frozen bias, independent residual, profile and temperature checks passed\n";
}
