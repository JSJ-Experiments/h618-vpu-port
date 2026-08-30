# H618 port status

Target: Orange Pi Zero 3, Orange Pi OS Arch arm64, kernel `6.1.31-1`.

## Native V4L2 path

`cedrus-v4l2/` combines the stock stateless Cedrus decoders with the Bootlin
H.264 encoder prototype and an H618-specific port. Reverse engineering of the
Orange Pi Android 12 vendor encoder established the H618 top-mode, ISP input
address/stride, width/height packing, output address shift, and AVC control
register ABI. No proprietary source or binary is committed.

Hardware validation completed for H.264 encode at 320x240, 640x480, 1280x720,
1920x1080 and 3840x2160. Generated Annex-B streams decode without FFmpeg
errors; one-frame PSNR results are 52--58 dB. A 25-frame 4K stream also decodes
cleanly. At the vendor-tested 696 MHz VE clock, the same-buffer benchmark
encodes 25 4K frames in 0.623 seconds (40.10 fps), exceeding the advertised
4K25 capability. A slower v4l2-ctl result was traced to refilling 311 MB of raw
input from userspace on the 1 GB board.

The module is currently validated as a guarded replacement: remove distro
`sunxi_cedrus`, load its V4L2/VB2 dependencies and the experimental module,
run the test, then unload it and restore distro `sunxi_cedrus`.

## Android reference path

The legacy character driver and 32-bit Bionic bridge remain a reverse-
engineering oracle only. The corrected private ABI includes the leading
`bEncH264Nalu` word, Android-specific parameter indexes, the Android 12
MemAdapter callback order, and explicit input-buffer recycling. The vendor
stack successfully encodes 25 frames at 1920x1080 and 3840x2160, and its SPS,
PPS, and slice form a valid Annex-B stream. The legacy and V4L2 drivers cannot
own the VE concurrently.

## Remaining scope

* Validate all stock H.264/H.265/VP8/MPEG-2 stateless decode paths on H618.
* VP9 and AVS2 are not implemented by this Cedrus generation and require new
  codec engine support rather than format advertisement.
* Add the H618 JPEG encoder path.
* Validate Panfrost desktop rendering with the enabled GPU DT node.
