#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "json.hpp"
#include "picosha2.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include "filesystem.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
namespace fs=ghc::filesystem;
using Json=nlohmann::json;
inline volatile std::sig_atomic_t interrupted=0;
inline void on_signal(int){interrupted=1;}
inline long long now_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void require(bool ok,const std::string &s){if(!ok)throw std::runtime_error(s);}
inline std::string read_file(const fs::path&p){std::ifstream f(p.string(),std::ios::binary);require(bool(f),"Cannot read "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
inline void write_file(const fs::path&p,const std::string&s){std::ofstream f(p.string(),std::ios::binary);f.exceptions(std::ios::failbit|std::ios::badbit);f<<s;}
inline std::string sha256(const fs::path&p){std::ifstream f(p.string(),std::ios::binary);require(bool(f),"Cannot hash "+p.string());picosha2::hash256_one_by_one h;char b[65536];while(f.read(b,sizeof(b))||f.gcount()){h.process(b,b+f.gcount());}require(!f.bad(),"Hash read failed");h.finish();return picosha2::get_hash_hex_string(h);}
inline std::string csv_quote(std::string s){std::string o="\"";for(char c:s){if(c=='"')o+='"';o+=c;}return o+'"';}
inline void new_run(const fs::path&p){require(!fs::exists(p),"Output already exists: "+p.string());require(fs::create_directories(p),"Cannot create run directory");}
inline void sleep_ms(int ms){
#ifdef _WIN32
 Sleep(static_cast<DWORD>(ms));
#else
 std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}
inline void sleep_interruptible(int ms){for(int t=0;t<ms&&!interrupted;t+=10)sleep_ms(std::min(10,ms-t));}
struct Args{
 std::map<std::string,std::string> values;
 Args(int argc,char**argv,std::set<std::string> keys,std::set<std::string> flags={}){for(int i=1;i<argc;i++){std::string k=argv[i];require(!values.count(k),"Duplicate option "+k);if(flags.count(k))values[k]="true";else{require(keys.count(k)&&i+1<argc,"Unknown/missing option "+k);values[k]=argv[++i];}}}
 std::string get(const std::string&k,const std::string&d="")const{auto it=values.find(k);return it==values.end()?d:it->second;}
 bool has(const std::string&k)const{return values.count(k)!=0;}
 std::string need(const std::string&k)const{require(has(k)&&!get(k).empty(),"Required "+k);return get(k);}
};
inline void validate(const Json&c){
 int hz=c.at("frequency_hz"),reps=c.at("repetitions");double hold=c.at("hold_s");
 require(hz==50||hz==333,"Frequency must be 50 or 333");require(std::isfinite(hold)&&hold>=.5&&hold<=30&&reps>=1&&reps<=10,"Invalid hold/repetitions");
 auto ch=c.at("channels"),steps=c.at("offsets_us");require(ch.is_array()&&ch.size()==4&&steps.is_array()&&!steps.empty()&&steps.size()<=100,"Invalid channels/steps");
 std::set<int> ids,pwms;const std::map<int,std::string> pins={{4,"B2"},{5,"B3"},{6,"A30"},{7,"A31"}};
 for(auto&x:ch){int id=x.at("servo"),p=x.at("pwm");std::string pin=x.at("pin");require(id>=1&&id<=4&&ids.insert(id).second&&pins.count(p)&&pins.at(p)==pin&&pwms.insert(p).second,"Invalid/duplicate mapping");
 double lo=x.at("min_us"),mid=x.at("center_us"),hi=x.at("max_us");require(500<=lo&&lo<=mid&&mid<=hi&&hi<=2500,"Invalid bounds");
 for(auto&d:steps){double pulse=mid+d.get<double>();require(std::isfinite(pulse)&&pulse>=lo&&pulse<=hi&&pulse<1e6/hz,"Pulse out of bounds");}}
}
struct Step{int servo,pwm;double pulse;};
inline std::vector<Step> plan(const Json&c){validate(c);std::vector<Step> out;for(int r=0;r<c.at("repetitions").get<int>();r++)for(auto&x:c.at("channels"))for(auto&d:c.at("offsets_us"))out.push_back({x.at("servo"),x.at("pwm"),x.at("center_us").get<double>()+d.get<double>()});return out;}
inline bool telemetry(const std::string&line,std::vector<long long>&v){
 static const std::regex re(R"(SERVO,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(-?\d+)(?:\r?$))");std::smatch m;if(!std::regex_search(line,m,re))return false;
 v.clear();for(int i=1;i<=8;i++)v.push_back(std::stoll(m[i]));require(v[1]>=1&&v[1]<=4&&v[2]==v[1]+3&&v[3]>=1400&&v[3]<=1600&&v[4]==333,"Invalid telemetry values");return true;
}
struct Point{double x,y;};
inline double angle(Point p,Point z,Point t){double ax=z.x-p.x,ay=p.y-z.y,bx=t.x-p.x,by=p.y-t.y;require(std::hypot(ax,ay)>=5&&std::hypot(bx,by)>=5,"Points too close to pivot");return std::atan2(ax*by-ay*bx,ax*bx+ay*by)*180.0/std::acos(-1.0);}
