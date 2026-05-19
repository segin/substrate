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

exec qemu-system-i386 -cpu qemu32,+sse,+sse2 -accel kvm \
  -drive file=rootfs.img,format=raw,if=none,id=drive0 \
  -device ich9-ahci,id=sata0 \
  -device ide-hd,bus=sata0.0,unit=0,drive=drive0 \
  -device piix3-usb-uhci -device usb-kbd \
  -netdev tap,id=n0,fd=3,vhost=off \
  -device virtio-net-pci,netdev=n0,mac="$MACADDR" \
  -kernel sys/kernel.bin \
  -append "root=/dev/storage/sata0 serial_debug" \
  -serial stdio \
  -audio driver=sdl,model=ac97,id=audio0 \
  3<>"$TAPDEV"
