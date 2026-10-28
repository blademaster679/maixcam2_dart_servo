#include "../imu/sample.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace dual_imu;
static void expect(bool ok){if(!ok)throw std::runtime_error("IMU decode regression");}
int main() {
    // Independent wire-format fixtures: +1g X, -1g Y, +2g Z; signed gyro values.
    auto icm=decode_icm({0,0, 0x20,0, 0xe0,0, 0x40,0, 0,0x83, 0xff,0x7d, 0,0});
    expect(icm.values[0]==1 && icm.values[1]==-1 && icm.values[2]==2);
    expect(icm.values[3]==2 && icm.values[4]==-2 && icm.temperature==25 && icm.valid);
    auto bmi=decode_bmi({0,0x20, 0,0xe0, 0,0x40, 0,1, 0,0xff, 0,0, 0x56,0x34,0x12},{0,4});
    expect(bmi.values[0]==1 && bmi.values[1]==-1 && bmi.values[2]==2);
    expect(std::abs(bmi.values[3]-3.90625)<1e-9 && std::abs(bmi.values[4]+3.90625)<1e-9);
    expect(bmi.sensor_time==0x123456 && bmi.temperature==25 && bmi.valid);
    expect(!decode_bmi(std::vector<uint8_t>(15),{0,0x80}).valid);
    expect(!decode_icm(std::vector<uint8_t>(14)).valid);
    bool threw=false;try{decode_icm({0});}catch(const std::invalid_argument &){threw=true;}expect(threw);
    expect(signed16(0x80,0)==-32768 && signed16(0x7f,0xff)==32767);
    std::cout<<"IMU wire format, scales, temperature and invalid-frame checks passed\n";
}
