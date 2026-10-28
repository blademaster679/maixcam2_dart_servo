#include "linux_bus.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>

namespace dual_imu {
namespace fs = std::filesystem;
void check(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}
static void syscall_ok(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message + ": " + std::strerror(errno));
}
void delay_us(unsigned us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
uint64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static void sysfs_write(const fs::path &path, const std::string &value) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    syscall_ok(fd >= 0, "open " + path.string());
    auto count = ::write(fd, value.data(), value.size());
    int error = errno;
    ::close(fd);
    errno = error;
    syscall_ok(count == static_cast<ssize_t>(value.size()), "write " + path.string());
}
static int request_gpio(const char *chip, const std::vector<unsigned> &lines, bool output) {
    int fd = ::open(chip, O_RDONLY | O_CLOEXEC);
    syscall_ok(fd >= 0, std::string("open ") + chip);
    try {
        gpiochip_info info{};
        syscall_ok(ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0, "GPIO chip information");
        const std::string expected = std::string(chip) == "/dev/gpiochip0" ? "4800000.gpio" : "4801000.gpio";
        check(info.label == expected, "Unexpected GPIO bank " + std::string(info.label));
        gpiohandle_request request{};
        request.flags = output ? GPIOHANDLE_REQUEST_OUTPUT : GPIOHANDLE_REQUEST_INPUT;
        request.lines = lines.size();
        std::strcpy(request.consumer_label, "maixcam2-dual-imu");
        for (size_t i=0; i<lines.size(); ++i) {
            gpioline_info line{};
            line.line_offset = lines[i];
            syscall_ok(ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &line) == 0, "GPIO line information");
            check(!(line.flags & (GPIOLINE_FLAG_KERNEL | GPIOLINE_FLAG_IS_OUT)),
                  "GPIO line already owned or output: " + std::to_string(lines[i]));
            request.lineoffsets[i] = lines[i];
            request.default_values[i] = 1;
        }
        syscall_ok(ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &request) == 0, "request GPIO lines");
        ::close(fd);
        return request.fd;
    } catch (...) { ::close(fd); throw; }
}
static void release_cs_to_input(int fd) {
    // Linux 4.19 lacks GPIOHANDLE_SET_CONFIG_IOCTL: release then request input.
    ::close(fd);
    int chip = ::open("/dev/gpiochip0", O_RDONLY | O_CLOEXEC);
    syscall_ok(chip >= 0, "open GPIO for restore");
    gpiohandle_request r{};
    r.lineoffsets[0] = 27; r.lines = 1; r.flags = GPIOHANDLE_REQUEST_INPUT;
    std::strcpy(r.consumer_label, "dual-imu-restore");
    int result = ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &r);
    int error = errno;
    ::close(chip); errno = error;
    syscall_ok(result == 0, "restore A27 GPIO input");
    ::close(r.fd);
}
uint32_t CarrierV11::read32(uint32_t address) {
    void *p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,memory_,address&~4095U);
    syscall_ok(p != MAP_FAILED, "map pin register");
    auto value=*reinterpret_cast<volatile uint32_t *>(static_cast<char *>(p)+(address&4095));
    munmap(p,4096); return value;
}
void CarrierV11::write32(uint32_t address, uint32_t value) {
    void *p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,memory_,address&~4095U);
    syscall_ok(p != MAP_FAILED, "map pin register");
    *reinterpret_cast<volatile uint32_t *>(static_cast<char *>(p)+(address&4095))=value;
    __sync_synchronize();
    munmap(p,4096);
    check(read32(address)==value,"pin register readback mismatch");
}
void CarrierV11::mux(uint32_t address, unsigned function) {
    write32(address,(read32(address)&~(7U<<16))|(function<<16));
}
void CarrierV11::acquire() {
    utsname system{};
    syscall_ok(uname(&system)==0,"uname");
    check(std::string(system.machine)=="aarch64", "V1.1 hardware access requires the AArch64 board");
    check(geteuid()==0,"root is required for V1.1 pinmux and bus lease");
    check(memory_<0,"carrier already acquired");
    lock_=::open("/run/lock/maixcam2-dual-imu.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    syscall_ok(lock_>=0,"open IMU lock");
    syscall_ok(flock(lock_,LOCK_EX|LOCK_NB)==0,"another dual IMU process is running");
    // Refuse existing bus users and bound I2C clients before touching pinmux.
    for (const auto &p:fs::directory_iterator("/proc")) {
        auto name=p.path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789")!=std::string::npos) continue;
        std::error_code ec;
        for (fs::directory_iterator f(p.path()/"fd",ec),end;!ec && f!=end;f.increment(ec)) {
            std::error_code e;
            auto target=fs::read_symlink(f->path(),e).string();
            if (!e && (target.rfind("/dev/spidev",0)==0 || target=="/dev/i2c-1" || target=="/dev/i2c-6"))
                throw std::runtime_error("bus already open by PID "+name+": "+target);
        }
    }
    for (const auto &d:fs::directory_iterator("/sys/bus/i2c/devices")) {
        auto name=d.path().filename().string();
        if ((name.rfind("1-",0)==0 || name.rfind("6-",0)==0) && fs::exists(d.path()/"driver"))
            throw std::runtime_error("I2C client still bound: "+name);
    }
    memory_=::open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);
    syscall_ok(memory_>=0,"open /dev/mem");
    check(!(read32(0x06061008)&1) && !(read32(0x0606101c)&1),"PWM4/5 active on BMI270 data pins");
    const std::vector<std::pair<uint32_t,unsigned>> targets={
        {0x02300024,2},{0x0230000c,2},{0x02300018,2},{0x0230003c,2},
        {0x104f00cc,6},{0x104f00d8,6},{0x104f00e4,6},{0x104f00f0,6},
        {0x02304030,6},{0x02304084,1},{0x02304090,1},{0x02304024,1}};
    for (auto [address,function]:targets) {
        (void)function;
        pads_.push_back({address,read32(address)});
        evidence["pinmux_before"].push_back({{"address",address},{"value",read32(address)}});
    }
    for (const auto *node:{"4851000.i2c","4856000.i2c"}) {
        fs::path path=fs::path("/sys/bus/platform/devices")/node/"driver";
        check(fs::exists(path),std::string("expected I2C controller missing/unbound: ")+node);
        auto driver=fs::canonical(path).string();
        sysfs_write(fs::path(driver)/"unbind",node);
        detached_.push_back({driver,node});
    }
    cs_=request_gpio("/dev/gpiochip0",{27},true);
    alternate_=request_gpio("/dev/gpiochip1",{18,19,20,21},false);
    for (auto [address,function]:targets) mux(address,function);
    evidence["carrier"]="V1.1";
    evidence["icm_transport"]="spidev1.0 hardware CS0/A2";
    evidence["bmi_transport"]="spidev2.1 clock/data, GPIO A27 CS; B18/B19/B20/B21 isolated inputs";
    evidence["kernel"]=system.release;
    evidence["hostname"]=system.nodename;
}
void CarrierV11::bmi_select(bool active) {
    check(cs_>=0,"BMI software CS not acquired");
    gpiohandle_data d{}; d.values[0]=active?0:1;
    syscall_ok(ioctl(cs_,GPIOHANDLE_SET_LINE_VALUES_IOCTL,&d)==0,"BMI software CS");
}
std::vector<std::string> CarrierV11::restore() {
    std::vector<std::string> errors;
    auto attempt=[&](auto operation){try{operation();}catch(const std::exception &e){errors.push_back(e.what());}};
    if(cs_>=0) attempt([&]{bmi_select(false);});
    for(auto i=pads_.rbegin();i!=pads_.rend();++i) attempt([&]{write32(i->first,i->second);});
    if(cs_>=0){int fd=cs_;cs_=-1;attempt([&]{release_cs_to_input(fd);});}
    if(alternate_>=0){::close(alternate_);alternate_=-1;}
    for(auto i=detached_.rbegin();i!=detached_.rend();++i)
        attempt([&]{sysfs_write(fs::path(i->first)/"bind",i->second);});
    detached_.clear();
    for(const auto &pad:pads_) {
        const auto address=pad.first;
        const auto value=pad.second;
        attempt([&]{
            auto actual=read32(address);
            evidence["pinmux_after"].push_back({{"address",address},{"value",actual}});
            check(actual==value,"pinmux restore mismatch at "+std::to_string(address));
        });
    }
    pads_.clear();
    if(memory_>=0){::close(memory_);memory_=-1;}
    if(lock_>=0){::close(lock_);lock_=-1;}
    return errors;
}
CarrierV11::~CarrierV11(){for(const auto &e:restore())std::cerr<<"cleanup: "<<e<<'\n';}

Spi::Spi(const std::string &path,CarrierV11 *software_cs):cs_(software_cs) {
    fd_=::open(path.c_str(),O_RDWR|O_CLOEXEC);
    syscall_ok(fd_>=0,"open "+path);
    try {
        syscall_ok(ioctl(fd_,SPI_IOC_RD_MODE32,&old_mode_)==0,"read SPI mode");
        syscall_ok(ioctl(fd_,SPI_IOC_RD_BITS_PER_WORD,&old_bits_)==0,"read SPI bits");
        syscall_ok(ioctl(fd_,SPI_IOC_RD_MAX_SPEED_HZ,&old_speed_)==0,"read SPI speed");
        saved_=true;
        // AX kernel rejects SPI_NO_CS. The carrier lease isolates physical CS1.
        uint32_t mode=SPI_MODE_0,speed=1000000;uint8_t bits=8;
        syscall_ok(ioctl(fd_,SPI_IOC_WR_MODE32,&mode)==0,"set SPI mode 0");
        syscall_ok(ioctl(fd_,SPI_IOC_WR_BITS_PER_WORD,&bits)==0,"set SPI bits");
        syscall_ok(ioctl(fd_,SPI_IOC_WR_MAX_SPEED_HZ,&speed)==0,"set SPI speed");
    } catch(...){try{close();}catch(...){}throw;}
}
void Spi::close() {
    if(fd_<0)return;
    int failure=0;
    if(saved_) {
        if(ioctl(fd_,SPI_IOC_WR_MODE32,&old_mode_)<0)failure=errno;
        if(ioctl(fd_,SPI_IOC_WR_BITS_PER_WORD,&old_bits_)<0)failure=errno;
        if(ioctl(fd_,SPI_IOC_WR_MAX_SPEED_HZ,&old_speed_)<0)failure=errno;
    }
    ::close(fd_);fd_=-1;errno=failure;
    syscall_ok(!failure,"restore SPI settings");
}
Spi::~Spi(){try{close();}catch(const std::exception &e){std::cerr<<e.what()<<'\n';}}
std::vector<uint8_t> Spi::transfer(const std::vector<uint8_t> &tx) {
    check(!tx.empty() && tx.size()<=2048,"SPI transaction length out of bounds");
    std::vector<uint8_t> rx(tx.size());
    spi_ioc_transfer x{};
    x.tx_buf=reinterpret_cast<uintptr_t>(tx.data());
    x.rx_buf=reinterpret_cast<uintptr_t>(rx.data());
    x.len=tx.size();x.speed_hz=1000000;x.bits_per_word=8;
    ++transactions;
    if(cs_)cs_->bmi_select(true);
    int result=ioctl(fd_,SPI_IOC_MESSAGE(1),&x);int error=errno;
    if(cs_)cs_->bmi_select(false);
    if(result!=static_cast<int>(tx.size())){++errors;errno=error;syscall_ok(false,"SPI transfer failed/short");}
    if(cs_)delay_us(post_transfer_delay_us);
    return rx;
}
std::vector<uint8_t> Spi::read(uint8_t reg,size_t count,size_t dummy) {
    check(count>0 && count+dummy+1<=2048,"SPI read length out of bounds");
    std::vector<uint8_t> tx(1+dummy+count);tx[0]=reg|0x80;
    auto rx=transfer(tx);return {rx.begin()+1+dummy,rx.end()};
}
uint8_t Spi::byte(uint8_t reg,size_t dummy){return read(reg,1,dummy)[0];}
void Spi::write(uint8_t reg,const uint8_t *data,size_t size) {
    check(size>0 && size<2048,"SPI write length out of bounds");
    std::vector<uint8_t> tx(1+size);tx[0]=reg&0x7f;
    std::copy(data,data+size,tx.begin()+1);transfer(tx);
}
void Spi::write(uint8_t reg,uint8_t value){write(reg,&value,1);}
}
