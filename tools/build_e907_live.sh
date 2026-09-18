#!/usr/bin/env bash
# Builds both the RV32 firmware and the A53 RAM loader. No board writes.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${1:-"$repo_dir/build_e907_live"}
: "${RISCV_CC:?Set RISCV_CC to a RISC-V bare-metal GCC}"
: "${ARM_CC:?Set ARM_CC to an AArch64 Linux GCC}"
rv_bin_dir=$(dirname -- "$RISCV_CC")
rv_prefix=$(basename -- "$RISCV_CC")
rv_prefix=${rv_prefix%gcc}
rv_objcopy=${RISCV_OBJCOPY:-"$rv_bin_dir/${rv_prefix}objcopy"}
rv_size=${RISCV_SIZE:-"$rv_bin_dir/${rv_prefix}size"}
extra=()
# For an extracted Debian toolchain, set the directory containing target as/ld.
if [[ -n ${RISCV_BINUTILS_DIR:-} ]]; then extra+=("-B$RISCV_BINUTILS_DIR/"); fi
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
cd -- "$repo_dir"
common=(-march=rv32ima -mabi=ilp32 -mcmodel=medany -mno-relax -msmall-data-limit=0 -ffreestanding -fno-builtin -fno-stack-protector -fno-jump-tables -Os -Wall -Wextra -Werror -nostdlib)
"$RISCV_CC" "${extra[@]}" "${common[@]}" -Wl,--no-relax,-T,firmware/e907_live/probe.ld firmware/e907_live/probe.S -o "$output_dir/e907_probe.elf"
"$RISCV_CC" "${extra[@]}" "${common[@]}" -Wl,--no-relax,-T,firmware/e907_live/servo.ld firmware/e907_live/start.S firmware/e907_live/servo.c -o "$output_dir/e907_servo.elf"
for name in probe servo; do
  "$rv_objcopy" -O binary "$output_dir/e907_$name.elf" "$output_dir/e907_$name.bin"
  "$rv_size" "$output_dir/e907_$name.elf"
done
"$ARM_CC" -O2 -Wall -Wextra -Werror -std=c99 tools/e907_live_probe.c -ldl -o "$output_dir/e907_live_probe"
sha256sum "$output_dir/e907_probe.bin" "$output_dir/e907_servo.bin" "$output_dir/e907_live_probe" > "$output_dir/SHA256SUMS"
