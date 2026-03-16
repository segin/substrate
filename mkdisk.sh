#!/bin/sh
# mkdisk.sh - Create a bootable 50MB disk image with GRUB
#
# Partition layout (MBR):
#   Partition 1: ~20MB ext2 (/boot) with GRUB + kernel
#   Partition 2: ~30MB ext2 (rootfs) from root.img
#
# Usage: sudo ./mkdisk.sh
#
# Requires: sfdisk, losetup, mkfs.ext2, grub-install, mount

set -e

DISK_IMG="disk.img"
ROOT_IMG="root.img"
KERNEL="sys/kernel.multiboot"
BOOT_START=2048                     # Sector 2048 (1MB alignment)
BOOT_SECTORS=40960                  # 20 MB
ROOT_SECTORS=61440                  # 30 MB (matches root.img exactly)
TOTAL_SECTORS=$((BOOT_START + BOOT_SECTORS + ROOT_SECTORS + 2048))  # + padding
DISK_SIZE=$((TOTAL_SECTORS * 512))

# Sanity checks
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: must run as root (need losetup/mount/grub-install)" >&2
    exit 1
fi

if [ ! -f "$ROOT_IMG" ]; then
    echo "Error: $ROOT_IMG not found" >&2
    exit 1
fi

if [ ! -f "$KERNEL" ]; then
    echo "Error: $KERNEL not found (run make -C sys first)" >&2
    exit 1
fi

cleanup() {
    set +e
    [ -n "$BOOT_MNT" ] && umount "$BOOT_MNT" 2>/dev/null
    [ -n "$BOOT_MNT" ] && rmdir "$BOOT_MNT" 2>/dev/null
    [ -n "$LOOP" ] && losetup -d "$LOOP" 2>/dev/null
}
trap cleanup EXIT

echo "==> Creating $((DISK_SIZE / 1024 / 1024))MB disk image ($TOTAL_SECTORS sectors)..."
dd if=/dev/zero of="$DISK_IMG" bs=512 count="$TOTAL_SECTORS" status=none

echo "==> Writing MBR partition table..."
sfdisk --quiet "$DISK_IMG" <<EOF
label: dos

start=$BOOT_START, size=$BOOT_SECTORS, type=83, bootable
start=$((BOOT_START + BOOT_SECTORS)), size=$ROOT_SECTORS, type=83
EOF

echo "==> Setting up loop device..."
LOOP=$(losetup --find --show --partscan "$DISK_IMG")
echo "    Loop device: $LOOP"

# Wait for partition devices to appear
sleep 1

PART1="${LOOP}p1"
PART2="${LOOP}p2"

if [ ! -b "$PART1" ] || [ ! -b "$PART2" ]; then
    echo "Error: partition devices not found ($PART1, $PART2)" >&2
    exit 1
fi

echo "==> Formatting partition 1 (boot) as ext2..."
mkfs.ext2 -q -L boot "$PART1"

echo "==> Copying root.img to partition 2..."
dd if="$ROOT_IMG" of="$PART2" bs=1M status=none

echo "==> Mounting boot partition..."
BOOT_MNT=$(mktemp -d)
mount "$PART1" "$BOOT_MNT"

echo "==> Installing kernel..."
mkdir -p "$BOOT_MNT/boot/grub"
cp "$KERNEL" "$BOOT_MNT/boot/kernel.multiboot"

echo "==> Writing grub.cfg..."
cat > "$BOOT_MNT/boot/grub/grub.cfg" <<'GRUBCFG'
# Serial console for headless operation
serial --unit=0 --speed=115200
terminal_input serial console
terminal_output serial console

set timeout=3
set default=0

menuentry "Substrate" {
    multiboot /boot/kernel.multiboot serial_debug root=/dev/storage/ide0p2
}

menuentry "Substrate (no serial debug)" {
    multiboot /boot/kernel.multiboot root=/dev/storage/ide0p2
}
GRUBCFG

echo "==> Installing GRUB..."
grub-install --target=i386-pc --boot-directory="$BOOT_MNT/boot" --no-floppy "$LOOP"

echo "==> Unmounting..."
umount "$BOOT_MNT"
rmdir "$BOOT_MNT"
BOOT_MNT=""

echo "==> Detaching loop device..."
losetup -d "$LOOP"
LOOP=""

# Fix ownership so non-root user can use the image with QEMU
if [ -n "$SUDO_UID" ] && [ -n "$SUDO_GID" ]; then
    chown "$SUDO_UID:$SUDO_GID" "$DISK_IMG"
fi

echo "==> Done: $DISK_IMG"
echo "    Boot with: qemu-system-i386 -drive file=$DISK_IMG,format=raw -m 128M"
