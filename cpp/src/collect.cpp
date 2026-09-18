#include "serial.hpp"
#include <memory>
int main(int argc,char**argv){try{
 Args a(argc,argv,{"--port","--input","--baud","--out","--bec-v"});require(a.has("--port")!=a.has("--input"),"Choose --port OR --input");
 std::unique_ptr<Serial> serial;std::ifstream file;if(a.has("--port"))serial=std::make_unique<Serial>(a.get("--port"),std::stoi(a.get("--baud","115200")));else{file.open(a.get("--input"),std::ios::binary);require(bool(file),"Cannot open input");}
 fs::path out=a.need("--out");new_run(out);std::ofstream raw((out/"raw.log").string(),std::ios::binary),csv((out/"commands.csv").string());raw.exceptions(std::ios::failbit|std::ios::badbit);csv.exceptions(std::ios::failbit|std::ios::badbit);csv<<"seq,servo,pwm,pulse_us,frequency_hz,before_tick,after_tick,rc,host_receive_ns,measured_angle_deg\n";
 Json meta={{"complete",false},{"tick_hz",nullptr},{"measurement_source","manual_video"},{"clock","host receive time is NOT actuation time"},{"options",a.values}};
 std::signal(SIGINT,on_signal);std::signal(SIGTERM,on_signal);int result=0;long long previous=-1;size_t rejected=0,gaps=0;std::string line;
 try{while(!interrupted){char c;if(serial){if(!serial->read(c))continue;}else{if(!file.get(c)){require(!file.bad(),"Input read failed");break;}}
 raw.put(c);if(c!='\n'){line+=c;require(line.size()<65536,"Unterminated serial line too long");continue;}raw.flush();
 std::smatch m;static const std::regex tick(R"(SERVO_META,tick_hz,(\d+))");if(std::regex_search(line,m,tick))meta["tick_hz"]=std::stoll(m[1]);
 std::vector<long long> v;bool valid=false;try{valid=telemetry(line,v);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';rejected++;}
 if(valid){if(previous>=0&&v[0]!=previous+1){gaps++;std::cerr<<"Sequence gap/restart\n";}previous=v[0];for(auto n:v)csv<<n<<',';csv<<now_ns()<<",\n";csv.flush();}
 if(line.find("SERVO_END,")!=std::string::npos){meta["complete"]=true;meta["end_record"]=line;break;}line.clear();}}
 catch(const std::exception&e){meta["error"]=e.what();result=1;}
 if(!line.empty()&&!meta["complete"].get<bool>())meta["trailing_partial_line"]=line;
 meta["rejected_records"]=rejected;meta["sequence_gaps"]=gaps;meta["interrupted"]=bool(interrupted);write_file(out/"metadata.json",meta.dump(2));if(interrupted)std::cerr<<"Collector stopped; this does NOT stop the board.\n";return result;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
