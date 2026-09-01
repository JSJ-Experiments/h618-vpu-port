# H618 native JPEG encoder notes

The Android 12 `libvenc_jpeg.so` backend was used as a behavioral oracle. No
vendor binary is needed at runtime and none is committed.

## Hardware sequence

The H618 JPEG encoder shares the AVC encoder block at VE offset `0xb00` and
the encoder ISP input block. The working native sequence is:

1. Select JPEG mode with `b18 = 0x00010000`.
2. Configure the ISP for NV12 input and program 256-byte-unit DMA addresses.
3. Configure picture size, stream bounds, interrupt/status state, and JPEG
   parameter words.
4. Load 128 quantization words through the auto-incrementing SRAM port at
   `be0`/`be4`.
5. Program `b14 = BIT(25) | ceil(height / 16) << 16 | 7` and trigger with
   `b18 = 0x00010008`.
6. Read the byte-aligned entropy length from `b90` after the completion IRQ.

Each quantization SRAM word is:

```
high16 = quant >> 1
low16  = min(65535, 65536 / quant + (65536 / quant < 65535))
```

JPEG quality uses the conventional scale `5000 / quality` below 50 and
`200 - 2 * quality` otherwise, followed by rounded scaling and clamping to
1..255.

## Header handling

The vendor backend emits its markers through the hardware put-bits port,
using trigger `0x00010801` for every byte and polling status bit 9 after every
trigger. A missed final poll can overlap put-bits and entropy start.

The native driver deliberately avoids that fragile path. Hardware writes only
the entropy payload. In VB2 `buf_finish`, after the capture buffer has been
synchronized for CPU access, the driver moves entropy forward, writes a
623-byte baseline JFIF header containing the matching quantization tables, and
appends EOI. This also keeps all CPU buffer access out of hard-IRQ context.

## Validation

Validated on Orange Pi Zero 3, kernel `6.1.31-1`:

* 320x240 at qualities 1, 50, 90 and 100
* 640x480, 1280x720 and 1920x1080 at quality 90
* marker parsing, FFmpeg decoding and ImageMagick decoding
* 120 frames at 1920x1080: 133.25 fps
* H.264 regression: 167.81 fps at 1080p and 40.07 fps at 4K

An early test module also exposed a control-registration bug: JPEG duplicated
`V4L2_CID_MIN_BUFFERS_FOR_OUTPUT`, leaving the duplicate V4L2 control without
a cluster pointer. The automatic `v4l_id` udev probe opened `/dev/video1` and
panicked in `__v4l2_ctrl_handler_setup`. Keeping the queue control shared and
registering it only once fixes the panic.
