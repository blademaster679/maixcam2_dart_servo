#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dual_imu {
struct Sample {
    std::array<int16_t,6> raw{};
    std::array<double,6> values{}; // ax/ay/az in g; gx/gy/gz in deg/s
    double temperature = 0;
    uint32_t sensor_time = 0; // BMI free-running 24-bit counter, not a sample sequence
    bool valid = true;
    bool saturated = false;
};
inline int16_t signed16(uint8_t high,uint8_t low) {
    uint16_t value=(uint16_t(high)<<8)|low;
    return static_cast<int16_t>(value<32768 ? int(value) : int(value)-65536);
}
inline void scale_sample(Sample &sample,double gyro_lsb_per_dps) {
    bool all_zero=true;
    for(size_t i=0;i<6;++i) {
        sample.values[i]=sample.raw[i]/(i<3 ? 8192.0 : gyro_lsb_per_dps);
        all_zero &= sample.raw[i]==0;
        sample.saturated |= sample.raw[i]==-32768 || sample.raw[i]==32767;
    }
    sample.valid &= !all_zero;
}
inline Sample decode_icm(const std::vector<uint8_t> &data) {
    if(data.size()!=14)throw std::invalid_argument("ICM sample needs 14 bytes");
    Sample sample;
    sample.temperature=25.0+signed16(data[0],data[1])/132.48;
    for(size_t i=0;i<6;++i)sample.raw[i]=signed16(data[2+2*i],data[3+2*i]);
    scale_sample(sample,65.5);
    return sample;
}
inline Sample decode_bmi(const std::vector<uint8_t> &data,const std::vector<uint8_t> &temp) {
    if(data.size()!=15 || temp.size()!=2)throw std::invalid_argument("BMI sample needs 15+2 bytes");
    Sample sample;
    for(size_t i=0;i<6;++i)sample.raw[i]=signed16(data[1+2*i],data[2*i]);
    sample.sensor_time=uint32_t(data[12])|(uint32_t(data[13])<<8)|(uint32_t(data[14])<<16);
    auto raw_temp=signed16(temp[1],temp[0]);
    sample.valid=raw_temp!=-32768;
    sample.temperature=23.0+raw_temp/512.0;
    scale_sample(sample,65.536);
    return sample;
}
}
