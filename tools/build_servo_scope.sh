#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${1:-"$repo_dir/build_servo_scope"}
"$repo_dir/tools/build_e907_live.sh" "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
"$ARM_CC" -O2 -Wall -Wextra -Werror -std=c99 "$repo_dir/tools/servo_scope_linux.c" -o "$output_dir/servo_scope_linux"
cp -- "$repo_dir/tools/run_servo_scope.py" "$output_dir/run_scope.py"
cp -- "$repo_dir/firmware/e907_live/SCOPE.md" "$output_dir/README.md"
chmod +x -- "$output_dir/run_scope.py"
(
    cd -- "$output_dir"
    sha256sum e907_probe.bin e907_servo.bin e907_live_probe servo_scope_linux run_scope.py > SHA256SUMS
    tar --mtime=@0 --owner=0 --group=0 -czf servo_scope_tests.tar.gz \
        e907_probe.bin e907_servo.bin e907_live_probe servo_scope_linux run_scope.py SHA256SUMS README.md
)
printf 'Prepared package: %s/servo_scope_tests.tar.gz\n' "$output_dir"
