#include "serial.hpp"
#include <memory>
int main(int argc,char**argv){try{
 Args a(argc,argv,{"--port","--input","--baud","--out","--bec-v"});require(a.has("--port")!=a.has("--input"),"Choose --port OR --input");
 std::unique_ptr<Serial> serial;std::ifstream file;if(a.has("--port"))serial=std::make_unique<Serial>(a.get("--port"),std::stoi(a.get("--baud","115200")));else{file.open(a.get("--input"),std::ios::binary);require(bool(file),"Cannot open input");}
 fs::path out=a.need("--out");new_run(out);std::ofstream raw(out/"raw.log",std::ios::binary),csv(out/"commands.csv");raw.exceptions(std::ios::failbit|std::ios::badbit);csv.exceptions(std::ios::failbit|std::ios::badbit);
 csv<<"seq,servo,pwm,pulse_us,frequency_hz,before_tick,after_tick,rc,host_receive_ns,measured_angle_deg,source,mask,submit_monotonic_ns,checked_monotonic_ns,e907_snapshot_tick\n";
 Json meta={{"complete",false},{"tick_hz",nullptr},{"measurement_source","manual_video"},{"clock","A53 monotonic submission/check interval is not mechanical actuation time; file import receive time is blank"},{"options",a.values},{"supply_topology","V1 H2 servo positive = VBATT; upstream regulation unverified"},{"measured_voltage_v",nullptr}};
 if(a.has("--bec-v")){size_t pos=0;auto value=std::stod(a.get("--bec-v"),&pos);require(pos==a.get("--bec-v").size()&&std::isfinite(value)&&value>0,"Invalid measured supply voltage");meta["measured_voltage_v"]=value;meta["voltage_source"]="operator provided measurement; --bec-v is a legacy option name, not topology evidence";}
 std::signal(SIGINT,on_signal);std::signal(SIGTERM,on_signal);int result=0;long long previous=-1;size_t rejected=0,gaps=0,rows=0,legacy_seq=0;std::string line;
 const std::regex tick(R"(SERVO_META,tick_hz,(\d+))"),modern(R"(^BENCH_CMD,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(-?\d+)$)"),oldsingle(R"(^BENCH servo=([1-4]) PWM=([4-7]) pulse_us=(\d+) \(other outputs disabled\)$)"),oldmask(R"(^BENCH mask=0x([0-9a-fA-F]+) pulse_us=(\d+) \(all selected outputs verified\)$)"),end(R"(^BENCH_END,(-?\d+)$)"),exitline(R"(^EXIT_STATUS (-?\d+)$)");
 auto process=[&]{
  if(!line.empty()&&line.back()=='\r')line.pop_back();
  std::smatch m;std::vector<long long> v;
  std::string received=serial?std::to_string(now_ns()):"";
  try{
   if(std::regex_search(line,m,tick))meta["tick_hz"]=std::stoll(m[1]);
   if(telemetry(line,v)){
    if(previous>=0&&v[0]!=previous+1)++gaps;
    previous=v[0];for(auto n:v)csv<<n<<',';
    csv<<received<<",,SERVO,,,,\n";++rows;
   }else{
    bool modern_row=std::regex_match(line,m,modern);
    long long seq=0,mask=0,pulse=0,rc=0;std::string submit,checked,snapshot,source;
    if(modern_row){
     seq=std::stoll(m[1]);mask=std::stoll(m[2]);pulse=std::stoll(m[3]);
     require(std::stoll(m[4])==333,"Invalid BENCH frequency");
     submit=m[5];checked=m[6];snapshot=m[7];rc=std::stoll(m[8]);
     require(std::stoll(checked)>=std::stoll(submit)&&std::stoll(snapshot)<=4294967295LL,"Invalid BENCH time");
     source="BENCH_CMD";meta["e907_tick_hz"]=24000000;meta["e907_tick_bits"]=32;
    }else if(std::regex_match(line,m,oldsingle)){
     auto ch=std::stoll(m[1]);require(std::stoll(m[2])==ch+3,"Invalid PWM mapping");
     mask=1LL<<(ch-1);pulse=std::stoll(m[3]);seq=++legacy_seq;source="BENCH_legacy";
    }else if(std::regex_match(line,m,oldmask)){
     mask=std::stoll(m[1],nullptr,16);pulse=std::stoll(m[2]);seq=++legacy_seq;source="BENCH_legacy";
    }
    if(!source.empty()){
     require(mask>0&&mask<=15&&pulse>=1400&&pulse<=1600,"Invalid BENCH target");
     for(int ch=0;ch<4;ch++)if(mask&(1<<ch)){
      csv<<seq<<','<<ch+1<<','<<ch+4<<','<<pulse<<",333,,,"<<rc<<','<<received<<",,"<<source<<','<<mask<<','<<submit<<','<<checked<<','<<snapshot<<'\n';++rows;
     }
     if(rc!=0)meta["command_failure"]=true;
    }else if(line.rfind("BENCH_CMD,",0)==0||line.rfind("BENCH servo=",0)==0||line.rfind("BENCH mask=",0)==0)throw std::runtime_error("Malformed BENCH record");
   }
   if(std::regex_match(line,m,end)||std::regex_match(line,m,exitline)){
    auto code=std::stoll(m[1]);meta["exit_code"]=code;meta["complete"]=code==0;meta["end_record"]=line;
   }else if(line.find("SERVO_END,")!=std::string::npos){meta["complete"]=true;meta["end_record"]=line;}
   // PASS alone is not proof of cleanup. Do not terminate raw capture early.
   csv.flush();
  }catch(const std::exception&e){std::cerr<<e.what()<<'\n';++rejected;}
  line.clear();
 };
 try{while(!interrupted){char c;if(serial){if(!serial->read(c))continue;}else if(!file.get(c)){require(!file.bad(),"Input read failed");break;}
  raw.put(c);if(c!='\n'){line+=c;require(line.size()<65536,"Unterminated serial line too long");continue;}raw.flush();process();
  if(serial&&meta["complete"].get<bool>())break;
 }}catch(const std::exception&e){meta["error"]=e.what();result=1;}
 // Truncated final lines are not treated as complete telemetry.
 if(!line.empty())meta["trailing_partial_line"]=line;
 meta["rejected_records"]=rejected;meta["sequence_gaps"]=gaps;meta["rows"]=rows;meta["interrupted"]=bool(interrupted);
 meta["legacy_timing"]="Legacy BENCH seq is synthetic; missing timestamps remain blank. Modern seq includes unlogged STOP commands; gaps are not inferred.";
 if(rejected||meta.value("command_failure",false)||result||!line.empty()||interrupted){meta["complete"]=false;if(rejected)result=1;}
 write_file(out/"metadata.json",meta.dump(2));if(interrupted)std::cerr<<"Collector stopped; this does NOT stop the board.\n";return result;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
