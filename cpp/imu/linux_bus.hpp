#pragma once
#include "json.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace dual_imu {
using Json = nlohmann::json;
void check(bool ok, const std::string &message);
void delay_us(unsigned us);
uint64_t monotonic_ns();

// Runtime lease for this carrier only. No device-tree or boot-file changes.
class CarrierV11 {
public:
    CarrierV11() = default;
    ~CarrierV11();
    CarrierV11(const CarrierV11 &) = delete;
    CarrierV11 &operator=(const CarrierV11 &) = delete;
    void acquire();
    void bmi_select(bool active);
    std::vector<std::string> restore();
    Json evidence = Json::object();
private:
    int memory_ = -1, lock_ = -1, cs_ = -1, alternate_ = -1;
    std::vector<std::pair<uint32_t,uint32_t>> pads_;
    std::vector<std::pair<std::string,std::string>> detached_;
    uint32_t read32(uint32_t address);
    void write32(uint32_t address, uint32_t value);
    void mux(uint32_t address, unsigned function);
};

class Spi {
public:
    explicit Spi(const std::string &path, CarrierV11 *software_cs = nullptr);
    ~Spi();
    Spi(const Spi &) = delete;
    Spi &operator=(const Spi &) = delete;
    std::vector<uint8_t> transfer(const std::vector<uint8_t> &tx);
    std::vector<uint8_t> read(uint8_t reg, size_t count, size_t dummy = 0);
    uint8_t byte(uint8_t reg, size_t dummy = 0);
    void write(uint8_t reg, const uint8_t *data, size_t size);
    void write(uint8_t reg, uint8_t value);
    void close();
    uint64_t transactions = 0;
    uint64_t errors = 0;
    std::string callback_error;
    unsigned post_transfer_delay_us = 450; // BMI suspend/APS accesses need a longer gap.
private:
    int fd_ = -1;
    uint32_t old_mode_ = 0, old_speed_ = 0;
    uint8_t old_bits_ = 0;
    CarrierV11 *cs_;
    bool saved_ = false;
};
}
