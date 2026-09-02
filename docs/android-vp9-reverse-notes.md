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

`tools/vp9-controls-dump` uses GStreamer's stateful VP9 parser to turn an IVF
stream into the exact standardized frame and compressed-header controls used
by `tools/vp9-request-sequence`.  This keeps the native validation path
independent of the proprietary parser.

AVS2 also uses the CedarX VE callback table, but Linux 6.1 has no standardized
stateless AVS2 request controls. Its native interface will therefore require
either a new private request control pending an upstream API, or a userspace
translation layer; advertising an unimplemented compressed format would be
incorrect.
