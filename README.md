# H618 legacy VE port

Research port of the Android-compatible Allwinner Cedar VE character-device driver from Orange Pi's 5.4 H618 kernel to the Orange Pi OS 6.1 H618 kernel. It is isolated from the stock Cedrus V4L2 decoder and is not deployed to a device by CI.

The initial CI job is intentionally a compatibility inventory: it compiles the legacy source against the 6.1 source and retains the exact compiler diagnostics that guide each API adaptation.

## Build status

The ARM32-only cache assembly has been removed from the arm64 module. Its
arbitrary-user-address ioctl is rejected on arm64; a production adapter must
use `DMA_BUF_IOCTL_SYNC` on a contiguous DMA-BUF instead. This prevents a
silent cache-maintenance no-op.

## H618 binding adaptation

The port now recognizes Orange Pi OS’s `allwinner,sun50i-h616-video-engine` node and maps its upstream clock names (`ahb`, `mod`, `ram`). This remains **not deployable**: stock Cedrus already owns that node, and a contiguous DMA-BUF allocator and userspace adapter are still required.

## Coexistence target

The end state is stock Cedrus request-API decoding plus a V4L2 encoder on the
same VE driver. The temporary legacy-driver takeover is only a faster way to
validate vendor VENC behavior. It cannot coexist with Cedrus because both
bind `video-codec@1c0e000`.
