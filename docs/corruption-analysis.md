# H618 corruption incident and driver safeguards

The failures were consistent with stale VE DMA rather than ordinary userspace
package damage: unrelated AArch64 processes (`systemd`, journald, `nmtui`) began
receiving SIGILL/SIGSEGV, followed by dirty ext4 metadata after forced power
cycles.  Kernel logs also showed the legacy VE client exiting with a lost lock.

The audit found two concrete memory-safety defects in the experimental legacy
validation path:

1. A client could exit after `IOCTL_ENGINE_REQ` without `IOCTL_ENGINE_REL`.
   `release()` freed that client's coherent CMA buffers without first resetting
   the H618 VE, while the device-global engine reference and clocks remained
   active.  Because H618 has no IOMMU on this path, an unfinished job could keep
   writing to pages after the allocator reused them.
2. `IOCTL_COPY_PROC_INFO` accepted a userspace-selected length for a fixed
   1024-byte debug channel, permitting a kernel heap overwrite.

The legacy driver now:

- tracks engine references per open file;
- asserts VE reset and synchronizes its IRQ before unmapping/freeing DMA;
- unwinds leaked engine references during abnormal close;
- quiesces DMA before clock removal and module unbind;
- rejects debug records larger than their backing channel and validates the
  channel before copying;
- requests the IRQ only after all register/reset state exists;
- fully unwinds partial clock and probe failures.

Two target-side regression programs cover the specific faults:

- `tools/cedar-abort-cleanup-smoke.c` intentionally abandons an engine
  reference, VENC lock, mapping, and CMA allocation.  The fixed driver resets
  the VE, releases the lost lock/reference, disables its clocks, unloads, and
  rebinds stock Cedrus.
- `tools/cedar-debug-bounds-smoke.c` verifies a 1025-byte debug record is
  rejected while a 1024-byte record completes normally.

On the live 6.1.31 target, both tests completed, stock Cedrus rebound to the VE,
journald remained active, and no kernel errors were emitted.  This demonstrates
that the identified cleanup and bounds paths are fixed; it does not make
arbitrary proprietary register programming safe.  Vendor probes remain
temporary, isolated validation tools and must never be installed as an
automatic boot service.

For recovery, the target is configured to let PID 1 service the `sunxi-wdt`
hardware watchdog every 10 seconds, reboot 10 seconds after a kernel panic,
panic on an oops, and keep Magic SysRq enabled.  Previously the hardware existed
but `RuntimeWatchdogSec=0` and `kernel.panic=0`, so the observed init-killing
panic was expected to remain frozen indefinitely.
