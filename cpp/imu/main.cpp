#include "bias.hpp"
#include "sensors.hpp"
#include <array>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>

using namespace dual_imu;
namespace fs = std::filesystem;
static volatile std::sig_atomic_t interrupted = 0;
static void signal_handler(int) { interrupted = 1; }
struct Stream {
  std::ofstream file;
  uint64_t count = 0, invalid = 0, saturated = 0, changes = 0, previous_ns = 0;
  double max_gap_ms = 0, min_norm = 1e99, max_norm = 0;
  std::array<double, 7> mean{}, m2{};
  std::array<int16_t, 6> previous{};
  GyroBias bias;
  StationaryWindow stationary;
  void open(const fs::path &path) {
    file.open(path);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << "host_start_ns,host_end_ns,sensor_time,ax_raw,ay_raw,az_raw,gx_raw,"
            "gy_raw,gz_raw,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,temp_c,valid,"
            "saturated,gx_corrected_dps,gy_corrected_dps,gz_corrected_dps,gyro_"
            "bias_applied\n";
    file << std::setprecision(10);
  }
  void append(const Sample &sample, uint64_t start, uint64_t end,
              double seconds) {
    const auto corrected = bias.correct(sample);
    stationary.add(sample, seconds);
    ++count;
    invalid += !sample.valid;
    saturated += sample.saturated;
    if (count > 1 && sample.raw != previous)
      ++changes;
    previous = sample.raw;
    if (previous_ns)
      max_gap_ms = std::max(max_gap_ms, (end - previous_ns) / 1e6);
    previous_ns = end;
    double norm = std::sqrt(sample.values[0] * sample.values[0] +
                            sample.values[1] * sample.values[1] +
                            sample.values[2] * sample.values[2]);
    min_norm = std::min(min_norm, norm);
    max_norm = std::max(max_norm, norm);
    for (size_t i = 0; i < 7; ++i) {
      double value = i < 6 ? sample.values[i] : sample.temperature;
      double delta = value - mean[i];
      mean[i] += delta / count;
      m2[i] += delta * (value - mean[i]);
    }
    file << start << ',' << end << ',' << sample.sensor_time;
    for (auto value : sample.raw)
      file << ',' << value;
    for (auto value : sample.values)
      file << ',' << value;
    file << ',' << sample.temperature << ',' << sample.valid << ','
         << sample.saturated;
    for (auto value : corrected)
      file << ',' << value;
    file << ',' << bias.enabled << '\n';
  }
  Json summary(double seconds) const {
    std::array<double, 7> stddev{};
    for (size_t i = 0; i < 7; ++i)
      stddev[i] = std::sqrt(m2[i] / (count > 1 ? count - 1 : 1));
    std::array<double, 3> corrected{}, gyro_std{};
    for (size_t i = 0; i < 3; ++i) {
      corrected[i] = mean[i + 3] - (bias.enabled ? bias.dps[i] : 0);
      gyro_std[i] = stddev[i + 3];
    }
    return {{"samples", count},
            {"received_hz", seconds > 0 ? count / seconds : 0},
            {"invalid_samples", invalid},
            {"saturated_samples", saturated},
            {"changed_samples", changes},
            {"max_host_gap_ms", max_gap_ms},
            {"mean", mean},
            {"stddev", stddev},
            {"accel_norm_min_g", count ? min_norm : 0},
            {"accel_norm_max_g", max_norm},
            {"columns",
             {"ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps", "temp_c"}},
            {"gyro_correction",
             {{"applied", bias.enabled},
              {"bias_dps", bias.dps},
              {"mean_dps", corrected},
              {"stddev_dps", gyro_std}}}};
  }
};
static void save(const fs::path &path, const Json &value) {
  std::ofstream file(path.string() + ".tmp");
  file.exceptions(std::ios::badbit | std::ios::failbit);
  file << value.dump(2) << '\n';
  file.close();
  fs::rename(path.string() + ".tmp", path);
}
int main(int argc, char **argv) {
  fs::path out, calibration_path;
  double requested = 30, warmup = -1, calibration_seconds = 60;
  bool probe = false, run = false, calibrate = false,
       stationary_confirmed = false;
  Json profile;
  const Json calibration_settings = {{"odr_hz", 200},
                                     {"accel_range_g", 4},
                                     {"gyro_range_dps", 500},
                                     {"icm_gyro_lsb_per_dps", 65.5},
                                     {"bmi_gyro_lsb_per_dps", 65.536}};
  try {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << "V1.1 dual IMU: dual_imu --run|--probe --out "
                     "NEW_DIRECTORY [--seconds 1..600]\n"
                     "  --calibrate --stationary-confirmed "
                     "[--calibration-seconds 10..600]\n"
                     "  --calibration FILE: load a fixed calibration from this "
                     "board\n"
                     "  --warmup-seconds 0..600 (default 60 with calibration, "
                     "otherwise 0)\n"
                     "  --stationary-confirmed: validate static residual bias "
                     "(at least 10 seconds)\n"
                     "Linux/A53, SPI1 CS0 and SPI2 with A27 software CS. "
                     "Samples +/-4g, +/-500dps, 200Hz.\n";
        return 0;
      } else if (arg == "--probe")
        probe = true;
      else if (arg == "--run")
        run = true;
      else if (arg == "--calibrate")
        calibrate = true;
      else if (arg == "--stationary-confirmed")
        stationary_confirmed = true;
      else if (arg == "--calibration" && i + 1 < argc)
        calibration_path = argv[++i];
      else if (arg == "--out" && i + 1 < argc)
        out = argv[++i];
      else if ((arg == "--seconds" || arg == "--warmup-seconds" ||
                arg == "--calibration-seconds") &&
               i + 1 < argc) {
        std::string text = argv[++i];
        size_t consumed = 0;
        double value = std::stod(text, &consumed);
        check(consumed == text.size() && std::isfinite(value),
              "invalid duration");
        if (arg == "--seconds")
          requested = value;
        else if (arg == "--warmup-seconds")
          warmup = value;
        else
          calibration_seconds = value;
      } else
        throw std::runtime_error("unknown/incomplete argument: " + arg);
    }
    check(probe != run, "choose exactly one of --probe or --run");
    check(!(calibrate && !calibration_path.empty()),
          "choose --calibrate or --calibration, not both");
    check(!calibrate || stationary_confirmed,
          "calibration requires --stationary-confirmed");
    check(!probe ||
              (!calibrate && calibration_path.empty() && !stationary_confirmed),
          "calibration requires --run");
    if (warmup == -1)
      warmup = (calibrate || !calibration_path.empty()) ? 60 : 0;
    check(warmup >= 0 && warmup <= 600, "warmup seconds must be 0..600");
    check(!calibrate || warmup >= 30,
          "new calibration requires at least 30 seconds warmup");
    check(calibration_seconds >= 10 && calibration_seconds <= 600,
          "calibration seconds must be 10..600");
    check(std::isfinite(requested) && requested >= 1 && requested <= 600,
          "seconds must be 1..600");
    check(!stationary_confirmed || requested >= 10,
          "static validation needs at least 10 seconds");
    if (!calibration_path.empty()) {
      std::ifstream input(calibration_path);
      check(bool(input), "cannot open calibration file");
      input >> profile;
      check(profile.at("schema_version") == 1 &&
                profile.at("accepted") == true &&
                profile.at("method") == "stationary_mean_subtraction" &&
                profile.at("settings") == calibration_settings,
            "incompatible calibration profile");
      (void)GyroBias::from_json(profile.at("icm42688"));
      (void)GyroBias::from_json(profile.at("bmi270"));
    }
    check(!out.empty() && !fs::exists(out), "--out must name a new directory");
    check(fs::create_directories(out), "cannot create output directory");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGHUP, signal_handler);
  std::signal(SIGPIPE, SIG_IGN);
  Json report = {{"carrier", "V1.1"},
                 {"mode", probe ? "probe" : "capture"},
                 {"requested_seconds", requested},
                 {"passed", false},
                 {"complete", false},
                 {"nominal_odr_hz", 200},
                 {"stationary_confirmed", stationary_confirmed},
                 {"warmup_seconds", warmup},
                 {"sampling", "data-ready polling of registers; not lossless "
                              "FIFO or synchronized sampling"}};
  CarrierV11 board;
  std::unique_ptr<Spi> icm_spi, bmi_spi;
  std::unique_ptr<Icm42688> icm;
  std::unique_ptr<Bmi270> bmi;
  Stream icm_stream, bmi_stream;
  double elapsed = 0;
  bool capture_ok = false;
  std::vector<std::string> cleanup_errors;
  try {
    save(out / "summary.json", report);
    board.acquire();
    if (!profile.is_null())
      check(profile.at("board_hostname") == board.evidence.at("hostname"),
            "calibration belongs to another board");
    icm_spi = std::make_unique<Spi>("/dev/spidev1.0");
    bmi_spi = std::make_unique<Spi>("/dev/spidev2.1", &board);
    icm = std::make_unique<Icm42688>(*icm_spi);
    bmi = std::make_unique<Bmi270>(*bmi_spi);
    (void)bmi->id();
    delay_us(2000);
    unsigned icm_bad = 0, bmi_bad = 0;
    for (int i = 0; i < 100; ++i) {
      icm_bad += icm->id() != 0x47;
      bmi_bad += bmi->id() != 0x24;
    }
    report["identity"] = {{"icm_id", icm->id()},
                          {"bmi_id", bmi->id()},
                          {"reads_each", 100},
                          {"icm_errors", icm_bad},
                          {"bmi_errors", bmi_bad}};
    check(icm_bad == 0 && bmi_bad == 0, "chip ID check failed");
    std::cout << "ICM WHO_AM_I=0x47; BMI CHIP_ID=0x24; 100 reads each passed"
              << std::endl;
    if (!probe) {
      icm->initialize();
      bmi->initialize();
      report["icm_configuration"] = icm->configuration;
      report["bmi_configuration"] = bmi->configuration;
      std::cout << "Both initialized: 200 Hz, +/-4g, +/-500dps; BMI "
                   "configuration loaded"
                << std::endl;
      auto capture = [&](const std::string &phase, const fs::path &directory,
                         double seconds, Stream &a, Stream &b,
                         double &duration) {
        fs::create_directories(directory);
        a.open(directory / "icm42688.csv");
        b.open(directory / "bmi270.csv");
        std::cout << "Phase " << phase << ": " << seconds << " seconds"
                  << std::endl;
        report["active_phase"] = phase;
        save(out / "summary.json", report);
        const auto start = monotonic_ns();
        auto health = start;
        while (!interrupted) {
          auto now = monotonic_ns();
          duration = (now - start) / 1e9;
          if (duration >= seconds)
            break;
          if (icm->ready()) {
            auto begin = monotonic_ns();
            auto sample = icm->read();
            a.append(sample, begin, monotonic_ns(), (begin - start) / 1e9);
          }
          if (bmi->ready()) {
            auto begin = monotonic_ns();
            auto sample = bmi->read();
            b.append(sample, begin, monotonic_ns(), (begin - start) / 1e9);
          }
          now = monotonic_ns();
          check(now - (a.previous_ns ? a.previous_ns : start) < 2000000000ULL,
                "ICM data-ready timeout");
          check(now - (b.previous_ns ? b.previous_ns : start) < 2000000000ULL,
                "BMI data-ready timeout");
          if (now - health >= 1000000000ULL) {
            check(icm->id() == 0x47 && bmi->id() == 0x24,
                  "runtime chip identity mismatch");
            check(bmi_spi->byte(0x02, 1) == 0,
                  "BMI runtime error register is nonzero");
            check((bmi_spi->byte(0x21, 1) & 15) == 1,
                  "BMI lost initialization state");
            std::cout << phase << " t=" << int(duration)
                      << "s samples ICM=" << a.count << " BMI=" << b.count
                      << std::endl;
            health = now;
          }
          delay_us(250);
        }
        a.file.close();
        b.file.close();
        report["phases"][phase] = {{"elapsed_seconds", duration},
                                   {"icm42688", a.summary(duration)},
                                   {"bmi270", b.summary(duration)}};
        check(!interrupted, "interrupted by signal");
        check(a.count >= seconds * 120 && b.count >= seconds * 120,
              "too few fresh samples");
        check(a.invalid == 0 && b.invalid == 0 && a.saturated == 0 &&
                  b.saturated == 0,
              "invalid or saturated samples received");
        check(a.changes > 5 && b.changes > 5, "sensor output did not change");
      };
      if (warmup > 0) {
        Stream a, b;
        double duration = 0;
        capture("warmup", out / "warmup", warmup, a, b, duration);
      }
      if (calibrate) {
        Stream a, b;
        double duration = 0;
        capture("calibration", out / "calibration", calibration_seconds, a, b,
                duration);
        report["calibration_quality"] = {
            {"icm42688", a.stationary.quality(calibration_seconds)},
            {"bmi270", b.stationary.quality(calibration_seconds)}};
        const auto icm_bias = a.stationary.estimate(calibration_seconds);
        const auto bmi_bias = b.stationary.estimate(calibration_seconds);
        profile = {{"schema_version", 1},
                   {"accepted", true},
                   {"method", "stationary_mean_subtraction"},
                   {"board_hostname", board.evidence.at("hostname")},
                   {"settings", calibration_settings},
                   {"icm_configuration", icm->configuration},
                   {"bmi_configuration", bmi->configuration},
                   {"calibration_seconds", calibration_seconds},
                   {"warmup_seconds", warmup},
                   {"icm42688", icm_bias.json()},
                   {"bmi270", bmi_bias.json()},
                   {"quality", report["calibration_quality"]}};
      }
      if (!profile.is_null()) {
        check(profile.at("icm_configuration") == icm->configuration &&
                  profile.at("bmi_configuration") == bmi->configuration,
              "sensor configuration differs from calibration");
        icm_stream.bias = GyroBias::from_json(profile.at("icm42688"));
        bmi_stream.bias = GyroBias::from_json(profile.at("bmi270"));
        report["calibration_profile"] = profile;
        report["calibration_source"] =
            calibrate ? "this_run/calibration" : calibration_path.string();
        save(out / "calibration.json", profile);
      }
      capture("validation", out, requested, icm_stream, bmi_stream, elapsed);
      if (stationary_confirmed) {
        bool bias_ok = true;
        for (const auto &entry :
             std::array<std::pair<const char *, Stream *>, 2>{
                 {{"icm42688", &icm_stream}, {"bmi270", &bmi_stream}}}) {
          auto quality = entry.second->stationary.quality(requested);
          bool accepted = quality.at("accepted").get<bool>();
          if (entry.second->bias.enabled)
            for (size_t i = 0; i < 3; ++i)
              accepted &= std::abs(entry.second->mean[i + 3] -
                                   entry.second->bias.dps[i]) <= 0.05;
          report["static_validation"][entry.first] = {
              {"passed", accepted},
              {"quality", quality},
              {"residual_limit_dps",
               entry.second->bias.enabled ? Json(0.05) : Json(nullptr)}};
          bias_ok &= accepted;
        }
        check(bias_ok, "static validation failed: movement, thermal drift or "
                       "residual bias >0.05 dps");
      }
    }
    capture_ok = !interrupted;
    report["complete"] = capture_ok;
  } catch (const std::exception &e) {
    report["error"] = e.what();
    std::cerr << "FAILED: " << e.what() << std::endl;
  }
  auto cleanup = [&](auto fn) {
    try {
      fn();
    } catch (const std::exception &e) {
      cleanup_errors.push_back(e.what());
    }
  };
  if (bmi)
    cleanup([&] { bmi->stop(); });
  if (icm)
    cleanup([&] { icm->stop(); });
  bmi.reset();
  icm.reset(); // Destructors retry a failed stop while bus/pinmux are still
               // leased.
  if (icm_spi) {
    report["icm_spi"] = {{"transactions", icm_spi->transactions},
                         {"errors", icm_spi->errors}};
    cleanup([&] { icm_spi->close(); });
  }
  if (bmi_spi) {
    report["bmi_spi"] = {{"transactions", bmi_spi->transactions},
                         {"errors", bmi_spi->errors}};
    cleanup([&] { bmi_spi->close(); });
  }
  bmi_spi.reset();
  icm_spi.reset();
  auto board_errors = board.restore();
  cleanup_errors.insert(cleanup_errors.end(), board_errors.begin(),
                        board_errors.end());
  report["board"] = board.evidence;
  report["cleanup_errors"] = cleanup_errors;
  report["elapsed_seconds"] = elapsed;
  if (!probe) {
    report["icm42688"] = icm_stream.summary(elapsed);
    report["bmi270"] = bmi_stream.summary(elapsed);
  }
  report["passed"] = capture_ok && cleanup_errors.empty();
  try {
    save(out / "summary.json", report);
  } catch (const std::exception &e) {
    std::cerr << "cannot save result: " << e.what() << '\n';
    return 1;
  }
  std::cout << (report["passed"].get<bool>() ? "PASS" : "FAIL")
            << "; summary: " << (out / "summary.json") << std::endl;
  return report["passed"].get<bool>() ? 0 : (interrupted ? 130 : 1);
}
