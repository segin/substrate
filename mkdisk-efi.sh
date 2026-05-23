#!/bin/sh
# mkdisk-efi.sh — build a GPT/UEFI bootable substrate disk.
#
# Layout:
#   GPT primary    (1 MiB)
#   Partition 1    100 MiB  FAT32 ESP (type EFI System)
#       /EFI/BOOT/BOOTX64.EFI       — standalone x86_64 GRUB
#       /EFI/BOOT/kernel.multiboot  — i486 substrate kernel
#   Partition 2    4096 MiB ext2 rootfs (rootfs.img copied verbatim)
#   GPT backup     (1 MiB)
#
# GRUB is built standalone (one .EFI binary) with grub.cfg embedded,
# so the ESP only needs that binary + the kernel.
#
# No sudo required — uses mtools for the FAT side and dd for the
# ext2 side; the partition table is laid out by parted on a plain
# file.
#
# Outputs disk-efi.img in the current directory.

set -eu

DISK=disk-efi.img
KERNEL=sys/kernel.fb.multiboot
ROOTFS=rootfs.img

ESP_MIB=100
ROOT_MIB=4096
HEAD_MIB=1      # GPT primary
TAIL_MIB=1      # GPT backup
TOTAL_MIB=$((HEAD_MIB + ESP_MIB + ROOT_MIB + TAIL_MIB))

if [ ! -f "$KERNEL" ]; then
    echo "mkdisk-efi: $KERNEL missing — run \`make -C sys\` first" >&2
    exit 1
fi
if [ ! -f "$ROOTFS" ]; then
    echo "mkdisk-efi: $ROOTFS missing — run \`./build-rootfs.sh --image\` first" >&2
    exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# ------------------------------------------------------------------
# 1. Build BOOTX64.EFI with grub.cfg embedded.
#
# The kernel and the cfg live on the ESP we are about to create;
# the GRUB EFI loader looks up its embedded cfg, which uses paths
# relative to (hd0,gpt1) — i.e. the ESP root.
# ------------------------------------------------------------------

cat > "$WORK/grub.cfg" <<'GRUBCFG'
# Serial console mirrored to the EFI console — works headless.
serial --unit=0 --speed=115200
terminal_input  serial console
terminal_output serial console

set timeout=3
set default=0

# Locate the ESP by FAT label.  grub-mkstandalone doesn't always
# set $root, and an explicit `search` is more robust than hardcoding
# (hd0,gpt1) — that breaks the moment QEMU enumerates a CD/USB first.
search --label SUBSTRATE --set=root --no-floppy

menuentry "Substrate (i486)" {
    multiboot2 /EFI/BOOT/kernel.multiboot serial_debug root=/dev/storage/sata0p2
}

menuentry "Substrate (no serial debug)" {
    multiboot2 /EFI/BOOT/kernel.multiboot root=/dev/storage/sata0p2
}
GRUBCFG

echo "==> Building BOOTX64.EFI (x86_64-efi standalone)..."
grub-mkstandalone -O x86_64-efi \
    --modules="multiboot multiboot2 part_gpt fat ext2 normal configfile serial echo loadenv test \
               all_video efi_gop efi_uga video_bochs video_cirrus gfxterm gfxmenu" \
    -o "$WORK/BOOTX64.EFI" \
    "boot/grub/grub.cfg=$WORK/grub.cfg"

# ------------------------------------------------------------------
# 2. Build the FAT32 ESP image and populate via mtools (no mount).
# ------------------------------------------------------------------

ESP="$WORK/esp.img"
echo "==> Creating ${ESP_MIB} MiB FAT32 ESP..."
truncate -s "${ESP_MIB}M" "$ESP"
mkfs.fat -F 32 -n SUBSTRATE "$ESP" >/dev/null

mmd    -i "$ESP" ::/EFI
mmd    -i "$ESP" ::/EFI/BOOT
mcopy  -i "$ESP" "$WORK/BOOTX64.EFI"     ::/EFI/BOOT/BOOTX64.EFI
mcopy  -i "$ESP" "$KERNEL"               ::/EFI/BOOT/kernel.multiboot

# ------------------------------------------------------------------
# 3. Assemble the GPT disk and stamp the partitions into place.
# ------------------------------------------------------------------

echo "==> Creating $DISK ($TOTAL_MIB MiB)..."
truncate -s "${TOTAL_MIB}M" "$DISK"

parted --script "$DISK" \
    mklabel gpt \
    mkpart ESP    fat32 "${HEAD_MIB}MiB"  "$((HEAD_MIB + ESP_MIB))MiB" \
    set    1 esp on \
    mkpart rootfs ext2  "$((HEAD_MIB + ESP_MIB))MiB" \
                        "$((HEAD_MIB + ESP_MIB + ROOT_MIB))MiB"

echo "==> Writing ESP into partition 1..."
dd if="$ESP"    of="$DISK" bs=1M seek="$HEAD_MIB" \
   conv=notrunc status=none

echo "==> Writing $ROOTFS into partition 2..."
dd if="$ROOTFS" of="$DISK" bs=1M seek="$((HEAD_MIB + ESP_MIB))" \
   conv=notrunc status=none

echo "==> Done."
ls -lh "$DISK"
echo
echo "Boot with: ./run-efi.sh"
