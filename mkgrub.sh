#!/bin/sh
# mkgrub.sh — build a bootable GRUB rescue ISO carrying the substrate kernel,
# and (with --run) boot it under qemu with rootfs.img attached.
#
# This is the GRUB boot path.  It is real: GRUB loads the multiboot kernel,
# and the framebuffer variant reaches the graphical login.  The historical
# note that "GRUB2 does not boot substrate at all" was a misdiagnosis --
# nothing was wrong with the multiboot handoff.  Booting from an ISO always
# presents an optical drive, and enumerating a CD-ROM used to smash the
# kernel stack during partition scanning (a 2048-byte sector read into the
# sniffers' 512-byte stack buffers), so the machine reset-looped before it
# ever got far enough to look like a boot.  Fixed in geom_read_sector_bounded().
#
# Two menu entries are produced:
#
#   text mode    kernel.bin      multiboot mode_type=1, EGA text 80x25.
#   framebuffer  kernel.fb.bin   multiboot mode_type=0, linear FB.  GRUB picks
#                                the resolution and passes it in the multiboot
#                                info; the kernel inherits it with no vga=
#                                argument.  This is the entry that reaches X.
#
# The framebuffer entry is the default -- it is the one that gets you a
# graphical login.
#
# Usage:
#   ./mkgrub.sh                 build grub.iso only
#   ./mkgrub.sh --run           build, then boot it under qemu (serial to stdout)
#   ./mkgrub.sh --run --text    boot the text-mode entry instead
#
# Requires grub-mkrescue + xorriso (and mtools for the EFI bits).
set -eu

RUN=0
DEFAULT_ENTRY=1          # 1 = framebuffer, 0 = text
ISO=grub.iso

while [ $# -gt 0 ]; do
    case "$1" in
        --run)   RUN=1 ;;
        --text)  DEFAULT_ENTRY=0 ;;
        --iso=*) ISO="${1#--iso=}" ;;
        *) echo "mkgrub.sh: unknown argument '$1'" >&2; exit 1 ;;
    esac
    shift
done

for t in grub-mkrescue xorriso; do
    command -v "$t" >/dev/null 2>&1 || {
        echo "mkgrub.sh: $t not installed" >&2; exit 1; }
done

if [ ! -f sys/kernel.bin ] || [ ! -f sys/kernel.fb.bin ]; then
    echo "mkgrub.sh: sys/kernel.bin or sys/kernel.fb.bin missing -- run 'make -C sys' first" >&2
    exit 1
fi

# Sanity-check the handoff before building an image around it: grub-file is
# the same parser GRUB itself uses, so a "not multiboot" here means the
# header is broken, not that GRUB dislikes us.
for k in sys/kernel.bin sys/kernel.fb.bin; do
    if ! grub-file --is-x86-multiboot "$k"; then
        echo "mkgrub.sh: $k is not a valid x86 multiboot image" >&2
        exit 1
    fi
done

ISODIR=$(mktemp -d)
trap 'rm -rf "$ISODIR"' EXIT

mkdir -p "$ISODIR/boot/grub"
cp sys/kernel.bin    "$ISODIR/boot/kernel.bin"
cp sys/kernel.fb.bin "$ISODIR/boot/kernel.fb.bin"

# serial + console on both input and output so a headless run is debuggable.
cat > "$ISODIR/boot/grub/grub.cfg" <<EOF
serial --unit=0 --speed=115200
terminal_output serial console
terminal_input serial console
set timeout=3
set default=$DEFAULT_ENTRY

menuentry "substrate (text mode)" {
    multiboot /boot/kernel.bin serial_debug root=/dev/storage/sata0
    boot
}

menuentry "substrate (framebuffer)" {
    multiboot /boot/kernel.fb.bin serial_debug root=/dev/storage/sata0
    boot
}
EOF

grub-mkrescue -o "$ISO" "$ISODIR" >/dev/null 2>&1
echo "mkgrub.sh: wrote $ISO ($(( $(stat -c %s "$ISO") / 1024 / 1024 )) MiB)"

[ "$RUN" -eq 1 ] || exit 0

if [ ! -f rootfs.img ]; then
    echo "mkgrub.sh: rootfs.img missing -- run ./build-rootfs.sh --image first" >&2
    exit 1
fi

echo "mkgrub.sh: booting $ISO (rootfs.img on sata0, -snapshot so it stays pristine)"

# -snapshot: the ISO boot is a test path; keep rootfs.img untouched.
# +rdrand: the kernel RNG blocks forever without it on a headless/X boot.
exec qemu-system-i386 -cpu qemu32,+sse,+sse2,+rdrand -accel kvm \
    -m 512M -machine pc,i8042=off -snapshot -no-reboot -vga std \
    -cdrom "$ISO" -boot d \
    -device ich9-ahci,id=sata0 \
    -drive file=rootfs.img,format=raw,if=none,id=drive0 \
    -device ide-hd,bus=sata0.0,unit=0,drive=drive0 \
    -device piix3-usb-uhci,id=usbctl \
    -device usb-kbd,bus=usbctl.0 -device usb-mouse,bus=usbctl.0 \
    -serial stdio
