#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build a Zero 3 DTB with its already-configured Panfrost GPU node enabled.
set -Eeuo pipefail
kernel_tree=${1:?usage: $0 /path/to/linux-orangepi-tree [output.dtb]}
output=${2:-sun50i-h618-orangepi-zero3-panfrost.dtb}
repo_root=$(cd "$(dirname "$0")/.." && pwd)
patch_file="$repo_root/patches/0001-orangepi-zero3-enable-panfrost-gpu.patch"

[[ -f "$kernel_tree/arch/arm64/boot/dts/allwinner/sun50i-h618-orangepi-zero3.dts" ]] || {
  echo "not an Orange Pi H618 kernel tree: $kernel_tree" >&2; exit 2;
}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -a "$kernel_tree" "$work/kernel"
patch -d "$work/kernel" -p1 < "$patch_file"
make -C "$work/kernel" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
  arch/arm64/boot/dts/allwinner/sun50i-h618-orangepi-zero3.dtb
cp "$work/kernel/arch/arm64/boot/dts/allwinner/sun50i-h618-orangepi-zero3.dtb" "$output"
echo "built $output"
