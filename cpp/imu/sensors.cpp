#include "sensors.hpp"
#include "bmi2.h"
#include "bmi270.h"
#include <algorithm>
#include <iostream>

namespace dual_imu {
uint8_t Icm42688::id(){return spi_.byte(0x75);}
void Icm42688::initialize() {
    check(id()==0x47,"ICM WHO_AM_I mismatch");
    check((spi_.byte(0x76)&7)==0,"ICM must start in bank 0");
    for(uint8_t reg:{0x16,0x4c,0x4d,0x4e,0x4f,0x50,0x52})saved_[reg]=spi_.byte(reg);
    check(!(saved_.at(0x4e)&15) && !(saved_.at(0x16)&0xc0),"ICM already sampling; refusing to replace its configuration");
    modified_=true;
    spi_.write(0x4c,uint8_t((saved_.at(0x4c)&0xcc)|0x33)); // big endian data, SPI only
    spi_.write(0x4d,uint8_t((saved_.at(0x4d)&0xf8)|1)); // internal clock, no external CLKIN
    spi_.write(0x4f,0x47); // +/-500 dps, 200 Hz
    spi_.write(0x50,0x47); // +/-4 g, 200 Hz
    spi_.write(0x52,0x44); // UI filters ODR/10
    for(uint8_t reg:{0x4f,0x50})check(spi_.byte(reg)==0x47,"ICM configuration readback mismatch");
    spi_.write(0x4e,0x0f); // accel+gyro low noise; no writes for at least 200 us
    delay_us(60000);
    configuration={{"who_am_i",id()},{"odr_hz",200},{"accel_range_g",4},{"gyro_range_dps",500}};
    configuration["self_test_config"]=spi_.byte(0x70);
    check(configuration["self_test_config"]==0,"ICM self-test excitation is enabled");
    spi_.write(0x76,4);
    try {
        for(uint8_t reg=0x77;reg<=0x7f;++reg)configuration["user_offset_registers"].push_back(spi_.byte(reg));
    } catch(...) {spi_.write(0x76,0);throw;}
    spi_.write(0x76,0);
    for(uint8_t reg:{0x4c,0x4d,0x4e,0x4f,0x50,0x52})
        configuration["registers"].push_back({{"address",reg},{"value",spi_.byte(reg)}});
}
bool Icm42688::ready(){return (spi_.byte(0x2d)&8)!=0;}
Sample Icm42688::read(){return decode_icm(spi_.read(0x1d,14));}
void Icm42688::stop() {
    if(!modified_)return;
    spi_.write(0x4e,0);delay_us(50000);
    for(auto [reg,value]:saved_)if(reg!=0x4e)spi_.write(reg,value);
    spi_.write(0x4e,saved_.at(0x4e));
    for(auto [reg,value]:saved_)check(spi_.byte(reg)==value,"ICM restore readback mismatch");
    modified_=false;
}
Icm42688::~Icm42688(){try{stop();}catch(const std::exception &e){std::cerr<<"ICM cleanup: "<<e.what()<<'\n';}}

Bmi270::Bmi270(Spi &spi):spi_(spi) {
    device_.intf=BMI2_SPI_INTF;device_.intf_ptr=&spi_;
    device_.read=read_callback;device_.write=write_callback;device_.delay_us=delay_callback;
    device_.read_write_len=256; // Even chunks, below Linux spidev buffer limit.
    device_.dummy_byte=1;
}
BMI2_INTF_RETURN_TYPE Bmi270::read_callback(uint8_t reg,uint8_t *data,uint32_t len,void *context) {
    auto &spi=*static_cast<Spi *>(context);
    try {
        // Bosch already includes its dummy byte in len and removes it itself.
        std::vector<uint8_t> tx(1+len);tx[0]=reg|0x80;
        auto rx=spi.transfer(tx);std::copy(rx.begin()+1,rx.end(),data);return BMI2_INTF_RET_SUCCESS;
    } catch(const std::exception &e){spi.callback_error=e.what();return -1;}
}
BMI2_INTF_RETURN_TYPE Bmi270::write_callback(uint8_t reg,const uint8_t *data,uint32_t len,void *context) {
    auto &spi=*static_cast<Spi *>(context);
    try{spi.write(reg,data,len);return BMI2_INTF_RET_SUCCESS;}
    catch(const std::exception &e){spi.callback_error=e.what();return -1;}
}
void Bmi270::delay_callback(uint32_t us,void *){delay_us(us);}
void Bmi270::result(int8_t value,const char *operation) {
    check(value==BMI2_OK,std::string(operation)+" failed: "+std::to_string(int(value))+" "+spi_.callback_error);
}
uint8_t Bmi270::id(){return spi_.byte(0x00,1);}
void Bmi270::initialize() {
    (void)id();delay_us(2000); // Select SPI after POR; discard the first response.
    check(id()==0x24,"BMI270 CHIP_ID mismatch");
    check((spi_.byte(0x7d,1)&15)==0,"BMI270 already sampling; refusing to reset");
    check((spi_.byte(0x21,1)&15)==0,"BMI270 already initialized; preserve its existing configuration");
    modified_=true;
    result(bmi270_init(&device_),"bmi270_init");
    auto internal=spi_.byte(0x21,1);
    check((internal&15)==1,"BMI270 config initialization did not report init_ok");
    result(bmi2_set_adv_power_save(BMI2_DISABLE,&device_),"disable BMI power saving");
    spi_.post_transfer_delay_us=2;
    bmi2_sens_config config[2]{};
    config[0].type=BMI2_ACCEL;config[1].type=BMI2_GYRO;
    result(bmi2_get_sensor_config(config,2,&device_),"get BMI configuration");
    config[0].cfg.acc.odr=BMI2_ACC_ODR_200HZ;
    config[0].cfg.acc.bwp=BMI2_ACC_NORMAL_AVG4;
    config[0].cfg.acc.filter_perf=BMI2_PERF_OPT_MODE;
    config[0].cfg.acc.range=BMI2_ACC_RANGE_4G;
    config[1].cfg.gyr.odr=BMI2_GYR_ODR_200HZ;
    config[1].cfg.gyr.bwp=BMI2_GYR_NORMAL_MODE;
    config[1].cfg.gyr.filter_perf=BMI2_PERF_OPT_MODE;
    config[1].cfg.gyr.noise_perf=BMI2_PERF_OPT_MODE;
    config[1].cfg.gyr.range=BMI2_GYR_RANGE_500;
    result(bmi2_set_sensor_config(config,2,&device_),"set BMI configuration");
    uint8_t sensors[2]={BMI2_ACCEL,BMI2_GYRO};
    result(bmi2_sensor_enable(sensors,2,&device_),"enable BMI accel and gyro");
    delay_us(100000);
    bmi2_sens_config readback[2]{};readback[0].type=BMI2_ACCEL;readback[1].type=BMI2_GYRO;
    result(bmi2_get_sensor_config(readback,2,&device_),"read back BMI configuration");
    check(readback[0].cfg.acc.odr==BMI2_ACC_ODR_200HZ && readback[0].cfg.acc.range==BMI2_ACC_RANGE_4G &&
          readback[1].cfg.gyr.odr==BMI2_GYR_ODR_200HZ && readback[1].cfg.gyr.range==BMI2_GYR_RANGE_500,
          "BMI ODR/range readback mismatch");
    configuration={{"chip_id",id()},{"internal_status",internal},{"config_bytes",device_.config_size},
                   {"odr_hz",200},{"accel_range_g",4},{"gyro_range_dps",500},
                   {"gyro_software_cross_axis_correction",false}};
    for(uint8_t reg:{0x02,0x21,0x40,0x41,0x42,0x43,0x7c,0x7d})
        configuration["registers"].push_back({{"address",reg},{"value",spi_.byte(reg,1)}});
    check(spi_.byte(0x02,1)==0,"BMI error register nonzero after initialization");
}
bool Bmi270::ready(){return (spi_.byte(0x03,1)&0xc0)==0xc0;}
Sample Bmi270::read(){return decode_bmi(spi_.read(0x0c,15,1),spi_.read(0x22,2,1));}
void Bmi270::stop() {
    if(!modified_)return;
    // Bosch bmi2_soft_reset() also reloads the firmware. For cleanup issue
    // the documented volatile reset command directly, leaving sensors suspended.
    spi_.post_transfer_delay_us=450;
    spi_.write(0x7e,0xb6);
    delay_us(2000);
    (void)id();delay_us(2000); // Re-select SPI after reset before checking registers.
    check(id()==0x24 && (spi_.byte(0x7d,1)&15)==0 && (spi_.byte(0x21,1)&15)==0,
          "BMI cleanup did not return to uninitialized suspend");
    modified_=false;
}
Bmi270::~Bmi270(){try{stop();}catch(const std::exception &e){std::cerr<<"BMI cleanup: "<<e.what()<<'\n';}}
}
