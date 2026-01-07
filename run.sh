#!/bin/sh
# Run Substrate in QEMU
# - Uses kernel.multiboot
# - Attaches root.img as Primary Master IDE drive
# - Passes root=/dev/storage/ide0 to kernel
# - Enables serial debug output to stdio

qemu-system-i386 \
  -kernel sys/kernel.multiboot \
  -m 128M \
  -display none \
  -serial file:serial.log \
  -drive file=root.img,format=raw,index=0,media=disk \
  -append "serial_debug root=/dev/storage/ide0"
