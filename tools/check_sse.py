#!/usr/bin/env python3
"""Fail the build if an SSE or MMX opcode reached an executable section.

A kernel that has not enabled the FPU takes #UD on the first one, and the
fault lands somewhere far from the cause, so this is worth catching at build
time. That happened once already: zig's default x86 target has SSE2 on, and
clang used `movd xmm0` for a 64-bit integer move.

This is a byte scanner, not a disassembler. It does not know where
instructions begin, so it can match bytes that are really part of a preceding
instruction's immediate operand. String addresses pushed before a call are the
common case: `push $0x00110f00` contains the bytes 0f 11 00, which look like
`movups`. Those are filtered by checking whether the surrounding bytes read as
an address inside the image; anything that does is an operand, not an opcode.

The unambiguous forms (66 0F ..., the operand-size prefixed SSE2 moves) are
always reported, because no immediate produces that prefix by accident.
"""
import struct
import sys

# 66 0F xx: operand-size prefixed SSE2. Unambiguous.
PREFIXED = {0x6E, 0x7E, 0xD6, 0x6F, 0x7F, 0x28, 0x29}
# 0F xx on its own: real SSE, but also a common byte pair inside immediates.
BARE = {0x10, 0x11, 0x28, 0x29, 0x58, 0x59, 0x6E, 0x7E}


def sections(data):
    e_shoff, = struct.unpack_from("<I", data, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    out = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size, *_ = struct.unpack_from("<IIIIIIIIII", data, o)
        out.append(dict(name=name, type=typ, flags=flags, addr=addr, off=off, size=size))
    shstr = out[e_shstrndx]
    for s in out:
        raw = data[shstr["off"] + s["name"]:]
        s["label"] = raw[:raw.index(b"\0")].decode(errors="replace")
    return out


def image_range(secs):
    lo = min((s["addr"] for s in secs if s["addr"]), default=0)
    hi = max((s["addr"] + s["size"] for s in secs if s["addr"]), default=0)
    return lo, hi


def main():
    data = open(sys.argv[1], "rb").read()
    secs = sections(data)
    lo, hi = image_range(secs)

    hard, filtered = [], 0

    for s in secs:
        if s["type"] != 1 or not (s["flags"] & 0x4):
            continue
        body = data[s["off"]:s["off"] + s["size"]]

        for j in range(len(body) - 2):
            prefixed = body[j] == 0x66 and body[j + 1] == 0x0F and body[j + 2] in PREFIXED
            bare = body[j] == 0x0F and body[j + 1] in BARE
            if not prefixed and not bare:
                continue

            if bare and not prefixed:
                # Does this sit inside a 32-bit immediate holding an address?
                # First handle an address at the start of a known immediate.
                # This is common for `push $string`, where the first two bytes
                # of the address can otherwise look exactly like an opcode.
                looks_like_operand = False
                address_immediate = (
                    j > 0 and
                    (body[j - 1] == 0x68 or
                     0xB8 <= body[j - 1] <= 0xBF or
                     body[j - 1] in (0xA1, 0xA3))
                )
                if address_immediate and j + 4 <= len(body):
                    value, = struct.unpack_from("<I", body, j)
                    looks_like_operand = lo <= value < hi

                # Also try alignments that place the bytes mid-operand.
                for back in (1, 2, 3):
                    k = j - back
                    if k < 0 or k + 4 > len(body):
                        continue
                    value, = struct.unpack_from("<I", body, k)
                    if lo <= value < hi:
                        looks_like_operand = True
                        break
                if looks_like_operand:
                    filtered += 1
                    continue

            hard.append((s["addr"] + j, s["label"], body[j:j + 6].hex()))

    print(f"SSE/MMX opcodes in executable sections: {len(hard)}"
          f"   ({filtered} byte matches filtered as operands)")
    for addr, label, raw in hard[:10]:
        print(f"  {addr:#010x} in {label}: {raw}")

    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
