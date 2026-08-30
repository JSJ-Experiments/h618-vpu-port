# Experimental upstream H618 encoder path

The installed Orange Pi kernel is `6.1.31-1`. Its exact source/configuration
and `Module.symvers` were reconstructed, allowing the experimental Cedrus tree
in `cedrus-v4l2/` to build as an out-of-tree replacement module.

The port is based on Bootlin commit
`4e9497947c56f10351ed8c000605dbe47d4aee46` (`cedrus/h264-encoding`). It adds
V4L2 H.264 encoding, but declares `CEDRUS_CAPABILITY_H264_ENC` only for the
V3/V3s/S3 variant. This repository adds a real H618 register/address/ISP port;
it is not just a capability-bit change.

No complete kernel rebuild is required for current validation. The module has
produced clean H.264 from 320x240 through 3840x2160 and exceeds 4K25 when the
same DMA input is requeued. It is still loaded only for guarded tests and the
stock distro module is restored afterward.
