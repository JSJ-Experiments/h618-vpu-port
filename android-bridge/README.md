# Android memory-adapter bridge

This builds a 32-bit Android/Bionic `libMemAdapter.so` replacement. It uses
the port driver's per-file coherent-CMA allocator (`0x710`/`0x711`) instead of
Android ION. It is not installed automatically and must only be used while the
legacy VE driver owns the VPU.

The ABI follows CedarX's public `ScMemOpsS` layout. It is a bridge for the
vendor encoder libraries, not a general-purpose allocator.
