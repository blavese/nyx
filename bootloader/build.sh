#!/usr/bin/env bash
# Builds the bootloader. It is linked at 0x7C00 and flattened, because a BIOS
# loads a boot image as raw bytes and knows nothing about ELF.
set -e
cd "$(dirname "$0")/.."

ZIG="${ZIG:-$(command -v zig || true)}"
if [ -z "$ZIG" ]; then
  ZIG=$(find /c/Users/admin/AppData/Local/Microsoft/WinGet/Packages -iname zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && { echo "zig not found" >&2; exit 1; }

# GNU stat and BSD stat spell this differently, and macOS ships the BSD one.
filesize() { stat -c %s "$1" 2>/dev/null || stat -f %z "$1" 2>/dev/null || echo '?'; }

mkdir -p build
"$ZIG" cc -target x86-freestanding-none -mcpu=i686 \
  -ffreestanding -nostdlib -static -c \
  -o build/cdboot.o bootloader/cdboot.S

"$ZIG" ld.lld -T bootloader/link.ld -o build/cdboot.elf build/cdboot.o

python tools/flatten.py build/cdboot.elf build/cdboot.bin 0x7C00
echo "built build/cdboot.bin ($(filesize build/cdboot.bin) bytes)"

"$ZIG" cc -target x86-freestanding-none -mcpu=i686   -ffreestanding -nostdlib -static -c   -o build/mbr.o bootloader/mbr.S
"$ZIG" ld.lld -T bootloader/mbr.ld -o build/mbr.elf build/mbr.o
python tools/flatten.py build/mbr.elf build/mbr.bin 0x7C00
echo "built build/mbr.bin ($(filesize build/mbr.bin) bytes)"

# The SMP trampoline is the same kind of thing: 16-bit code that has to sit
# at a fixed low address with no relocation, so it is a flat binary too.
"$ZIG" cc -target x86-freestanding-none -mcpu=i686   -ffreestanding -nostdlib -static -c   -o build/trampoline.o bootloader/trampoline.S
"$ZIG" ld.lld -T bootloader/trampoline.ld -o build/trampoline.elf build/trampoline.o
python tools/flatten.py build/trampoline.elf build/trampoline.bin 0x8000
echo "built build/trampoline.bin ($(filesize build/trampoline.bin) bytes)"
