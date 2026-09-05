# H618 VP9 handback

Date: 2026-09-05

## Status

Native H618 CedarX/VE VP9 decode support is validated for:

- VP9 profile 0, 8-bit 4:2:0
- VP9 profile 2, 10-bit 4:2:0
- inter-frame references and scaling
- tile columns
- segmentation
- backward probability adaptation
- mixed-resolution references
- long reference chains and capture-buffer recycling
- 3840x2160 profile-2 decode

Unsupported VP9 profile 1, profile 3, and 12-bit requests are rejected with
`-EOPNOTSUPP`. P010 is not advertised; the V4L2 CAPTURE ABI remains NV12/ST12.

## 10-bit reconstruction details

H618 stores profile-2 reconstruction in a native split 8+2-bit surface while
SDRT writes the user-visible NV12 CAPTURE buffer.

The final reference handling has three important H618-specific details:

1. Reference picture heights are programmed on a 16-line-aligned geometry.
2. The bottom visible row is replicated through the aligned reference padding
   for the high-eight-bit and packed-low-two-bit luma/chroma regions.
3. The hardware writes packed low-two-bit chroma after
   `ALIGN(visible_height, 8)` luma rows. Before the surface becomes a reference,
   that chroma payload is moved to the 16-line-aligned reference layout.

The third item was the final mixed-resolution bug. At 320x180 with a packed
stride of 96 bytes, the exact chroma payload was found at offset `0x45500` while
the old code copied from `0x45380`; the 0x180-byte difference is exactly four
96-byte rows. Using `ALIGN(height, 8)` for the source makes the entire
640x360 -> 320x180 -> 640x360 sequence byte-exact.

## Validation

All comparisons below are byte-for-byte against libvpx software decode unless
noted otherwise.

| Test | Result |
| --- | --- |
| Profile 2 10-bit 640x360, tile_cols_log2=0, 5 frames | PASS |
| Profile 2 10-bit 640x360, tile_cols_log2=1, 5 frames | PASS |
| Profile 2 10-bit mixed resize, 640x360 -> 320x180 -> 640x360, 16 frames | PASS |
| Profile 2 10-bit segmentation, 320x240, 60 frames | PASS |
| Profile 2 10-bit backward probability adaptation, 640x360, 12 frames | PASS |
| Profile 2 10-bit long reference chain, 640x360, 30 frames, 8-buffer pool | PASS |
| Profile 2 10-bit 3840x2160, 8 tile columns, 5 frames, 3-buffer pool | PASS |
| Profile 0 segmentation, 12 frames | PASS |
| Profile 0 backward probability adaptation, 12 frames | PASS |
| Profile 0 long reference chain/recycling, 30 frames | PASS |
| Profile 0 mixed-resolution resize, 16 frames | PASS |
| Profile 1 request | PASS: rejected with `-EOPNOTSUPP` |
| Profile 3 request | PASS: rejected with `-EOPNOTSUPP` |
| Profile 2 12-bit request | PASS: rejected with `-EOPNOTSUPP` |
| P010 advertised on CAPTURE | PASS: no |

### 4K CMA note

A first 3840x2160 test with five simultaneously allocated CAPTURE buffers
failed while allocating the third 10-bit reconstruction surface. The kernel
reported CMA allocation failure (`-ENOMEM`), not a decoder/VPU error. Running
the same five-frame stream with a three-buffer reference-safe recycling pool
completed successfully and all five frames matched libvpx exactly.

## Final quality checks

- `git diff --check`: clean
- kernel `checkpatch.pl --strict`: 0 errors, 0 warnings, 0 checks
- module vermagic: `6.1.31-1 SMP mod_unload aarch64`
- final tested module SHA-256:
  `7a31a41618004741408774d97ce407791029345f86350b08920e2635530221df`
- post-format smoke tests: 10-bit tc0 PASS; full resize16 PASS

The module compiler is newer than the compiler used for the target kernel
(GCC 15.2.0 vs GCC 12.1.0), producing the standard compiler-version warning,
but the module builds, loads, and passes the validation above.

## Useful validation artifacts

Development host:

- Main WIP: `~/build/h618-vpu-port.10bit-crc-probe-20260905-144609`
- Mixed-resolution exact refs: `/tmp/ref-resize16-exact`
- Mixed-resolution raw 10-bit refs: `/tmp/ref-resize16-raw10`
- 10-bit segmentation controls: `/tmp/h618-vp9-10bit-suite/seg60-controls-2123`
- 10-bit adaptation controls: `/tmp/h618-vp9-10bit-suite/adapt12-controls-2128`
- 10-bit long-chain controls: `/tmp/h618-vp9-10bit-suite/long30-controls-2131`
- 10-bit 4K controls: `/tmp/h618-vp9-10bit-suite/4k5-controls-2136`

Orange Pi target:

- Host: `oem@10.151.4.253`
- Test helper: `/tmp/h618-vp9-test/native-cedrus-session`
- Request harness: `/tmp/h618-vp9-test/vp9-request-sequence`
- Larger outputs: `/home/oem/vp9-test-output`

The target `/tmp` is a small tmpfs and was nearly full during validation; use
`/home/oem` for large 4K or multi-frame output files.
