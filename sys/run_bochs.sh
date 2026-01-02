#!/bin/bash
set -e

# Create ISO directory structure
mkdir -p isodir/boot/grub

# Copy kernel
cp kernel.bin isodir/boot/kernel.bin

# Create GRUB config with the same arguments as QEMU
cat > isodir/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

menuentry "TestUnix" {
    multiboot /boot/kernel.bin serial_debug root=/dev/storage/ide0 init=/bin/busybox
    boot
}
EOF

# Create ISO
echo "Generating kernel.iso..."
grub-mkrescue -o kernel.iso isodir

# Run Bochs
echo "Starting Bochs..."
bochs -f bochsrc -q
