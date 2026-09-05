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

The distro GStreamer `v4l2slvp9dec` element now discovers the experimental
device and decodes that sequence byte-for-byte identically to FFmpeg.  Decoder
capture stride propagation was corrected so a stale 1280-pixel default cannot
leak into a 320-pixel sequence.  GStreamer regression runs remain byte-exact
to the recorded hardware outputs for H.264, HEVC, VP8 and MPEG-2.

Backward probability adaptation is also implemented for non-frame-parallel
VP9.  The H618's `0x3398`-byte symbol-count image is translated into the
standard V4L2 VP9 count interface, including its non-linear motion-vector
layout, before the kernel VP9 helpers refresh the selected frame context.  A
12-frame 640x360 adaptive key/P sequence produces the same probability-image
CRC on every frame as the Android vendor decoder and is byte-identical to its
FFmpeg software reference over all 4,147,200 output bytes.

VP9 segmentation is implemented, including ALT_Q/ALT_L, reference-frame and
skip features, tree/prediction probabilities, temporal updates, and segment-map
retention across frames.  A 60-frame 320x240 CBR stream using variance AQ and
four map updates is byte-identical through GStreamer's `v4l2slvp9dec` to its
FFmpeg software reference (SHA-256
`050649b904c646a116373429d2f5db5c37cee7deb596fb7d45fc4c678427dc38`).
The same reverse pass corrected the inter-frame header fields for high-precision
motion vectors and interpolation-filter selection.

Both VP9 tile dimensions are supported.  The H618 consumes row-major tile
geometry records from the head of its auxiliary buffer; the first tile remains
in the dedicated start/end registers.  Twelve-frame row-only and combined
two-column/two-row sequences decode byte-identically to FFmpeg after removing
the V4L2 capture buffer's normal 16-line alignment padding.  The combined 2-D
test has SHA-256
`224bb95c40e9c2b0c8e7b32cac8b9d4f7ebe857e1b2ea696ca0ed3db3c7c1694`.

VP9 differently-sized reference frames are now supported by the native H618
path.  CedarX's scale-factor routine computes Q14 `reference/current` factors;
the H618 register packing is mostly factor-in-bits-5..20 plus a five-bit Q4
step, except `GOLDEN_SCALE1`, whose vertical Q14 factor occupies bits 16..31.
The top-level reconstruction stride and chroma geometry must also be programmed
from each decoded frame's own aligned dimensions rather than from the maximum
negotiated CAPTURE format, because that reconstruction is later consumed as a
reference using its stored frame dimensions.  A 16-frame 640x360 -> 320x180 ->
640x360 sequence is byte-identical to libvpx for every visible frame.  The same
module also remains byte-identical on the existing three-frame inter, 60-frame
segmentation, row-tile and 2-D-tile regressions.  GStreamer 1.24's
`v4l2slvp9dec` intentionally drops non-keyframe resolution changes, so the
mixed-size test uses the direct stateless request client rather than that
userspace element.

Sustained 4K VP9 profile-0 decode is also validated.  A 120-frame
3840x2160@30 test stream completes through the direct request path in 2.738
seconds with output discarded, or 43.83 fps.  A second run streamed all
1,492,992,000 decoded NV12 bytes through a FIFO; its SHA-256 is
`00b7bbb72eeb47e72af2a566710905641f7d3904a0e074ef3295aa7105bdc02c`,
identical to an explicit libvpx decode of the same IVF.  GStreamer's default
4K capture pool cannot be used on this 1 GB board because its 128 MiB CMA pool
runs out while allocating 12,441,600-byte capture buffers.  The validation
client therefore supports reference-safe capture recycling and needed four
buffers for this stream.

CedarX also proves that H618 hardware supports VP9 profile 2 at 10-bit: the
vendor decoder successfully decoded a three-frame 320x240 `yuv420p10le`
sequence.  The native V4L2 path does not advertise this yet because H618's
primary 10-bit reconstruction surface is not ordinary NV12/P010.  It stores
an 8-bit NV21 high plane followed by packed two-bit extension data; exposing
that through a standard userspace format, or using the secondary output path,
remains the next VP9 ABI task.

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

* Finish the VP9 profile/10-bit V4L2 output-surface/API path; profile-2 10-bit hardware decode itself is vendor-validated.
* AVS/AVS2 is intentionally deferred. It requires a new codec engine and a
  suitable userspace API rather than mere format advertisement.
* Package the native module for persistent boot and validate decode/encode
  coexistence under normal applications.
