# H618 port status

The target is Orange Pi OS Arch arm64, kernel `6.1.31-1`.  The module builds
against its exact configuration and `Module.symvers` in CI.  A temporary
unbind of `sunxi_cedrus` proved that the port binds the H618
`video-codec@1c0e000` node and creates `/dev/cedar_dev`; Cedrus was restored
after the probe.

This is **not** an encoder yet.  The Android H.264 implementation is a
32-bit Android/Bionic binary, while the target is a 64-bit Linux userspace.
The public CedarX tree provides the wrapper ABI but not the proprietary H.264
hardware core.

## Deliberate safety constraints

* The legacy and Cedrus drivers cannot own the VE concurrently.
* H618’s current kernel has CMA but no exported DMA-HEAP or ION allocator.
* The old arbitrary-address cache-flush ioctl is rejected on arm64.  A
  no-op cache flush could silently yield corrupted video.
* DMA-BUF import accepts only one DMA segment because the legacy ABI submits
  one address and this kernel has no VE IOMMU mapping.

The next implementation milestone is a contiguous DMA-BUF allocator plus
`DMA_BUF_IOCTL_SYNC`, then a minimal 64-bit CedarX-compatible encoder adapter.

## Verified on hardware

The current module was built on Blacksmith and briefly loaded on the target.
It claimed the H618 VE node, created `/dev/cedar_dev`, and was unloaded in the
same guarded command; the stock `sunxi_cedrus` module and `/dev/video0` were
then restored. No module is installed persistently.
