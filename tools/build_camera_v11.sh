#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
output_dir=${1:-"$repo_dir/build_camera_v11"}
cc_bin=${ARM_CC:-${CC:-cc}}
mkdir -p -- "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)
"$cc_bin" -shared -fPIC -O2 -std=c11 -Wall -Wextra -Werror \
    "$repo_dir/tools/camera_v11_i2c.c" -o "$output_dir/camera_v11_i2c.so"
cp -- "$repo_dir/tools/camera_v11_capture.py" "$repo_dir/tools/run_camera_v11.py" "$output_dir/"
cp -- "$repo_dir/docs/CAMERA_V1_1.md" "$output_dir/README.md"
cp -- "$repo_dir/docs/camera_v1_1_verified_2026-11-20.json" "$output_dir/"
(
    cd -- "$output_dir"
    sha256sum camera_v11_i2c.so camera_v11_capture.py run_camera_v11.py README.md \
        camera_v1_1_verified_2026-11-20.json > SHA256SUMS
)
printf 'Prepared V1.1 camera package: %s\n' "$output_dir"
