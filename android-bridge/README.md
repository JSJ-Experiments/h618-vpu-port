# Android memory-adapter bridge

This builds a 32-bit Android/Bionic `libMemAdapter.so` replacement. It uses
the port driver's per-file coherent-CMA allocator (`0x710`/`0x711`) instead of
Android ION. It is not installed automatically and must only be used while the
legacy VE driver owns the VPU.

The ABI follows CedarX's public `ScMemOpsS` layout. It is a bridge for the
vendor encoder libraries, not a general-purpose allocator.

`memadapter-abi-probe` is a 32-bit Bionic test program. With the replacement
library first in `LD_LIBRARY_PATH` and `/dev/cedar_dev` owned by the legacy
driver, it verifies the loader ABI and opens the VE allocation device. It does
not start an encode.

`libCdcIonShim.so` is loaded privately with `LD_PRELOAD` for the vendor VENC
closure. It replaces the initial ION bookkeeping open with a non-ION handle and
answers CedarX's obsolete command-3 `check_h3pro` query as a non-H3 device;
actual buffers still come from `libMemAdapter.so`'s coherent-CMA bridge.  It
must be preloaded only for the isolated vendor encoder process.
