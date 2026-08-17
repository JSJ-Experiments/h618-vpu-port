# H618 Mali G31 desktop rendering

The target kernel already has `CONFIG_DRM_PANFROST=m` and userspace Mesa has
`panfrost_dri.so`. Its Zero 3 board DTB deliberately disables `gpu@1800000`,
so Xorg falls back to software rendering. The patch in `patches/` enables the
existing node and its `dcdc1` supply.

After a clean reflash, build or stage the patched DTB, replace the `FDT`
target from `extlinux.conf` atomically on the boot partition, and reboot.
Then `modprobe panfrost` and verify `glxinfo -B` reports the Panfrost renderer
instead of llvmpipe. Do not replace the DTB on the currently corrupt rootfs.
