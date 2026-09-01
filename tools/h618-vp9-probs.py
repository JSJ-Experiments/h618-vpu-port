#!/usr/bin/env python3
"""Inspect H618 VP9 probability images and make V4L2 update controls.

The default context is the raw 2039-byte ``v4l2_vp9_frame_context``.  A
hardware image may be either the 0xafc-byte table itself or a larger CedarX
work buffer selected with ``--offset``.
"""

import argparse
from pathlib import Path

CTX_SIZE = 2039
CONTROL_SIZE = 2040
HW_SIZE = 0xAFC

TX8 = 0
TX16 = TX8 + 2
TX32 = TX16 + 4
COEF = TX32 + 6
SKIP = COEF + 4 * 2 * 2 * 6 * 6 * 3
INTER_MODE = SKIP + 3
INTERP_FILTER = INTER_MODE + 7 * 3
IS_INTER = INTERP_FILTER + 4 * 2
COMP_MODE = IS_INTER + 4
SINGLE_REF = COMP_MODE + 5
COMP_REF = SINGLE_REF + 5 * 2
Y_MODE = COMP_REF + 5
UV_MODE = Y_MODE + 4 * 9
PARTITION = UV_MODE + 10 * 9
MV = PARTITION + 16 * 3

assert MV == 1970


def put_word(out: bytearray, values: bytes) -> None:
    out.extend(values)
    out.extend(bytes(4 - len(values)))


def pack(ctx: bytes) -> bytes:
    assert len(ctx) == CTX_SIZE
    out = bytearray()
    put_word(out, bytes((ctx[TX8], ctx[TX8 + 1])))
    for off in (TX16, TX16 + 2, TX32, TX32 + 3):
        put_word(out, ctx[off:off + (2 if off < TX32 else 3)])
    for off in range(COEF, SKIP, 3):
        put_word(out, ctx[off:off + 3])
    put_word(out, ctx[SKIP:SKIP + 3])
    for off in range(INTER_MODE, INTERP_FILTER, 3):
        put_word(out, ctx[off:off + 3])
    for off in range(INTERP_FILTER, IS_INTER, 2):
        put_word(out, ctx[off:off + 2])
    put_word(out, ctx[IS_INTER:IS_INTER + 4])
    out.extend(ctx[COMP_MODE:COMP_MODE + 5] + bytes(3))
    out.extend(ctx[COMP_REF:COMP_REF + 5] + bytes(3))
    for off in range(SINGLE_REF, COMP_REF, 2):
        put_word(out, ctx[off:off + 2])
    out.extend(bytes(4))
    for off in range(Y_MODE, UV_MODE, 9):
        out.extend(ctx[off:off + 9] + bytes(7))
    for off in range(UV_MODE, PARTITION, 9):
        out.extend(ctx[off:off + 9] + bytes(7))
    for off in range(PARTITION, MV, 3):
        put_word(out, ctx[off:off + 3])
    put_word(out, ctx[MV:MV + 3])
    assert len(out) == 0xA94

    out.extend(bytes(HW_SIZE - len(out)))
    sign = MV + 3
    classes = sign + 2
    class0_bit = classes + 20
    bits = class0_bit + 2
    class0_fr = bits + 20
    fr = class0_fr + 12
    class0_hp = fr + 6
    hp = class0_hp + 2
    for component in range(2):
        base = 0xA94 + (1 - component) * 0x30
        out[base:base + 4] = bytes((
            ctx[sign + component], ctx[class0_bit + component],
            ctx[class0_hp + component], ctx[hp + component]))
        out[base + 4:base + 7] = ctx[class0_fr + component * 6:
                                          class0_fr + component * 6 + 3]
        out[base + 8:base + 11] = ctx[class0_fr + component * 6 + 3:
                                            class0_fr + component * 6 + 6]
        out[base + 0xC:base + 0x16] = ctx[classes + component * 10:
                                             classes + component * 10 + 10]
        out[base + 0x1C:base + 0x26] = ctx[bits + component * 10:
                                              bits + component * 10 + 10]
        out[base + 0x28:base + 0x2B] = ctx[fr + component * 3:
                                               fr + component * 3 + 3]
    return bytes(out)


def unpack(hw: bytes) -> bytes:
    assert len(hw) >= HW_SIZE
    hw = hw[:HW_SIZE]
    ctx = bytearray(CTX_SIZE)
    ctx[TX8:TX8 + 2] = hw[:2]
    ctx[TX16:TX16 + 2] = hw[4:6]
    ctx[TX16 + 2:TX16 + 4] = hw[8:10]
    ctx[TX32:TX32 + 3] = hw[12:15]
    ctx[TX32 + 3:TX32 + 6] = hw[16:19]
    pos = 0x14
    for off in range(COEF, SKIP, 3):
        ctx[off:off + 3] = hw[pos:pos + 3]
        pos += 4
    ctx[SKIP:SKIP + 3] = hw[pos:pos + 3]
    pos += 4
    for off in range(INTER_MODE, INTERP_FILTER, 3):
        ctx[off:off + 3] = hw[pos:pos + 3]
        pos += 4
    for off in range(INTERP_FILTER, IS_INTER, 2):
        ctx[off:off + 2] = hw[pos:pos + 2]
        pos += 4
    ctx[IS_INTER:IS_INTER + 4] = hw[pos:pos + 4]
    pos += 4
    ctx[COMP_MODE:COMP_MODE + 5] = hw[pos:pos + 5]
    pos += 8
    ctx[COMP_REF:COMP_REF + 5] = hw[pos:pos + 5]
    pos += 8
    for off in range(SINGLE_REF, COMP_REF, 2):
        ctx[off:off + 2] = hw[pos:pos + 2]
        pos += 4
    pos += 4
    for off in range(Y_MODE, UV_MODE, 9):
        ctx[off:off + 9] = hw[pos:pos + 9]
        pos += 16
    for off in range(UV_MODE, PARTITION, 9):
        ctx[off:off + 9] = hw[pos:pos + 9]
        pos += 16
    for off in range(PARTITION, MV, 3):
        ctx[off:off + 3] = hw[pos:pos + 3]
        pos += 4
    ctx[MV:MV + 3] = hw[pos:pos + 3]
    assert pos == 0xA90

    sign = MV + 3
    classes = sign + 2
    class0_bit = classes + 20
    bits = class0_bit + 2
    class0_fr = bits + 20
    fr = class0_fr + 12
    class0_hp = fr + 6
    hp = class0_hp + 2
    for component in range(2):
        base = 0xA94 + (1 - component) * 0x30
        ctx[sign + component] = hw[base]
        ctx[class0_bit + component] = hw[base + 1]
        ctx[class0_hp + component] = hw[base + 2]
        ctx[hp + component] = hw[base + 3]
        ctx[class0_fr + component * 6:class0_fr + component * 6 + 3] = \
            hw[base + 4:base + 7]
        ctx[class0_fr + component * 6 + 3:class0_fr + component * 6 + 6] = \
            hw[base + 8:base + 11]
        ctx[classes + component * 10:classes + component * 10 + 10] = \
            hw[base + 0xC:base + 0x16]
        ctx[bits + component * 10:bits + component * 10 + 10] = \
            hw[base + 0x1C:base + 0x26]
        ctx[fr + component * 3:fr + component * 3 + 3] = \
            hw[base + 0x28:base + 0x2B]
    return bytes(ctx)


def inv_recenter_nonneg(value: int, modulus: int) -> int:
    if value > 2 * modulus:
        return value
    if value & 1:
        return modulus - ((value + 1) >> 1)
    return modulus + (value >> 1)


def update_prob(delta: int, prob: int) -> int:
    if not delta:
        return prob
    if prob <= 128:
        return 1 + inv_recenter_nonneg(delta, prob - 1)
    return 255 - inv_recenter_nonneg(delta, 255 - prob)


def inverse_delta(old: int, new: int) -> int:
    if old == new:
        return 0
    for delta in range(1, 256):
        if update_prob(delta, old) == new:
            return delta
    raise ValueError(f"no VP9 update maps {old} to {new}")


def make_keyframe_control(default: bytes, target: bytes, tx_mode: int) -> bytes:
    delta = bytearray(CTX_SIZE)
    processed = []
    if tx_mode == 4:
        processed.extend(range(TX8, COEF))
    # BAND_6(0) is three contexts; the other coefficient bands use six.
    for tx in range(min(tx_mode, 3) + 1):
        for block_type in range(2):
            for intra in range(2):
                for band in range(6):
                    count = 3 if band == 0 else 6
                    start = COEF + (((((tx * 2 + block_type) * 2 + intra) * 6 +
                                      band) * 6) * 3)
                    processed.extend(range(start, start + count * 3))
    processed.extend(range(SKIP, SKIP + 3))
    for index in processed:
        delta[index] = inverse_delta(default[index], target[index])

    result = bytearray((tx_mode,))
    result.extend(delta)
    assert len(result) == CONTROL_SIZE

    check = bytearray(default)
    for index in processed:
        check[index] = update_prob(delta[index], check[index])
    if bytes(check) != target:
        differences = [i for i, (a, b) in enumerate(zip(check, target)) if a != b]
        raise ValueError(f"target changes {len(differences)} unprocessed contexts; "
                         f"first offsets: {differences[:12]}")
    return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("default", type=Path,
                        help="raw context, or ELF .rodata beginning with it")
    parser.add_argument("hardware", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--offset", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--tx-mode", type=int, required=True, choices=range(5))
    args = parser.parse_args()

    default = args.default.read_bytes()[:CTX_SIZE]
    if len(default) != CTX_SIZE:
        parser.error("default context is shorter than 2039 bytes")
    raw = args.hardware.read_bytes()
    hardware = raw[args.offset:args.offset + HW_SIZE]
    if len(hardware) != HW_SIZE:
        parser.error("hardware probability image is too short")
    target = unpack(hardware)
    if pack(target) != hardware:
        parser.error("internal H618 probability round-trip failed")
    control = make_keyframe_control(default, target, args.tx_mode)
    args.output.write_bytes(control)
    print(f"wrote {len(control)} bytes to {args.output}")


if __name__ == "__main__":
    main()
