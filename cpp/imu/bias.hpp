#pragma once
#include "sample.hpp"
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>

namespace dual_imu {
// A constant estimate from a separate stationary interval. Never updated while
// measuring: slow real rotation must not be silently learned as sensor bias.
struct GyroBias {
    std::array<double,3> dps{};
    double temperature_c=0;
    bool enabled=false;
    std::array<double,3> correct(const Sample &sample)const {
        if(enabled && (!std::isfinite(sample.temperature) ||
                       std::abs(sample.temperature-temperature_c)>3.0))
            throw std::runtime_error("gyro calibration temperature differs by more than 3 C; recalibrate");
        std::array<double,3> result{};
        for(size_t i=0;i<3;++i)result[i]=sample.values[i+3]-(enabled?dps[i]:0);
        return result;
    }
    nlohmann::json json()const {
        return {{"bias_dps",dps},{"reference_temperature_c",temperature_c}};
    }
    static GyroBias from_json(const nlohmann::json &value) {
        GyroBias result;
        const auto &bias=value.at("bias_dps");
        if(!bias.is_array() || bias.size()!=3)throw std::runtime_error("bias needs exactly three axes");
        for(size_t i=0;i<3;++i) {
            result.dps[i]=bias.at(i).get<double>();
            if(!std::isfinite(result.dps[i]) || std::abs(result.dps[i])>10)
                throw std::runtime_error("invalid gyro bias");
        }
        result.temperature_c=value.at("reference_temperature_c").get<double>();
        if(!std::isfinite(result.temperature_c) || result.temperature_c< -40 || result.temperature_c>85)
            throw std::runtime_error("invalid calibration temperature");
        result.enabled=true;
        return result;
    }
};

struct BiasMoments {
    uint64_t count=0;
    std::array<double,7> mean{},m2{};
    void add(const Sample &sample) {
        ++count;
        for(size_t i=0;i<7;++i) {
            const double value=i<6?sample.values[i]:sample.temperature;
            const double delta=value-mean[i];
            mean[i]+=delta/count;m2[i]+=delta*(value-mean[i]);
        }
    }
    double stddev(size_t axis)const{return std::sqrt(m2[axis]/(count>1?count-1:1));}
};

struct StationaryWindow {
    BiasMoments all;
    std::map<unsigned,BiasMoments> blocks;
    uint64_t invalid=0;
    double min_norm=std::numeric_limits<double>::infinity(),max_norm=0;
    double min_temp=std::numeric_limits<double>::infinity(),max_temp=-min_temp;
    void add(const Sample &sample,double seconds) {
        bool valid=sample.valid && !sample.saturated && std::isfinite(sample.temperature);
        for(auto value:sample.values)valid &= std::isfinite(value);
        if(!valid){++invalid;return;}
        all.add(sample);blocks[static_cast<unsigned>(seconds/5)].add(sample);
        double norm=0;for(size_t i=0;i<3;++i)norm+=sample.values[i]*sample.values[i];
        norm=std::sqrt(norm);min_norm=std::min(min_norm,norm);max_norm=std::max(max_norm,norm);
        min_temp=std::min(min_temp,sample.temperature);max_temp=std::max(max_temp,sample.temperature);
    }
    // Practical acceptance limits for this static calibration, not datasheet
    // specifications. A stationary operator is still needed: constant slow
    // rotation is not distinguishable from bias with these checks alone.
    nlohmann::json quality(double seconds)const {
        std::vector<std::string> errors;
        auto require=[&](bool ok,const char *why){if(!ok)errors.emplace_back(why);};
        require(seconds>=10 && all.count>=seconds*120,"too few stationary samples");
        require(invalid==0,"invalid or saturated samples");
        require(min_norm>=0.9 && max_norm<=1.1,"acceleration magnitude outside 0.9..1.1 g");
        require(max_temp-min_temp<=2,"temperature span exceeds 2 C");
        std::array<double,3> gyro_std{},accel_std{},span{};
        nlohmann::json windows=nlohmann::json::array();
        for(size_t axis=0;axis<3;++axis) {
            gyro_std[axis]=all.stddev(axis+3);accel_std[axis]=all.stddev(axis);
            require(gyro_std[axis]<=0.2,"gyro standard deviation exceeds 0.2 dps");
            require(accel_std[axis]<=0.01,"accel standard deviation exceeds 0.01 g");
            require(std::abs(all.mean[axis+3])<=10,"gyro mean exceeds 10 dps");
            double low=std::numeric_limits<double>::infinity(),high=-low;
            size_t full=0;
            for(const auto &entry:blocks) {
                if(entry.first*5+5>seconds+0.001)continue;
                if(axis==0)require(entry.second.count>=600,"too few samples in a 5 s block");
                low=std::min(low,entry.second.mean[axis+3]);high=std::max(high,entry.second.mean[axis+3]);++full;
            }
            require(full>=2,"fewer than two full 5 s blocks");
            span[axis]=full?high-low:0;
            require(span[axis]<=0.08,"gyro 5 s means span exceeds 0.08 dps");
        }
        for(const auto &entry:blocks)windows.push_back({{"start_seconds",entry.first*5},
            {"samples",entry.second.count},{"mean",entry.second.mean}});
        return {{"accepted",errors.empty()},{"errors",errors},{"samples",all.count},
                {"gyro_stddev_dps",gyro_std},{"accel_stddev_g",accel_std},
                {"gyro_5s_mean_span_dps",span},{"temperature_min_c",all.count?min_temp:0},
                {"temperature_max_c",all.count?max_temp:0},{"blocks",windows},
                {"limits",{{"gyro_stddev_dps",0.2},{"accel_stddev_g",0.01},
                           {"gyro_5s_mean_span_dps",0.08},{"temperature_span_c",2.0},
                           {"accel_norm_g",{0.9,1.1}},{"minimum_received_hz",120}}}};
    }
    GyroBias estimate(double seconds)const {
        if(!quality(seconds).at("accepted").get<bool>())throw std::runtime_error("stationary calibration rejected");
        GyroBias bias;
        for(size_t i=0;i<3;++i)bias.dps[i]=all.mean[i+3];
        bias.temperature_c=all.mean[6];bias.enabled=true;
        return bias;
    }
};
}
