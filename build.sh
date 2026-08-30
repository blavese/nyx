#!/usr/bin/env bash
# Builds the kernel using zig's bundled clang+lld as a cross compiler, so no
# separate i686-elf toolchain is needed.
#
# -mcpu=i686 matters: zig's default x86 target has SSE2 on, and clang will
# happily emit movd/xmm for 64-bit integer moves. A kernel that has not set
# CR4.OSFXSR takes #UD on the first one.
set -e
cd "$(dirname "$0")"

ZIG="${ZIG:-$(command -v zig || true)}"
if [ -z "$ZIG" ]; then
  ZIG=$(find /c/Users/admin/AppData/Local/Microsoft/WinGet/Packages -iname zig.exe 2>/dev/null | head -1)
fi
[ -z "$ZIG" ] && { echo "zig not found; set ZIG=/path/to/zig" >&2; exit 1; }

mkdir -p build

# The user programs and the SMP trampoline go inside the kernel image, so
# they are built first.
ZIG="$ZIG" bash userland/build.sh
ZIG="$ZIG" bash bootloader/build.sh

SRC=$(find boot kernel -name '*.c' -o -name '*.S' | sort | tr '\n' ' ')

"$ZIG" cc -target x86-freestanding-none -mcpu=i686 \
  -ffreestanding -nostdlib -static -O2 -std=gnu11 \
  -fno-sanitize=undefined -fno-stack-protector -fno-stack-check \
  -fno-builtin -fno-omit-frame-pointer \
  -mno-sse -mno-sse2 -mno-mmx -mno-80387 -mno-red-zone \
  -Wall -Wextra -Wno-unused-parameter \
  -Iinclude \
  -Wl,-T,linker.ld -Wl,--build-id=none -Wl,-z,max-page-size=4096 \
  -o build/nyx.elf $SRC

echo "built build/nyx.elf ($(stat -c %s build/nyx.elf 2>/dev/null || echo ?) bytes)"
