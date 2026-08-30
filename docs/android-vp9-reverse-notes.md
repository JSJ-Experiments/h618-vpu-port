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

This proves H618 VP9 belongs in the shared VE driver and can coexist with the
other Cedrus engines. It is not a separately mapped Hantro block. The next
native implementation step is to map the remaining `libawvp9HwAL.so` register
writes to `V4L2_PIX_FMT_VP9_FRAME` plus the standardized
`V4L2_CID_STATELESS_VP9_FRAME` and
`V4L2_CID_STATELESS_VP9_COMPRESSED_HDR` request controls.

AVS2 also uses the CedarX VE callback table, but Linux 6.1 has no standardized
stateless AVS2 request controls. Its native interface will therefore require
either a new private request control pending an upstream API, or a userspace
translation layer; advertising an unimplemented compressed format would be
incorrect.
