#!/bin/sh
# Run Substrate in QEMU
#
# Usage:
#   ./run.sh              - Boot rootfs.img via BIOS (custom bootloader)
#   ./run.sh --grub       - Boot from disk.img (GRUB)
#   ./run.sh --kernel     - Boot via QEMU -kernel loader (legacy)

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
elif [ "$1" = "--grub" ]; then
    shift
    # GRUB mode: boot from partitioned disk.img
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
else
    # Default: boot rootfs.img via BIOS with custom bootloader
    if [ ! -f rootfs.img ]; then
        echo "rootfs.img not found. Run: ./build-rootfs.sh" >&2
        exit 1
    fi
    qemu-system-i386 \
      -drive file=rootfs.img,format=raw \
      -m 128M \
      -display none \
      -serial file:serial.log \
      -usb -device usb-kbd
fi
