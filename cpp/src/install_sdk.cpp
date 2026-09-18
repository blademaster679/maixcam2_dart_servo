#include "common.hpp"
static std::string lf(std::string s){s.erase(std::remove(s.begin(),s.end(),'\r'),s.end());return s;}
int main(int argc,char**argv){try{
 Args a(argc,argv,{"--sdk","--package"});fs::path sdk=fs::absolute(a.need("--sdk")),pkg=fs::absolute(a.get("--package","."));
 auto driver=sdk/"riscv/drivers/pwm/drv_pwm.c",backup=sdk/"riscv/drivers/pwm/drv_pwm.c.servo_backup",app=sdk/"riscv/applications/servo_bench.c";
 auto text=read_file(driver),ref=read_file(pkg/"research/riscv__drivers__pwm__drv_pwm.c"),ext=read_file(pkg/"firmware/pwm_us_extension.c.inc"),payload=read_file(pkg/"firmware/servo_bench.c");
 bool original=lf(text)==lf(ref);require(original||lf(text)==lf(ref+ext),"SDK driver differs from reviewed version/extension");require(!fs::exists(app)||read_file(app)==payload,"Existing application differs; preserving it");require(!original||!fs::exists(backup),"Backup already exists; preserving it");require(fs::is_directory(app.parent_path()),"Missing applications directory");
 if(original){fs::copy_file(driver,backup);try{write_file(driver,text+ext);write_file(app,payload);}catch(...){fs::copy_file(backup,driver,fs::copy_options::overwrite_existing);throw;}}
 else if(!fs::exists(app))write_file(app,payload);
 std::cout<<"Integrated. No build/flash. SHA256 "<<sha256(driver)<<"\nRelocate UART1 and resolve Linux PWM ownership before running.\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
