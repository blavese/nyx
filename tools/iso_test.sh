#!/usr/bin/env bash
# Boots the image the two ways a real machine would, and checks nyx came up.
#
# The kernel has always been handed to QEMU with -kernel, which means QEMU
# was doing the bootloader's job. Neither of the boots below has that help:
# one goes through El Torito the way a disc does, the other through the MBR
# the way a USB stick does, and both run bootloader/cdboot.S.
set -e
cd "$(dirname "$0")/.."

QEMU="${QEMU:-}"
[ -z "$QEMU" ] && QEMU=$(command -v qemu-system-i386 || true)
[ -z "$QEMU" ] && QEMU="/c/Program Files/qemu/qemu-system-i386.exe"

python tools/mkiso.py >/dev/null

ISO=build/nyx.iso
DISK=build/isotest.img
rm -f "$DISK"
head -c 33554432 /dev/zero > "$DISK"

fails=0
run() {
  local what="$1"; shift
  local out
  out=$(mktemp)
  # Type a command at it, so the check proves the shell is really running
  # rather than that the banner happened to be printed.
  (sleep 8
   s="write booted.txt $what"
   for (( i=0; i<${#s}; i++ )); do printf '%s' "${s:$i:1}"; sleep 0.03; done
   printf '\n'; sleep 1
   s="cat booted.txt"
   for (( i=0; i<${#s}; i++ )); do printf '%s' "${s:$i:1}"; sleep 0.03; done
   printf '\n'; sleep 2) \
    | timeout 70 "$QEMU" "$@" -m 64 -no-reboot -display none -serial stdio \
        > "$out" 2>&1 || true

  echo "--- $what ---"
  for want in "nyx 0.6.1" "video" "progs" "nyx>" "$what"; do
    if grep -qF "$want" "$out"; then echo "  PASS  $want"
    else echo "  FAIL  $want"; fails=$((fails+1)); fi
  done
  rm -f "$out"
}

echo "=== boot test ==="
run "booted from the disc" -cdrom "$ISO" -boot d \
    -drive "file=$DISK,format=raw,if=ide,index=0"

rm -f "$DISK"
head -c 33554432 /dev/zero > "$DISK"

run "booted from the stick" \
    -drive "file=$ISO,format=raw,if=ide,index=0" -boot c \
    -drive "file=$DISK,format=raw,if=ide,index=1"

rm -f "$DISK"
echo
if [ $fails -eq 0 ]; then echo "boot test: all checks passed"
else echo "boot test: $fails failed"; fi
exit $fails
