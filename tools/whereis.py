#!/usr/bin/env python3
"""Map a kernel address back to the function containing it, straight from
the ELF symbol table. Saves needing binutils on the build machine."""
import struct, sys

def symbols(path):
    d = open(path, "rb").read()
    assert d[:4] == b"\x7fELF" and d[4] == 1, "expected 32-bit ELF"
    e_shoff, = struct.unpack_from("<I", d, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size, link, info, align, entsize = \
            struct.unpack_from("<IIIIIIIIII", d, off)
        secs.append(dict(name=name, type=typ, addr=addr, offset=offset,
                         size=size, link=link, entsize=entsize))
    shstr = secs[e_shstrndx]
    def sname(n):
        s = d[shstr["offset"] + n:]
        return s[:s.index(b"\0")].decode()
    out = []
    for s in secs:
        if s["type"] != 2:      # SHT_SYMTAB
            continue
        strtab = secs[s["link"]]
        cnt = s["size"] // s["entsize"]
        for i in range(cnt):
            off = s["offset"] + i * s["entsize"]
            nm, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, off)
            raw = d[strtab["offset"] + nm:]
            nm = raw[:raw.index(b"\0")].decode(errors="replace")
            if nm and value:
                out.append((value, size, nm))
    return sorted(out)

if __name__ == "__main__":
    syms = symbols(sys.argv[1])
    for a in sys.argv[2:]:
        addr = int(a, 0)
        best = None
        for value, size, nm in syms:
            if value <= addr and (size == 0 or addr < value + size):
                if best is None or value > best[0]:
                    best = (value, size, nm)
        if best:
            print(f"{addr:#010x}  in  {best[2]}  (+{addr - best[0]} of {best[1]} bytes, starts {best[0]:#010x})")
        else:
            print(f"{addr:#010x}  no symbol found")
