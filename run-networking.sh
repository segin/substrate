#!/bin/sh
# Run substrate under qemu with networking.
#
# Two networking back-ends:
#
#   default (macvtap): bridge the guest NIC onto a real host interface via
#   macvtap.  The guest gets its own MAC on your LAN and DHCP reaches your
#   real router.  Host <-> guest traffic is NOT possible with macvtap by
#   design; use a bridge+tap setup if you need that.  Requires sudo and an
#   Ethernet-like default-route NIC (macvtap does NOT work on wifi or on
#   point-to-point tun/VPN devices — "RTNETLINK answers: Invalid argument").
#
#   --user (QEMU internal networking, slirp): the guest sits behind QEMU's
#   built-in NAT (gateway 10.0.2.2, guest DHCP -> 10.0.2.15).  No sudo, no
#   host NIC, no macvtap — works anywhere, including when your default route
#   is wifi or a VPN tun.  Outbound works; inbound needs hostfwd (set
#   $HOSTFWD, e.g. HOSTFWD=hostfwd=tcp::2222-:22).
#
# Override the host NIC (macvtap mode) via $NIC (default: the interface
# owning the default route).
set -eu

# Flags:
#   --gfx    add vga=1024x768@32 + -vga std so substrate's BGA fb driver
#            brings up a graphical framebuffer.  Without --gfx, qemu's
#            default display (hardware text mode) is used.
#   --kvm    opt in to -accel kvm.  Default is TCG software emulation
#            because KVM has a coherence bug on i386 that produces
#            transient single-byte read corruption right after a
#            SIGALRM is delivered to a userland handler (reproduced
#            by tests/lib/c/torture_heap_stdio sc9f; passes under
#            TCG).  Use --kvm when you specifically want speed and
#            understand the risk; otherwise leave it off.
#   --user   use QEMU internal (user-mode/slirp) networking instead of
#            macvtap-onto-real-LAN.  No sudo, no host NIC required.
#   --debug  start QEMU's GDB stub listening on tcp::1234 (override the port
#            with $GDBPORT).  By default the guest still boots normally and
#            you attach whenever you like (e.g. to break into a hang); set
#            GDBHALT=1 to also freeze the CPU at reset so you can set
#            breakpoints before boot and `continue` from gdb.
GFX=0
KVM=0
USERNET=0
DEBUG=0
for arg in "$@"; do
    case "$arg" in
        --gfx)   GFX=1 ;;
        --kvm)   KVM=1 ;;
        --user)  USERNET=1 ;;
        --debug) DEBUG=1 ;;
    esac
done

APPEND="root=/dev/storage/sata0 trap serial_debug"
GFX_ARGS=""
ACCEL_ARG=""
if [ "$GFX" -eq 1 ]; then
    APPEND="$APPEND vga=1024x768@32"
    GFX_ARGS="-vga std"
fi
if [ "$KVM" -eq 1 ]; then
    ACCEL_ARG="-accel kvm"
fi

# NETDEV_ARGS holds the qemu -netdev/-device pair for the chosen back-end.
# In macvtap mode the guest's tap is passed as fd 3 (opened with `exec`
# below) so the same single qemu invocation works for both back-ends.
if [ "$USERNET" -eq 1 ]; then
    # QEMU user-mode NAT.  $HOSTFWD lets you forward host ports in, e.g.
    #   HOSTFWD=hostfwd=tcp::2222-:22 ./run-networking.sh --user
    HOSTFWD=${HOSTFWD:-}
    NETDEV_ARGS="-netdev user,id=n0${HOSTFWD:+,$HOSTFWD} -device virtio-net-pci,netdev=n0"
    echo "run-networking.sh: QEMU user-mode networking (no macvtap/sudo)"
else
    NIC=${NIC:-$(ip -o route show default | awk '{print $5; exit}')}
    if [ -z "$NIC" ]; then
        echo "run-networking.sh: could not auto-detect a default-route NIC;" \
             "set NIC=<iface> or use --user for QEMU internal networking" >&2
        exit 1
    fi

    MACVTAP=${MACVTAP:-macvtap0}

    cleanup() {
        sudo ip link delete "$MACVTAP" 2>/dev/null || true
    }
    trap cleanup EXIT INT TERM

    # Tear down any leftover macvtap from a previous run before recreating.
    sudo ip link delete "$MACVTAP" 2>/dev/null || true

    if ! sudo ip link add link "$NIC" name "$MACVTAP" type macvtap mode bridge; then
        echo "run-networking.sh: macvtap on '$NIC' failed (wifi/tun/VPN NICs" \
             "are not supported); use --user for QEMU internal networking" >&2
        exit 1
    fi
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

    # Open the tap as fd 3 for qemu to inherit (-netdev tap,fd=3).
    exec 3<>"$TAPDEV"
    NETDEV_ARGS="-netdev tap,id=n0,fd=3,vhost=off -device virtio-net-pci,netdev=n0,mac=$MACADDR"
fi

# Prefer a kernel in the current directory; fall back to sys/.
if [ -f kernel.bin ]; then
    KERNEL=kernel.bin
elif [ -f sys/kernel.bin ]; then
    KERNEL=sys/kernel.bin
else
    echo "run-networking.sh: kernel.bin not found in . or sys/" >&2
    exit 1
fi

# --debug: expose QEMU's GDB stub.  -gdb tcp::PORT listens; -S (GDBHALT=1)
# additionally freezes the CPU at reset until the debugger continues.  For
# symbols, point gdb at the kernel ELF (sys/kernel.bin carries them).
DEBUG_ARGS=""
if [ "$DEBUG" -eq 1 ]; then
    GDBPORT=${GDBPORT:-1234}
    DEBUG_ARGS="-gdb tcp::$GDBPORT"
    HALTNOTE=""
    if [ "${GDBHALT:-0}" = 1 ]; then
        DEBUG_ARGS="$DEBUG_ARGS -S"
        HALTNOTE=" (CPU halted at reset; run 'continue' in gdb to boot)"
    fi
    echo "run-networking.sh: GDB stub on tcp::$GDBPORT$HALTNOTE"
    echo "    connect: gdb -ex 'symbol-file $KERNEL' -ex 'target remote :$GDBPORT'"
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

# Not exec'd: when qemu exits the script resumes and (macvtap mode) the
# EXIT trap tears the macvtap down.
#
# i8042=off disables the emulated PS/2 controller (both keyboard and
# mouse).  The PS/2 mouse path is janky/glitchy under substrate, and we
# supply usb-kbd + usb-mouse below, so dropping PS/2 leaves a single,
# clean USB pointer feeding /dev/input/event0 instead of an aggregate of
# a good USB mouse and a flaky PS/2 one.
qemu-system-i386 -cpu qemu32,+sse,+sse2 $ACCEL_ARG \
  -machine pc,i8042=off \
  -drive file=rootfs.img,format=raw,if=none,id=drive0 \
  -device ich9-ahci,id=sata0 \
  -device ide-hd,bus=sata0.0,unit=0,drive=drive0 \
  -device piix3-usb-uhci -device usb-kbd -device usb-mouse \
  $NETDEV_ARGS \
  -kernel "$KERNEL" \
  -append "$APPEND" \
  $GFX_ARGS \
  $DEBUG_ARGS \
  -serial stdio \
  -audio driver=sdl,model=ac97,id=audio0
