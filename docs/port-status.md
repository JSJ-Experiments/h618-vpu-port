# H618 port status

The target is Orange Pi OS Arch arm64, kernel `6.1.31-1`.  The module builds
against its exact configuration and `Module.symvers` in CI.  A temporary
unbind of `sunxi_cedrus` proved that the port binds the H618
`video-codec@1c0e000` node and creates `/dev/cedar_dev`; Cedrus was restored
after the probe.

This is not yet a demonstrated encoder. The Android H.264 implementation is
a 32-bit Android/Bionic binary, while the target is a 64-bit Linux userspace.
The target kernel's `CONFIG_COMPAT` has been proven to run arm32 code. The
private Android runtime and VENC library closure load under the Android linker
in emulation. A public Bionic `libMemAdapter.so` replacement now supplies the
public CedarX `ScMemOpsS` ABI using a guarded per-file coherent-CMA allocator
in the legacy driver. Both bridge and its ABI probe build with the Android NDK
on Blacksmith.

## Deliberate safety constraints

* The legacy and Cedrus drivers cannot own the VE concurrently.
* H618’s current kernel has CMA but no exported DMA-HEAP or ION allocator;
  the temporary bridge uses explicit coherent-CMA allocations instead.
* The old arbitrary-address cache-flush ioctl is rejected on arm64.  A
  no-op cache flush could silently yield corrupted video.
* DMA-BUF import accepts only one DMA segment because the legacy ABI submits
  one address and this kernel has no VE IOMMU mapping.

Next: on a freshly flashed target, run the coherent-CMA smoke test and Bionic
memory-ABI probe while the guarded legacy driver owns VE, then exercise one
H.264 frame through the Android VENC libraries. A real coexistence solution
still needs a V4L2 encoder path in Cedrus and therefore kernel-driver work.

## Verified on hardware

The current module was built on Blacksmith and briefly loaded on the target.
It claimed the H618 VE node, created `/dev/cedar_dev`, and was unloaded in the
same guarded command; the stock `sunxi_cedrus` module and `/dev/video0` were
then restored. No module is installed persistently.

The target's VE IP registers were read only while the guarded legacy takeover
was active: `0xf0 = 0`, `0xe0 = 0x00033010`, and `0xe4 = 0x00012011`.
`0x12011` is an entry in the Android H.264 binary's capability table. The
driver now follows Android's `0xf0`, `0xe0`, `0xe4` fallback for
`IOCTL_GET_IC_VER`.

## Safety status (2026-08-17)

The Android H.264 encoder closure is hardware-validated only at 320×240. Its
larger initialization paths are **not safe** with the current experimental
legacy kernel bridge: 720p and 1080p attempts can destabilize the system.
`h264-one-frame-smoke` therefore rejects sizes above 320×240 unless
`H618_UNSAFE_EXPERIMENTAL=1` is explicitly set. Do not treat the Android OMX
XML limits or the H618 datasheet limits as supported by this port yet.
