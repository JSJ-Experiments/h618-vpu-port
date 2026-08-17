# Guarded legacy VE session

`legacy-ve-session` is the only supported temporary takeover path for a
cleanly flashed test target. It unbinds the stock Cedrus platform driver,
inserts the exact-kernel `sunxi_ve_legacy.ko`, runs one command, and uses an
exit trap to remove the module and rebind Cedrus.

Example after staging a matching module and aarch64 coherent test:

```sh
sudo ./scripts/legacy-ve-session ./sunxi_ve_legacy.ko -- ./cedar-coherent-smoke
```

Do not use this on the filesystem-corrupt installation. This is intentionally
not a service and never persists across reboot. It cannot provide concurrent
Cedrus decode and legacy VENC.
