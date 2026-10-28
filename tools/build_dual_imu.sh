#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${1:-"$repo_dir/build_dual_imu"}
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
cc_bin=${CC:-gcc}
cxx_bin=${CXX:-g++}
for file in bmi2 bmi270; do
  "$cc_bin" -std=c99 -O2 -Wall -Wextra -I"$repo_dir/cpp/vendor/bmi270" \
    -c "$repo_dir/cpp/vendor/bmi270/$file.c" -o "$output_dir/$file.o"
done
"$cxx_bin" -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  -I"$repo_dir/cpp/vendor" -I"$repo_dir/cpp/vendor/bmi270" \
  "$repo_dir/cpp/imu/main.cpp" "$repo_dir/cpp/imu/linux_bus.cpp" "$repo_dir/cpp/imu/sensors.cpp" \
  "$output_dir/bmi2.o" "$output_dir/bmi270.o" -o "$output_dir/dual_imu"
# The output may target AArch64 while the build host is x86_64.
printf 'Built %s\n' "$output_dir/dual_imu"
(cd -- "$output_dir" && sha256sum dual_imu > SHA256SUMS)
