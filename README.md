# H618 legacy VE port

Research port of the Android-compatible Allwinner Cedar VE character-device driver from Orange Pi's 5.4 H618 kernel to the Orange Pi OS 6.1 H618 kernel. It is isolated from the stock Cedrus V4L2 decoder and is not deployed to a device by CI.

The initial CI job is intentionally a compatibility inventory: it compiles the legacy source against the 6.1 source and retains the exact compiler diagnostics that guide each API adaptation.

## Build status

The ARM32-only cache assembly has been removed from the arm64 module. Its
arbitrary-user-address ioctl is rejected on arm64; a production adapter must
use `DMA_BUF_IOCTL_SYNC` on a contiguous DMA-BUF instead. This prevents a
silent cache-maintenance no-op.

## H618 binding adaptation

The port now recognizes Orange Pi OS’s `allwinner,sun50i-h616-video-engine` node and maps its upstream clock names (`ahb`, `mod`, `ram`). The legacy driver also exposes a per-file coherent-CMA allocation ABI, and `android-bridge/` builds a 32-bit Android/Bionic `libMemAdapter.so` replacement for the vendor VENC stack. Neither is installed automatically: stock Cedrus already owns the node.

## Coexistence target

The end state is stock Cedrus request-API decoding plus a V4L2 encoder on the
same VE driver. The temporary legacy-driver takeover is only a faster way to
validate vendor VENC behavior. It cannot coexist with Cedrus because both
bind `video-codec@1c0e000`.

## Current validation path

No full kernel rebuild is required for the temporary validation path: the
out-of-tree module is built against the exact target kernel headers and
`Module.symvers`. After a clean reflash: temporarily unbind Cedrus, load the
matching module, run `tools/cedar-coherent-smoke`, then run
`android-bridge/memadapter-abi-probe` with the private Android runtime and
vendor VENC libraries. `tools/android32-vendor-probe --create-h264` then
checks that the proprietary VENC dispatcher can create its H.264 object.
Unload the module and rebind Cedrus afterwards.

The Android libraries and extracted runtime are private deployment inputs and
are never committed to this public repository.
