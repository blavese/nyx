#!/usr/bin/env bash
# Builds each user program into a standalone ELF executable. These are not
# linked against the kernel in any way: the only thing they share with it is
# the system call numbers in nyx.h.
set -e
cd "$(dirname "$0")"

ZIG="${ZIG:-$(command -v zig || true)}"
if [ -z "$ZIG" ]; then
  ZIG=$(find /c/Users/admin/AppData/Local/Microsoft/WinGet/Packages -iname zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && { echo "zig not found" >&2; exit 1; }

mkdir -p ../build/user
for src in *.c; do
  name="${src%.c}"
  "$ZIG" cc -target x86-freestanding-none -mcpu=i686 \
    -ffreestanding -nostdlib -static -O2 -std=gnu11 \
    -fno-sanitize=undefined -fno-stack-protector -fno-stack-check \
    -fno-builtin -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
    -Wall -Wextra \
    -Wl,-T,link.ld -Wl,--build-id=none \
    -o "../build/user/$name.elf" "$src"
  echo "  $name.elf  $(stat -c %s "../build/user/$name.elf") bytes"
done
