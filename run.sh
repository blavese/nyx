#!/usr/bin/env bash
# run.sh          graphical window
# run.sh -t       headless, serial on stdout
# run.sh -T       headless self test, exits with the kernel's status
set -e
cd "$(dirname "$0")"
QEMU="${QEMU:-}"
[ -z "$QEMU" ] && QEMU=$(command -v qemu-system-i386 || true)
[ -z "$QEMU" ] && QEMU="/c/Program Files/qemu/qemu-system-i386.exe"
[ ! -x "$QEMU" ] && { echo "qemu-system-i386 not found" >&2; exit 1; }

bash build.sh >/dev/null

# A raw image file is the disk. It is created on first run and then
# persists, which is the whole point.
DISK="${NYX_DISK:-nyx.img}"
if [ ! -f "$DISK" ]; then
  echo "creating $DISK (16 MiB)"
  head -c 16777216 /dev/zero > "$DISK"
fi
COMMON=(-kernel build/nyx.elf -m 64 -no-reboot
        -drive "file=$DISK,format=raw,if=ide,index=0"
        -netdev user,id=n0 -device rtl8139,netdev=n0)
case "$1" in
  -T) shift
      set +e
      "$QEMU" "${COMMON[@]}" -append selftest -serial stdio -display none \
              -device isa-debug-exit,iobase=0xf4,iosize=0x04 "$@"
      st=$?
      # isa-debug-exit reports (code<<1)|1, so 1 means the kernel exited 0
      [ $st -eq 1 ] && exit 0
      exit $st ;;
  -t) shift; exec "$QEMU" "${COMMON[@]}" -serial stdio -display none "$@" ;;
  *)  exec "$QEMU" "${COMMON[@]}" -serial stdio "$@" ;;
esac
