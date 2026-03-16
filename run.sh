#!/bin/sh
# Run Substrate in QEMU
#
# Usage:
#   ./run.sh              - Boot from disk.img (GRUB)
#   ./run.sh --kernel     - Boot via QEMU -kernel loader (legacy)
#   ./run.sh [extra args] - Passed to kernel command line (legacy mode)

if [ "$1" = "--kernel" ]; then
    shift
    # Legacy mode: QEMU loads kernel directly
    qemu-system-i386 \
      -kernel sys/kernel.multiboot \
      -m 128M \
      -display none \
      -serial file:serial.log \
      -drive file=root.img,format=raw,index=0,media=disk \
      -usb -device usb-kbd \
      -append "serial_debug root=/dev/storage/ide0 $*"
else
    # Default: boot from GRUB disk image
    if [ ! -f disk.img ]; then
        echo "disk.img not found. Run: sudo ./mkdisk.sh" >&2
        exit 1
    fi
    qemu-system-i386 \
      -drive file=disk.img,format=raw \
      -m 128M \
      -display none \
      -serial file:serial.log \
      -usb -device usb-kbd
fi
