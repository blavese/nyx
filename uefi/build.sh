#!/usr/bin/env bash
# Builds the UEFI loader. It is a PE executable rather than an ELF, because
# that is what firmware loads, and it is compiled for the Microsoft calling
# convention because that is what firmware calls with.
set -e
cd "$(dirname "$0")/.."

ZIG="${ZIG:-$(command -v zig || true)}"
if [ -z "$ZIG" ]; then
  ZIG=$(find /c/Users/admin/AppData/Local/Microsoft/WinGet/Packages -iname zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && { echo "zig not found" >&2; exit 1; }

filesize() { stat -c %s "$1" 2>/dev/null || stat -f %z "$1" 2>/dev/null || echo '?'; }

mkdir -p build
"$ZIG" cc -target x86_64-uefi \
  -ffreestanding -nostdlib -fshort-wchar \
  -fno-sanitize=undefined -fno-stack-protector -fno-stack-check \
  -mno-red-zone -O2 -std=gnu11 \
  -Wall -Wextra -Wno-unused-parameter \
  -o build/BOOTX64.EFI uefi/loader.c

echo "built build/BOOTX64.EFI ($(filesize build/BOOTX64.EFI) bytes)"
