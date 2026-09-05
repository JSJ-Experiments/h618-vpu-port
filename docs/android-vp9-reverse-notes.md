# Android VP9 reverse-engineering notes

The Android image contains two unrelated VP9 implementations. The earlier
analysis accidentally inspected `libawvp9Hw.so`, an old Hantro-style backend
that opens `/dev/mem` and mentions `/tmp/dev/hx170`. The H618 CedarX plugin
list actually selects **`libawvp9HwAL.so`**. Conclusions drawn from the other
library do not apply to H618.

`libawvp9HwAL.so` is an Allwinner VE backend:

* `CreateVp9HwDecoder` receives the CedarX VE-operations table.
* During initialization it calls the VE operation at table offset `0x20` with
  engine mode **5**, obtains one register base, calls the same operation with
  mode 0, and obtains a second/top register base.
* Those addresses are stored in the exported globals `uVp9RegisterBaseAddr`
  and `uVp9TopBaseAddr`; the library does not open or mmap `/dev/mem`.
* `Vp9HwVeIsr` reads and acknowledges status at decoder-register offset
  `0x38`, with completion/error bits 0--2.
* `VP9SetTopReg` writes top-register offsets `0xc4`, `0xc8`, and `0xec`.

CedarX's public `veInterface.h` identifies group 5 as the H.265 decoder group.
The VP9 register names and offsets confirm that H618 reuses the block at VE
offset `0x500`, with VP9-specific semantics. The recovered map is now recorded
in `cedrus_regs.h`. Vendor allocation routines additionally establish two
fixed working areas of `0x88000` and `0x1f4000` bytes and two segment maps of
32 bytes per 64x64 superblock.  The HAL's probability builder was reversed
into a dynamic V4L2-frame-context packer.  CedarX issues trigger command 7 to
prime that image before command 8 starts the frame; omitting the first command
caused valid-looking completion with incorrect pixels.

H618 VP9 therefore belongs in the shared VE driver and can coexist with the
other Cedrus engines; it is not a separately mapped Hantro block.  The native
path uses `V4L2_PIX_FMT_VP9_FRAME` plus the standardized
`V4L2_CID_STATELESS_VP9_FRAME` and
`V4L2_CID_STATELESS_VP9_COMPRESSED_HDR` request controls.  A three-frame
key/P/P stream is byte-identical to software decoding.  The sequence also
identified header-sync bit 25 as the H618 `use_prev_frame_mvs` control: it is
clear for the first inter frame after a key frame and set for the next inter
frame.  CedarX uses one persistent page-aligned motion-vector workspace for
the sequence rather than one allocation per reference picture.

Non-frame-parallel streams set header-sync bit 27, which makes the engine emit
the `0x3398`-byte backward-adaptation count image at auxiliary-buffer offset
`0x4b00`.  Disassembly of `vp9_update_counts`, `Vp9AdaptCoefProbs`,
`Vp9AdaptModeProbs`, and `Vp9AdaptNmvProbs` recovered the hardware ordering.
Coefficient and ordinary mode counts are mostly contiguous; motion-vector
counts are split and reordered by component and field.  The native driver
normalizes that layout and uses the common V4L2 VP9 probability helpers.  On a
12-frame 640x360 adaptive stream, its packed probability CRCs match the vendor
decoder frame-for-frame and its decoded NV12 is byte-identical to FFmpeg.

Segmentation uses header-sync bits 17--19 for enabled, update-map, and temporal
update.  The seven tree probabilities are stored at probability-image offset
`0xaf0`, followed by padding and the three prediction probabilities at
`0xaf8`.  Register `0x55c` holds reference-frame enable/value and skip-enable
features; ALT_Q and ALT_L remain in the dequant and loop-filter SRAM tables.
The segment map is not addressed by the vendor-named register `0xb0`: the
engine reads and writes an implicit window at probability allocation offset
`0x8000`, with 32 bytes per superblock.  The native driver mirrors CedarX by
retaining that map across frames and replacing it only after a successful
update-map frame.  Its probability and segment-map CRCs match targeted Android
snapshots, and a 60-frame variance-AQ stream is byte-identical to software.

Disassembly and an Android inter-frame snapshot also established that
header-sync bit 8 is `allow_high_precision_mv` and bits 9--11 contain the VP9
interpolation-filter enum.  Treating bit 11 as the high-precision flag happened
to work for switchable-filter streams until a frame disabled high-precision
motion vectors.

Tile rows and columns share one row-major geometry list at the start of the
auxiliary buffer.  Each tile after the first occupies four words: two zero
words, packed `(start_y << 16) | start_x`, and packed
`(end_y << 16) | end_x`, in 64x64-superblock coordinates.  The first tile uses
registers `0x568` and `0x56c`.  Header-sync bit 0 means that the total tile
count is greater than one.  A targeted Android dump for a 640x360 two-row
frame contained `00000000 00000000 00030000 00050009`, matching the native
geometry exactly.

`android-bridge/vp9-counts-oracle` calls the vendor library's pure
`VP9GetCounts()` export against a captured count image.  It is retained only as
a reverse-engineering cross-check; the native decode path neither loads nor
ships the Android library.

`tools/vp9-controls-dump` uses GStreamer's stateful VP9 parser to turn an IVF
stream into the exact standardized frame and compressed-header controls used
by `tools/vp9-request-sequence`.  This keeps the native validation path
independent of the proprietary parser.

`android-bridge/vdecoder-ivf-smoke` accepts `CEDAR_BRIDGE_DUMP_FRAME=N` to dump
only one selected zero-based frame.  This keeps targeted vendor snapshots from
filling the board's tmpfs during long sequences.

AVS2 is deferred for the current implementation. It also uses the CedarX VE
callback table, but Linux 6.1 has no standardized
stateless AVS2 request controls. Its native interface will therefore require
either a new private request control pending an upstream API, or a userspace
translation layer; advertising an unimplemented compressed format would be
incorrect.
