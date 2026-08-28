#!/usr/bin/env python3
"""Fail the build if any SSE/MMX opcode reached an executable section.
A kernel that has not enabled the FPU takes #UD on the first one, and the
fault lands somewhere far from the cause."""
import struct, sys
d = open(sys.argv[1], "rb").read()
e_shoff, = struct.unpack_from("<I", d, 0x20)
e_shentsize, e_shnum, _ = struct.unpack_from("<HHH", d, 0x2E)
bad = []
for i in range(e_shnum):
    o = e_shoff + i * e_shentsize
    _, typ, flags, addr, off, size, *_r = struct.unpack_from("<IIIIIIIIII", d, o)
    if typ == 1 and (flags & 0x4):
        b = d[off:off + size]
        for j in range(len(b) - 2):
            if b[j] == 0x66 and b[j+1] == 0x0F and b[j+2] in (0x6E,0x7E,0xD6,0x6F,0x7F,0x28,0x29):
                bad.append(addr + j)
            elif b[j] == 0x0F and b[j+1] in (0x10,0x11,0x28,0x29,0x58,0x59,0x6E,0x7E):
                bad.append(addr + j)
print(f"SSE/MMX opcodes in executable sections: {len(bad)}")
if bad:
    print("  first at", hex(bad[0]))
    sys.exit(1)
