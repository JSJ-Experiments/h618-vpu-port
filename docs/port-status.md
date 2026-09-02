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

Native baseline JPEG encoding is validated at 320x240, 640x480, 1280x720 and
1920x1080, including qualities 1, 50, 90 and 100. Every output has a valid
JFIF marker graph, decodes cleanly with FFmpeg/ImageMagick, and reports the
requested 4:2:0 dimensions. A 120-frame 1080p benchmark completed at 133.25
fps, above the H618's advertised 1080p60 JPEG rate. Hardware produces the
quantized entropy payload; the driver adds the 623-byte baseline JFIF header
and EOI after VB2 has synchronized the coded buffer for CPU access.

Stock Cedrus decode is hardware-validated for MPEG-2, H.264, H.265/HEVC and
VP8. Panfrost is also validated under XFCE/Xorg with an accelerated Mali-G31
renderer.

The new native H618 VP9 engine now decodes 8-bit profile-0 key and same-size
inter frames through
the standardized stateless request controls.  The control probability context
is forward-updated and packed at run time; no captured vendor table is used by
the driver.  Output is byte-identical to software decoding for 320x240 key
frames at two different compressed-header probability sets, for a four-tile
1920x1080 key frame (compared over the complete 1920x1088 capture allocation),
and for every frame of a three-frame 320x240 key/P/P sequence.  The q37 key
frame's native and fresh FFmpeg reference CRCs are both `4b33ea93`; the former
`674f4ee7` comparison target was a stale `/tmp` artifact.

The module is currently validated as a guarded replacement: remove distro
`sunxi_cedrus`, load its V4L2/VB2 dependencies and the experimental module,
run the test, then unload it and restore distro `sunxi_cedrus`. The test helper
can retain the experimental module with `CEDRUS_KEEP_EXPERIMENTAL=1` for a
multi-test session.

## Android reference path

The legacy character driver and 32-bit Bionic bridge remain a reverse-
engineering oracle only. The corrected private ABI includes the leading
`bEncH264Nalu` word, Android-specific parameter indexes, the Android 12
MemAdapter callback order, and explicit input-buffer recycling. The vendor
stack successfully encodes 25 frames at 1920x1080 and 3840x2160, and its SPS,
PPS, and slice form a valid Annex-B stream. The legacy and V4L2 drivers cannot
own the VE concurrently.

## Remaining scope

* Finish VP9 reference scaling, backward probability adaptation, segmentation,
  tile rows, and profile/10-bit coverage.  AVS2 still requires
  a new codec engine and a suitable userspace API rather than mere format
  advertisement.
* Package the native module for persistent boot and validate decode/encode
  coexistence under normal applications.
