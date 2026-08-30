# H618 Cedrus V4L2 encode/decode backport

This directory is based on Bootlin's GPL-2.0 `cedrus/h264-encoding` work at
commit `4e9497947c56f10351ed8c000605dbe47d4aee46` and is backported as an
out-of-tree module for the Orange Pi OS 6.1.31 kernel.

Local changes add the `allwinner,sun50i-h616-video-engine` match used by H616
and H618, adapt the remove callback to Linux 6.1, and implement the H618 ISP,
address, dimension, clock, and H.264 register differences.

The Android TV H618 `libvenc_h264.so` was disassembled only to verify the VE
register ABI. No Android vendor code or binary is included here. Its encoder
register offsets match the public Cedrus encoder implementation.

H.264 encoding is hardware-validated at 320x240, 640x480, 1280x720,
1920x1080, and 3840x2160.  FFmpeg software decoding reports no errors; measured
one-frame PSNR was 52--58 dB depending on size.  A 25-frame 4K stream also
decoded cleanly. `tools/v4l2-encode-bench.c` requeues one prepared input buffer
and measures 40.10 fps for 25 3840x2160 frames at a 696 MHz VE clock. In
contrast, making `v4l2-ctl` read 311 MB from `/dev/zero` during the same test
measured 16.52 fps: that result was userspace input-copy throughput, not the
encoder limit.

The driver advertises even dimensions through 3840x2160 and generates H.264
cropping syntax for dimensions that are not multiples of 16. The stock Cedrus
decode formats remain available on the same module and node.
