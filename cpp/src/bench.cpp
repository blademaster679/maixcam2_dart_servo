#include "common.hpp"
#include <memory>
#ifdef SERVO_MAIXCDK
#include "maix_pwm.hpp"
#include "maix_pinmap.hpp"
#include "maix_sys.hpp"
#endif
struct Hardware{
#ifdef SERVO_MAIXCDK
 std::map<int,std::unique_ptr<maix::peripheral::pwm::PWM>> devices;
 std::map<std::string,std::string> old;
 void init(const Json&x,int hz){using namespace maix::peripheral;std::string pin=x.at("pin");int id=x.at("servo"),p=x.at("pwm");old[pin]=pinmap::get_pin_function(pin);require(int(pinmap::set_pin_function(pin,"PWM"+std::to_string(p)))==0,"pinmux failed");devices[id]=std::make_unique<pwm::PWM>(p,hz,0,false);}
 void set(int id,double us){require(devices.at(id)->duty_val(int(std::llround(us*1000)))>=0,"PWM write failed");}
 void enable(int id){require(int(devices.at(id)->enable())==0,"PWM enable failed");}
 ~Hardware(){for(auto&x:devices)try{if(int(x.second->disable())!=0)std::cerr<<"DISABLE FAILED "<<x.first<<'\n';}catch(...){std::cerr<<"DISABLE exception\n";}devices.clear();for(auto&x:old)try{if(int(maix::peripheral::pinmap::set_pin_function(x.first,x.second))!=0)std::cerr<<"RESTORE FAILED "<<x.first<<'\n';}catch(...){std::cerr<<"RESTORE exception\n";}}
#else
 void init(const Json&,int){throw std::runtime_error("Build with MaixCDK for --run");}void set(int,double){}void enable(int){}
#endif
};
int main(int argc,char**argv){try{
 Args a(argc,argv,{"--config","--out"},{"--run"});Json c=Json::parse(read_file(a.get("--config","config.json")));auto steps=plan(c);bool run=a.has("--run");
#ifndef SERVO_MAIXCDK
 require(!run,"This PC binary is dry-run only. Build MaixCDK project for hardware.");
#else
 if(run)require(maix::sys::device_id()=="maixcam2","Wrong board");
#endif
 fs::path out=a.need("--out");new_run(out);c["mode"]=run?"hardware_cpp":"DRY_RUN";c["clock"]="steady_clock nanoseconds; commands NOT motion feedback";write_file(out/"metadata.json",c.dump(2));
 std::signal(SIGINT,on_signal);std::signal(SIGTERM,on_signal);unsigned seq=0;std::string outcome="complete";int result=0;
 std::ofstream log((out/"commands.csv").string());log.exceptions(std::ios::failbit|std::ios::badbit);log<<"seq,event,servo,pwm,pulse_us,frequency_hz,before_ns,after_ns,measured_angle_deg,status\n";
 try{Hardware hw;int hz=c.at("frequency_hz");auto command=[&](Step s,const char*event){auto before=now_ns();std::string status=run?"API_OK_NOT_FEEDBACK":"DRY_RUN";bool failed=false;try{if(run)hw.set(s.servo,s.pulse);}catch(...){status="API_ERROR";failed=true;}
 log<<++seq<<','<<event<<','<<s.servo<<','<<s.pwm<<','<<s.pulse<<','<<hz<<','<<before<<','<<now_ns()<<",,"<<status<<'\n';log.flush();std::cout<<seq<<" servo="<<s.servo<<" pulse_us="<<s.pulse<<' '<<status<<std::endl;require(!failed,"PWM command failed");};
 if(run){for(auto&x:c["channels"]){if(interrupted)break;hw.init(x,hz);command({x["servo"],x["pwm"],x["center_us"]},"initial_neutral");hw.enable(x["servo"]);}sleep_interruptible(3000);}
 for(auto&s:steps){if(interrupted)break;command(s,"step");if(run)sleep_interruptible(int(c["hold_s"].get<double>()*1000));}
 if(interrupted){outcome="interrupted";result=130;}
 }catch(const std::exception&e){outcome="error";result=1;std::cerr<<e.what()<<'\n';}
 write_file(out/"end.json",Json{{"last_seq",seq},{"outcome",outcome}}.dump(2));return result;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
