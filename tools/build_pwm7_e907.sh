#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${1:-"$repo_dir/build_pwm7_e907"}
: "${RISCV_CC:?Set RISCV_CC}" "${ARM_CC:?Set ARM_CC}"
rv_bin_dir=$(dirname -- "$RISCV_CC")
rv_prefix=$(basename -- "$RISCV_CC"); rv_prefix=${rv_prefix%gcc}
extra=()
if [[ -n ${RISCV_BINUTILS_DIR:-} ]]; then extra+=("-B$RISCV_BINUTILS_DIR/"); fi
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
cd -- "$repo_dir"
"$RISCV_CC" "${extra[@]}" -march=rv32ima -mabi=ilp32 -mcmodel=medany -mno-relax -msmall-data-limit=0 -ffreestanding -fno-builtin -fno-stack-protector -fno-jump-tables -Os -Wall -Wextra -Werror -nostdlib -Wl,--no-relax,-T,firmware/e907_live/servo.ld firmware/e907_live/start.S firmware/e907_live/pwm7.c -o "$output_dir/e907_pwm7.elf"
"${RISCV_OBJCOPY:-$rv_bin_dir/${rv_prefix}objcopy}" -O binary "$output_dir/e907_pwm7.elf" "$output_dir/e907_pwm7.bin"
"$ARM_CC" -O2 -Wall -Wextra -Werror -std=c99 tools/e907_pwm7_loader.c -ldl -o "$output_dir/e907_pwm7_loader"
cp tools/test_pwm7_e907.py "$output_dir/"
(cd -- "$output_dir"; sha256sum e907_pwm7.bin e907_pwm7_loader test_pwm7_e907.py > SHA256SUMS)
