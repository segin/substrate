#!/bin/sh
# Run substrate under qemu with the guest NIC bridged onto a real host
# interface via macvtap.  The guest gets its own MAC on your LAN and
# DHCP reaches your real router.  Host <-> guest traffic is NOT
# possible with macvtap by design; use a bridge+tap setup if you need
# that.
#
# Override the host NIC via $NIC (default: the interface owning the
# default route).
set -eu

NIC=${NIC:-$(ip -o route show default | awk '{print $5; exit}')}
if [ -z "$NIC" ]; then
    echo "run-networking.sh: could not auto-detect a default-route NIC;" \
         "set NIC=<iface> and retry" >&2
    exit 1
fi

MACVTAP=${MACVTAP:-macvtap0}

# --gfx adds vga=1024x768@32 to the kernel cmdline and forces -vga std
# so substrate's BGA fb driver brings up a graphical framebuffer.
# Without --gfx the rest of the qemu command line is unchanged
# (hardware text mode through qemu's default display).
GFX=0
for arg in "$@"; do
    case "$arg" in
        --gfx) GFX=1 ;;
    esac
done

APPEND="root=/dev/storage/sata0 trap serial_debug"
GFX_ARGS=""
if [ "$GFX" -eq 1 ]; then
    APPEND="$APPEND vga=1024x768@32"
    GFX_ARGS="-vga std"
fi

cleanup() {
    sudo ip link delete "$MACVTAP" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Tear down any leftover macvtap from a previous run before recreating.
sudo ip link delete "$MACVTAP" 2>/dev/null || true

sudo ip link add link "$NIC" name "$MACVTAP" type macvtap mode bridge
sudo ip link set "$MACVTAP" up

TAPIDX=$(cat "/sys/class/net/$MACVTAP/ifindex")
MACADDR=$(cat "/sys/class/net/$MACVTAP/address")
TAPDEV="/dev/tap$TAPIDX"

# udev creates /dev/tap$N a moment after the link comes up — wait for it.
i=0
while [ ! -e "$TAPDEV" ] && [ $i -lt 50 ]; do
    sleep 0.1
    i=$((i + 1))
done
if [ ! -e "$TAPDEV" ]; then
    echo "run-networking.sh: $TAPDEV never appeared" >&2
    exit 1
fi

sudo chown "$USER" "$TAPDEV"

echo "run-networking.sh: $MACVTAP up on $NIC, MAC $MACADDR, $TAPDEV"

# Prefer a kernel in the current directory; fall back to sys/.
if [ -f kernel.bin ]; then
    KERNEL=kernel.bin
elif [ -f sys/kernel.bin ]; then
    KERNEL=sys/kernel.bin
else
    echo "run-networking.sh: kernel.bin not found in . or sys/" >&2
    exit 1
fi

# Find the rootfs image.  If only the compressed form (rootfs.img.zst)
# exists, decompress it in place — the user normally distributes the
# compressed copy via git-annex / scp / similar.  rootfs.img wins if
# both are present (most likely just-rebuilt and not yet re-compressed).
if [ -f rootfs.img ]; then
    :
elif [ -f rootfs.img.zst ]; then
    if ! command -v zstd >/dev/null 2>&1; then
        echo "run-networking.sh: rootfs.img.zst present but zstd not installed" >&2
        exit 1
    fi
    echo "run-networking.sh: decompressing rootfs.img.zst -> rootfs.img"
    zstd -d --keep -- rootfs.img.zst
else
    echo "run-networking.sh: neither rootfs.img nor rootfs.img.zst found" >&2
    exit 1
fi

# Not exec'd: when qemu exits the script resumes and the EXIT trap
# tears the macvtap down.
qemu-system-i386 -cpu qemu32,+sse,+sse2 -accel kvm \
  -drive file=rootfs.img,format=raw,if=none,id=drive0 \
  -device ich9-ahci,id=sata0 \
  -device ide-hd,bus=sata0.0,unit=0,drive=drive0 \
  -device piix3-usb-uhci -device usb-kbd -device usb-mouse \
  -netdev tap,id=n0,fd=3,vhost=off \
  -device virtio-net-pci,netdev=n0,mac="$MACADDR" \
  -kernel "$KERNEL" \
  -append "$APPEND" \
  $GFX_ARGS \
  -serial stdio \
  -audio driver=sdl,model=ac97,id=audio0 \
  3<>"$TAPDEV"
