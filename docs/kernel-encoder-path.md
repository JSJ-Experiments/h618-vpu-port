# Experimental upstream H618 encoder path

The installed Orange Pi kernel (`6.1.31-1`) has only the built Cedrus module;
its headers tree omits the Cedrus source, so a V4L2 encoder cannot be safely
backported as a standalone module.

The investigated upstream prototype is Bootlin commit
`4e9497947c56f10351ed8c000605dbe47d4aee46` (`cedrus/h264-encoding`). It adds
V4L2 H.264 encoding, but declares `CEDRUS_CAPABILITY_H264_ENC` only for the
V3/V3s/S3 variant. H618/H616 support requires a real variant/registration port
and a bootable full kernel, not an unsafe capability-bit change.

The next experimental image must retain the stock `/boot/Image` and extlinux
entry as fallback, then add a distinct `h618-vpu-experimental` boot entry. It
must validate first at 320x240 and use watchdog/panic autoreboot before any
higher-resolution test.
