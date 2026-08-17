# H618 legacy VE port

Research port of the Android-compatible Allwinner Cedar VE character-device driver from Orange Pi's 5.4 H618 kernel to the Orange Pi OS 6.1 H618 kernel. It is isolated from the stock Cedrus V4L2 decoder and is not deployed to a device by CI.

The initial CI job is intentionally a compatibility inventory: it compiles the legacy source against the 6.1 source and retains the exact compiler diagnostics that guide each API adaptation.
