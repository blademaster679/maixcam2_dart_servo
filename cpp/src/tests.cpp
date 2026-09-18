#include "common.hpp"
template<class F>void rejects(F f){bool threw=false;try{f();}catch(...){threw=true;}require(threw,"Expected rejection");}
int main(int argc,char**argv){try{
 require(argc==2,"Provide config.json");auto c=Json::parse(read_file(argv[1]));auto steps=plan(c);require(steps.size()==108&&steps.back().pulse==1500,"Plan mismatch");for(auto&s:steps)require(s.pulse>=1445&&s.pulse<=1555,"Bounds mismatch");
 auto bad=c;bad["offsets_us"]={101};rejects([&]{validate(bad);});bad=c;bad["channels"][1]=bad["channels"][0];rejects([&]{validate(bad);});bad=c;bad["hold_s"]=-1;rejects([&]{validate(bad);});
 std::vector<long long> v;require(telemetry("msh> SERVO,2,1,4,1522,333,10,11,0\r",v)&&v[3]==1522,"Telemetry mismatch");require(!telemetry("noise",v),"Noise accepted");require(!telemetry("SERVO,2,1,4,1522,333,10,11,0junk",v),"Malformed tail accepted");rejects([&]{telemetry("SERVO,1,1,8,1500,333,1,1,0",v);});
 require(std::abs(angle({10,10},{20,10},{10,0})-90)<1e-10,"Angle mismatch");require(std::abs(angle({10,10},{20,10},{10,20})+90)<1e-10,"Angle sign mismatch");rejects([]{angle({0,0},{0,0},{10,10});});require(csv_quote("a,\"b")=="\"a,\"\"b\"","CSV quoting mismatch");
 std::string abc="abc";require(picosha2::hash256_hex_string(abc)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA mismatch");std::cout<<"PASS: plan, bounds, input rejection, telemetry, signed geometry, CSV and SHA256\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
