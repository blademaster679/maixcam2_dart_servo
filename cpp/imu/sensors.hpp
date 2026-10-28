#pragma once
#include "linux_bus.hpp"
#include "sample.hpp"
#include "bmi2_defs.h"
#include <map>

namespace dual_imu {
class Icm42688 {
public:
    explicit Icm42688(Spi &spi):spi_(spi){}
    ~Icm42688();
    uint8_t id();
    void initialize();
    bool ready();
    Sample read();
    void stop();
    Json configuration;
private:
    Spi &spi_;
    std::map<uint8_t,uint8_t> saved_;
    bool modified_=false;
};
class Bmi270 {
public:
    explicit Bmi270(Spi &spi);
    ~Bmi270();
    uint8_t id();
    void initialize();
    bool ready();
    Sample read();
    void stop();
    Json configuration;
private:
    Spi &spi_;
    bmi2_dev device_{};
    bool modified_=false;
    void result(int8_t value,const char *operation);
    static BMI2_INTF_RETURN_TYPE read_callback(uint8_t reg,uint8_t *data,uint32_t len,void *context);
    static BMI2_INTF_RETURN_TYPE write_callback(uint8_t reg,const uint8_t *data,uint32_t len,void *context);
    static void delay_callback(uint32_t us,void *context);
};
}
