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
#   --gfx[=WxH@bpp]
#            add `vga=<mode>` + -vga std so substrate's fb driver brings up a
#            graphical framebuffer.  Bare --gfx uses 1024x768@32 (BGA linear);
#            pass a mode to override, e.g. --gfx=640x480@4 (planar VGA) or
#            --gfx=800x600@16.  Without --gfx at all, qemu's default display
#            (hardware text mode) is used.
#   --kvm    opt in to -accel kvm.  Default is TCG software emulation
#            because KVM has a coherence bug on i386 that produces
#            transient single-byte read corruption right after a
#            SIGALRM is delivered to a userland handler (reproduced
#            by tests/lib/c/torture_heap_stdio sc9f; passes under
#            TCG).  Use --kvm when you specifically want speed and
#            understand the risk; otherwise leave it off.
#   --snapshot
#            add qemu's -snapshot: every writable disk (rootfs.img and any
#            --drive / --drive-ctrl images) is backed by a temporary overlay,
#            so all guest writes are discarded on exit and the on-disk images
#            stay byte-for-byte pristine.  Use this for throwaway / test boots
#            so a botched session can't corrupt rootfs.img.
#   --user   use QEMU internal (user-mode/slirp) networking instead of
#            macvtap-onto-real-LAN.  No sudo, no host NIC required.
#   --debug  start QEMU's GDB stub listening on tcp::1234 (override the port
#            with $GDBPORT).  By default the guest still boots normally and
#            you attach whenever you like (e.g. to break into a hang); set
#            GDBHALT=1 to also freeze the CPU at reset so you can set
#            breakpoints before boot and `continue` from gdb.
#   --usb-host=SPEC
#            pass a REAL host USB device through to the guest via
#            -device usb-host so substrate enumerates and drives it — use this
#            to test any real USB device (storage, HID, serial, audio, ...),
#            not just audio.  SPEC is either VID:PID in hex (e.g.
#            --usb-host=05ac:110b, as printed by `lsusb`) or BUS.ADDR in
#            decimal (e.g. --usb-host=1.5, to pick one of several identical
#            devices).  Repeatable to grab several devices.  QEMU must be able
#            to claim each device (it gets detached from its host driver), so
#            you may need to run as root or grant access to the matching
#            /dev/bus/usb/BUS/DEV node.  The devices share the UHCI root hub
#            with the emulated usb-kbd/usb-mouse, which has few ports, so grab
#            only a couple at a time.
#   --usb-audio
#            replace the default emulated AC'97 with QEMU's USB Audio Class
#            device (UAC 1.0) on the UHCI bus, so substrate's uac driver binds
#            it and it becomes /dev/audio0.  Use this to exercise USB audio
#            without real hardware.
#   --usb-audio-host[=VID:PID]
#            shorthand for --usb-host=<dev> that ALSO drops the emulated AC'97,
#            so a real passed-through USB audio device is the guest's only
#            audio device (/dev/audio0).  Default VID:PID 05ac:110b (the Apple
#            EarPods); the audio plays on the physical device, no host backend.
#            --usb-audio and --usb-audio-host are mutually exclusive and both
#            drop the default AC'97.  Override the emulated host backend driver
#            (for the AC'97 / --usb-audio modes) with $AUDIODRV (default sdl;
#            e.g. AUDIODRV=pa or AUDIODRV=alsa).
#   --drive FILE
#            attach an additional raw disk image on the next free port of
#            the SHARED boot AHCI controller.  Repeatable; the boot disk is
#            sata0.0, so extra drives land on ports 1..5 and appear in the
#            guest as /dev/storage/sata1, sata2, ... — mount them yourself
#            during the session, e.g. `mount /dev/storage/sata1 /mnt ext2`.
#            ICH9 AHCI has 6 ports, so at most 5 extra drives this way.
#   --drive-ctrl FILE
#            like --drive but put the image on its OWN ich9-ahci
#            controller (one HBA per drive) instead of sharing the boot
#            controller's ports.  Repeatable with no 5-disk cap (limited
#            only by PCI slots).  Use this to exercise substrate's
#            multiple-AHCI-controller support; the disks still appear as
#            /dev/storage/sataN in PCI-probe order, so verify with
#            `ls /dev/storage/` in the guest (if only the first HBA binds,
#            they won't show up — fall back to --drive).
#            (Images are attached raw; for qcow2 etc. edit the format= below.)
GFX=0
GFX_MODE="1024x768@32"   # used for bare --gfx; --gfx=WxH@bpp overrides
KVM=0
USERNET=0
DEBUG=0
SNAPSHOT=0
USB_AUDIO=0
USB_AUDIO_HOST=0
USB_HOST_DEVICES=""        # newline-separated VID:PID / BUS.ADDR passthrough specs
EXTRA_DRIVES=""
EXTRA_CTRL_DRIVES=""
while [ $# -gt 0 ]; do
    case "$1" in
        --gfx)      GFX=1 ;;
        --gfx=*)    GFX=1; GFX_MODE="${1#--gfx=}"
                    case "$GFX_MODE" in
                        [0-9]*x[0-9]*) : ;;
                        *) echo "run-networking.sh: --gfx needs WxH or WxH@bpp (e.g. --gfx=640x480@4)" >&2; exit 1 ;;
                    esac ;;
        --kvm)      KVM=1 ;;
        --user)     USERNET=1 ;;
        --debug)    DEBUG=1 ;;
        --snapshot) SNAPSHOT=1 ;;
        --usb-audio)        USB_AUDIO=1 ;;
        --usb-host)
            shift
            [ $# -gt 0 ] || { echo "run-networking.sh: --usb-host needs a VID:PID or BUS.ADDR argument" >&2; exit 1; }
            USB_HOST_DEVICES="$USB_HOST_DEVICES
$1" ;;
        --usb-host=*)
            USB_HOST_DEVICES="$USB_HOST_DEVICES
${1#--usb-host=}" ;;
        --usb-audio-host)
            USB_AUDIO_HOST=1
            USB_HOST_DEVICES="$USB_HOST_DEVICES
05ac:110b" ;;
        --usb-audio-host=*)
            USB_AUDIO_HOST=1
            USB_HOST_DEVICES="$USB_HOST_DEVICES
${1#--usb-audio-host=}" ;;
        --drive)
            shift
            [ $# -gt 0 ] || { echo "run-networking.sh: --drive needs a file argument" >&2; exit 1; }
            EXTRA_DRIVES="$EXTRA_DRIVES
$1" ;;
        --drive=*)
            EXTRA_DRIVES="$EXTRA_DRIVES
${1#--drive=}" ;;
        --drive-ctrl)
            shift
            [ $# -gt 0 ] || { echo "run-networking.sh: --drive-ctrl needs a file argument" >&2; exit 1; }
            EXTRA_CTRL_DRIVES="$EXTRA_CTRL_DRIVES
$1" ;;
        --drive-ctrl=*)
            EXTRA_CTRL_DRIVES="$EXTRA_CTRL_DRIVES
${1#--drive-ctrl=}" ;;
        *)
            echo "run-networking.sh: unknown argument '$1'" >&2
            exit 1 ;;
    esac
    shift
done

# Build the qemu args for any --drive images.  Each extra image gets the
# next AHCI port (1..5) on the existing ich9-ahci controller, so the guest
# enumerates them as /dev/storage/sata1, sata2, ...  Newline-separated so
# paths with spaces survive; an empty list yields no args.
EXTRA_DRIVE_ARGS=""
port=1
OLDIFS=$IFS
IFS='
'
for f in $EXTRA_DRIVES; do
    [ -n "$f" ] || continue
    if [ ! -f "$f" ]; then
        echo "run-networking.sh: --drive file not found: $f" >&2
        exit 1
    fi
    if [ "$port" -gt 5 ]; then
        echo "run-networking.sh: at most 5 extra --drive images (AHCI ports 1-5);" \
             "add a second '-device ich9-ahci' for more" >&2
        exit 1
    fi
    EXTRA_DRIVE_ARGS="$EXTRA_DRIVE_ARGS -drive file=$f,format=raw,if=none,id=drive$port -device ide-hd,bus=sata0.$port,unit=0,drive=drive$port"
    echo "run-networking.sh: extra drive $f -> sata0 port $port (guest /dev/storage/sataN)"
    port=$((port + 1))
done
IFS=$OLDIFS

# --drive-ctrl images: one fresh ich9-ahci HBA per image (id=ahci1, ahci2,
# ...), each carrying a single disk on its port 0.
#
# substrate probes PCI slots in DESCENDING order, so the highest-slot HBA
# enumerates first as sata0.  To keep the boot disk on sata0 we pin the
# boot controller to the top slot (0x1f, set on the qemu line below) and
# walk the extra controllers down from 0x1e, so the guest sees boot=sata0,
# then the extra disks as sata1, sata2, ... in --drive-ctrl order.
#
# The in-kernel AHCI driver caps total HBAs at AHCI_MAX_CONTROLLERS (4), so
# at most 3 --drive-ctrl images; raise that constant + rebuild for more.
BOOT_AHCI_ADDR=""
EXTRA_CTRL_ARGS=""
cidx=1
OLDIFS=$IFS
IFS='
'
for f in $EXTRA_CTRL_DRIVES; do
    [ -n "$f" ] || continue
    if [ ! -f "$f" ]; then
        echo "run-networking.sh: --drive-ctrl file not found: $f" >&2
        exit 1
    fi
    if [ "$cidx" -gt 3 ]; then
        echo "run-networking.sh: at most 3 --drive-ctrl images" \
             "(AHCI_MAX_CONTROLLERS=4 incl. the boot HBA)" >&2
        exit 1
    fi
    BOOT_AHCI_ADDR=",addr=0x1f"
    ctrladdr=$(printf '0x%x' $((31 - cidx)))   # 0x1e, 0x1d, 0x1c
    EXTRA_CTRL_ARGS="$EXTRA_CTRL_ARGS -drive file=$f,format=raw,if=none,id=cdrive$cidx -device ich9-ahci,id=ahci$cidx,addr=$ctrladdr -device ide-hd,bus=ahci$cidx.0,unit=0,drive=cdrive$cidx"
    echo "run-networking.sh: extra drive $f -> own controller ahci$cidx -> guest /dev/storage/sata$cidx"
    cidx=$((cidx + 1))
done
IFS=$OLDIFS

APPEND="root=/dev/storage/sata0 trap serial_debug"
GFX_ARGS=""
ACCEL_ARG=""
if [ "$GFX" -eq 1 ]; then
    APPEND="$APPEND vga=$GFX_MODE"
    GFX_ARGS="-vga std"
    echo "run-networking.sh: graphical framebuffer, vga=$GFX_MODE"
fi
if [ "$KVM" -eq 1 ]; then
    ACCEL_ARG="-accel kvm"
fi
SNAPSHOT_ARG=""
if [ "$SNAPSHOT" -eq 1 ]; then
    SNAPSHOT_ARG="-snapshot"
    echo "run-networking.sh: -snapshot enabled; all disk writes are temporary (on-disk images stay pristine)"
fi

# --usb-host / --usb-audio-host: pass real host USB devices through to the guest
# via -device usb-host, one per queued spec.  Each spec is either VID:PID in hex
# (e.g. 05ac:110b, matched by vendor/product id) or BUS.ADDR in decimal (e.g.
# 1.5, matched by physical bus/address — use this to pick one of several
# identical devices).  The devices attach to the same UHCI root hub as the
# emulated usb-kbd/usb-mouse, so substrate enumerates them the same way; that
# hub is small, so grab only a couple at a time.
USB_HOST_ARGS=""
uidx=1
OLDIFS=$IFS
IFS='
'
for spec in $USB_HOST_DEVICES; do
    [ -n "$spec" ] || continue
    case "$spec" in
        *:*)
            vid="${spec%%:*}"; pid="${spec##*:}"
            if [ -z "$vid" ] || [ -z "$pid" ]; then
                echo "run-networking.sh: --usb-host VID:PID is incomplete: '$spec'" >&2; exit 1
            fi
            case "$vid$pid" in
                *[!0-9A-Fa-f]*) echo "run-networking.sh: --usb-host VID:PID must be hex (e.g. 05ac:110b): '$spec'" >&2; exit 1 ;;
            esac
            match="vendorid=0x$vid,productid=0x$pid" ;;
        *.*)
            bus="${spec%%.*}"; addr="${spec##*.}"
            if [ -z "$bus" ] || [ -z "$addr" ]; then
                echo "run-networking.sh: --usb-host BUS.ADDR is incomplete: '$spec'" >&2; exit 1
            fi
            case "$bus$addr" in
                *[!0-9]*) echo "run-networking.sh: --usb-host BUS.ADDR must be decimal (e.g. 1.5): '$spec'" >&2; exit 1 ;;
            esac
            match="hostbus=$bus,hostaddr=$addr" ;;
        *)
            echo "run-networking.sh: --usb-host needs VID:PID (hex, e.g. 05ac:110b) or BUS.ADDR (decimal, e.g. 1.5): '$spec'" >&2
            exit 1 ;;
    esac
    USB_HOST_ARGS="$USB_HOST_ARGS -device usb-host,$match,id=usbhost$uidx"
    echo "run-networking.sh: passing host USB device $spec through to the guest" \
         "(claimed from the host driver; run as root or grant /dev/bus/usb access if it fails)"
    uidx=$((uidx + 1))
done
IFS=$OLDIFS

# Audio device selection.  Default: emulated AC'97 with a host backend.
#   --usb-audio       QEMU's emulated USB Audio Class device (UAC 1.0); the
#                     uac driver binds it as /dev/audio0.
#   --usb-audio-host  a --usb-host passthrough (queued above) that additionally
#                     drops the AC'97 so a real USB audio device (default the
#                     EarPods) is the guest's only audio_dev (/dev/audio0); it
#                     plays on the physical hardware.
# Both USB audio modes drop the AC'97 so the USB device is the only audio_dev
# and lands on /dev/audio0.  (Generic --usb-host passthrough is handled above
# and does NOT touch audio.)  USB devices attach to the piix3-usb-uhci below.
#
# Host backend ($AUDIODRV): default to QEMU's 'pa' when a PulseAudio/PipeWire
# server is reachable (it routes to the host mixer on both), else fall back to
# 'sdl'.  Note 'sdl' frequently does NOT feed a PipeWire host's mixer (no
# sink-input appears), which presents as "audio is silent" even though the
# guest is producing sound — so we avoid it when pulse/pipewire is present.
if [ -z "${AUDIODRV:-}" ]; then
    if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
        AUDIODRV=pa
    else
        AUDIODRV=sdl
    fi
fi
echo "run-networking.sh: host audio backend: $AUDIODRV (override with \$AUDIODRV)"
if [ "$USB_AUDIO" -eq 1 ] && [ "$USB_AUDIO_HOST" -eq 1 ]; then
    echo "run-networking.sh: --usb-audio and --usb-audio-host are mutually exclusive" >&2
    exit 1
fi
if [ "$USB_AUDIO_HOST" -eq 1 ]; then
    # The real USB audio device is already queued into USB_HOST_ARGS above; here
    # we only drop the emulated AC'97 so the passthrough device is the guest's
    # sole audio_dev and lands on /dev/audio0.  It plays on the physical device
    # (no host audio backend used).
    AUDIO_ARGS=""
    echo "run-networking.sh: real USB audio device passed through -> guest /dev/audio0 (plays on the physical device)"
elif [ "$USB_AUDIO" -eq 1 ]; then
    AUDIO_ARGS="-audiodev $AUDIODRV,id=audio0 -device usb-audio,audiodev=audio0"
    echo "run-networking.sh: emulated USB Audio Class device (UAC 1.0) -> guest /dev/audio0"
else
    AUDIO_ARGS="-audio driver=$AUDIODRV,model=ac97,id=audio0"
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
  -m 512M \
  -machine pc,i8042=off \
  $SNAPSHOT_ARG \
  -drive file=rootfs.img,format=raw,if=none,id=drive0 \
  -device ich9-ahci,id=sata0$BOOT_AHCI_ADDR \
  -device ide-hd,bus=sata0.0,unit=0,drive=drive0 \
  $EXTRA_DRIVE_ARGS \
  $EXTRA_CTRL_ARGS \
  -device piix3-usb-uhci -device usb-kbd -device usb-mouse \
  $USB_HOST_ARGS \
  $NETDEV_ARGS \
  -kernel "$KERNEL" \
  -append "$APPEND" \
  $GFX_ARGS \
  $DEBUG_ARGS \
  -serial stdio \
  $AUDIO_ARGS
