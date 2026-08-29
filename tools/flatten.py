#!/usr/bin/env python3
"""Turns an ELF into the flat image a bootloader can copy into place.

A BIOS hands control to raw bytes at a fixed address, and the kernel is
loaded by our own bootloader rather than by something that understands ELF.
Both of them want the same thing: every PT_LOAD segment laid out at the
address it asks for, gaps filled with zeros.

  flatten.py IN.elf OUT.bin [BASE]

BASE defaults to the lowest address any segment asks for. It also prints the
entry point, which is what the image builder patches into the loader.
"""
import struct
import sys


def load_segments(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        raise SystemExit("%s is not an ELF file" % path)
    if data[4] != 1:
        raise SystemExit("only 32-bit ELF is handled")

    entry = struct.unpack_from("<I", data, 24)[0]
    phoff = struct.unpack_from("<I", data, 28)[0]
    phentsize = struct.unpack_from("<H", data, 42)[0]
    phnum = struct.unpack_from("<H", data, 44)[0]

    segments = []
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type, p_off, p_vaddr, p_paddr, p_filesz, p_memsz, _flags, _align = \
            struct.unpack_from("<8I", data, off)
        if p_type != 1 or p_memsz == 0:      # PT_LOAD only
            continue
        segments.append((p_paddr, p_memsz, data[p_off:p_off + p_filesz]))

    if not segments:
        raise SystemExit("%s has nothing to load" % path)
    return entry, segments


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    src, dst = sys.argv[1], sys.argv[2]
    entry, segments = load_segments(src)

    base = int(sys.argv[3], 0) if len(sys.argv) > 3 else min(s[0] for s in segments)
    end = max(paddr + memsz for paddr, memsz, _ in segments)
    if base > min(s[0] for s in segments):
        raise SystemExit("a segment sits below the base address")

    image = bytearray(end - base)
    for paddr, _memsz, content in segments:
        at = paddr - base
        image[at:at + len(content)] = content

    open(dst, "wb").write(image)
    print("base %#x  entry %#x  %d bytes" % (base, entry, len(image)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
