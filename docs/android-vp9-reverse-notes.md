# Android VP9 reverse-engineering notes

Inspection of the private Android decoder closure shows that `libawvp9Hw.so`
is architecturally different from the CedarX H.264/HEVC decoders.

* It imports `open`, `close`, and `mmap` directly and contains the physical
  register base `0x01c00000`.
* It references `/dev/mem` and an Android-only `/tmp/dev/hx170` interface.
* Its register helper names (`vp9_func_ctrl_reg30`,
  `vp9_function_status_reg38`, `vp9_trigger_type_reg34`) match a distinct
  VP9 block, rather than the regular Cedar VE block at `0x01c0e000`.
* The stock Orange Pi OS DT does not expose this block. Android's supplied
  board DT likewise exposes only the Cedar VE node at `0x01c0e000`.

Therefore VP9 cannot be fixed by adding a Cedar VE mode-5 interrupt case.
The Linux port needs a separately owned, clock/reset-managed VP9 platform
component (or a documented proof that this Android binary targets a different
H6-family variant). It must not be allowed to map `/dev/mem` from normal
applications. The next implementation step is to recover the Android
clock/reset and interrupt wiring, then provide a constrained kernel driver
that exposes only the operations needed by the vendor VP9 closure.
