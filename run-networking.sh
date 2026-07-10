#!/bin/sh
# Run substrate under qemu with networking.
#
# Networking back-ends:
#   default (macvtap): bridge the guest NIC onto a real host interface. The
#     guest gets its own MAC on the LAN and DHCP reaches the real router.
#     Host<->guest traffic is not possible with macvtap. Requires sudo and an
#     Ethernet-like default-route NIC; fails on wifi and point-to-point
#     tun/VPN devices ("RTNETLINK answers: Invalid argument").
#   --user (slirp): guest behind QEMU's built-in NAT (gateway 10.0.2.2, guest
#     10.0.2.15). No sudo, no host NIC; works over wifi/VPN. Inbound needs
#     hostfwd ($HOSTFWD, e.g. HOSTFWD=hostfwd=tcp::2222-:22).
#
# $NIC overrides the host NIC in macvtap mode (default: default-route iface).
set -eu

# Flags:
#   --gfx[=WxH@bpp]
#            bring up substrate's fb driver (adds vga=<mode> + -vga std). Bare
#            --gfx uses 1024x768@32 (BGA linear); e.g. --gfx=640x480@4 (planar
#            VGA), --gfx=800x600@16. Without --gfx, qemu's text mode is used.
#   --kvm    enable -accel kvm. Default is TCG: KVM has an i386 coherence bug
#            that corrupts a single-byte read right after a SIGALRM reaches a
#            userland handler (tests/lib/c/torture_heap_stdio sc9f; passes
#            under TCG). Use --kvm for speed when that risk is acceptable.
#   --smp[=N]
#            boot with N virtual CPUs. Bare --smp uses 2; --smp=4 sets the
#            count. Default is 1 (uniprocessor). Substrate SMP is new: it boots
#            multi-core cleanly, but races under sustained load may still remain.
#   --snapshot
#            add -snapshot: every writable disk (rootfs.img, --drive,
#            --drive-ctrl) is backed by a throwaway overlay, so guest writes
#            are discarded on exit and the images stay pristine. Use for test
#            boots.
#   --user   QEMU user-mode (slirp) networking instead of macvtap. No sudo or
#            host NIC.
#   --debug  enable the serial_debug boot arg (verbose kernel serial output,
#            off by default because it slows the serial console) and start
#            QEMU's GDB stub on tcp::1234 ($GDBPORT overrides). GDBHALT=1 also
#            freezes the CPU at reset so breakpoints can be set before boot.
#   --usb-host=SPEC
#            pass a real host USB device through via -device usb-host (storage,
#            HID, serial, audio, ...). SPEC is VID:PID in hex (e.g. 05ac:110b,
#            from lsusb) or BUS.ADDR in decimal (e.g. 1.5, to disambiguate
#            identical devices). Repeatable. QEMU claims the device from its
#            host driver, so this needs root or access to the matching
#            /dev/bus/usb/BUS/DEV node. Devices share the UHCI root hub with
#            usb-kbd/usb-mouse (few ports); grab only a couple.
#   --usb-audio
#            replace the emulated AC'97 with QEMU's USB Audio Class device
#            (UAC 1.0) on UHCI; substrate's uac driver binds it as /dev/audio0.
#   --usb-audio-host[=VID:PID]
#            --usb-host=<dev> that also drops the emulated AC'97, so the
#            passed-through USB audio device is the guest's only audio device
#            (/dev/audio0). Default VID:PID 05ac:110b (Apple EarPods); audio
#            plays on the physical device. Mutually exclusive with --usb-audio.
#            $AUDIODRV overrides the host backend for the AC'97 / --usb-audio
#            modes (default sdl; e.g. AUDIODRV=pa, AUDIODRV=alsa).
#   --drive FILE
#            attach a raw disk image on the next free port of the shared boot
#            AHCI controller. Repeatable; the boot disk is sata0.0, so extras
#            land on ports 1..5 as guest /dev/storage/sata1, sata2, ... (mount
#            them yourself, e.g. mount /dev/storage/sata1 /mnt ext2). ICH9 AHCI
#            has 6 ports, so at most 5 extra drives.
#   --drive-ctrl FILE
#            like --drive but on a dedicated ich9-ahci controller per image.
#            Repeatable up to AHCI_MAX_CONTROLLERS-1 (3). Exercises substrate's
#            multiple-AHCI-controller support; disks appear as /dev/storage/sataN
#            in PCI-probe order (verify with ls /dev/storage/; if only the first
#            HBA binds, fall back to --drive). Raw format only; edit format=
#            below for qcow2 etc.
#   --floppy FILE
#            attach FILE as a floppy diskette (qemu if=floppy). Repeatable for
#            the PC's two drives: first --floppy is fd0 (A:), second fd1 (B:).
#            Raw; a 1.44 MB diskette is 1474560 bytes.
#   --usb-version=VER
#            select the emulated USB host controller: 1.1 = UHCI (piix3-usb-uhci,
#            default), 2.0 = EHCI (usb-ehci), 3.0 = xHCI (qemu-xhci). All USB
#            devices attach to it. substrate only drives UHCI today; 2.0/3.0
#            need the in-progress EHCI/xHCI drivers or the guest sees no USB.
GFX=0
GFX_MODE="1024x768@32"   # used for bare --gfx; --gfx=WxH@bpp overrides
KVM=0
SMP=1
USERNET=0
DEBUG=0
SNAPSHOT=0
USB_AUDIO=0
USB_AUDIO_HOST=0
USB_HOST_DEVICES=""        # newline-separated VID:PID / BUS.ADDR passthrough specs
EXTRA_DRIVES=""
EXTRA_CTRL_DRIVES=""
FLOPPY_IMAGES=""           # newline-separated floppy diskette images (fd0, fd1)
USB_VERSION="1.1"          # 1.1=UHCI (default), 2.0=EHCI, 3.0=xHCI
while [ $# -gt 0 ]; do
    case "$1" in
        --gfx)      GFX=1 ;;
        --gfx=*)    GFX=1; GFX_MODE="${1#--gfx=}"
                    case "$GFX_MODE" in
                        [0-9]*x[0-9]*) : ;;
                        *) echo "run-networking.sh: --gfx needs WxH or WxH@bpp (e.g. --gfx=640x480@4)" >&2; exit 1 ;;
                    esac ;;
        --kvm)      KVM=1 ;;
        --smp)      SMP=2 ;;
        --smp=*)    SMP="${1#--smp=}"
                    case "$SMP" in
                        ''|*[!0-9]*) echo "run-networking.sh: --smp needs a positive integer (e.g. --smp=4)" >&2; exit 1 ;;
                    esac
                    [ "$SMP" -ge 1 ] || { echo "run-networking.sh: --smp count must be >= 1" >&2; exit 1; } ;;
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
        --floppy)
            shift
            [ $# -gt 0 ] || { echo "run-networking.sh: --floppy needs a file argument" >&2; exit 1; }
            FLOPPY_IMAGES="$FLOPPY_IMAGES
$1" ;;
        --floppy=*)
            FLOPPY_IMAGES="$FLOPPY_IMAGES
${1#--floppy=}" ;;
        --usb-version=*)
            USB_VERSION="${1#--usb-version=}"
            case "$USB_VERSION" in
                1.1|2.0|3.0) : ;;
                *) echo "run-networking.sh: --usb-version must be 1.1, 2.0, or 3.0 (got '$USB_VERSION')" >&2; exit 1 ;;
            esac ;;
        *)
            echo "run-networking.sh: unknown argument '$1'" >&2
            exit 1 ;;
    esac
    shift
done

# USB host controller (--usb-version). All USB devices attach to its bus
# (id=usbctl). substrate only drives UHCI today.
case "$USB_VERSION" in
    1.1) USB_CTRL="-device piix3-usb-uhci,id=usbctl" ;;
    2.0) USB_CTRL="-device usb-ehci,id=usbctl" ;;
    3.0) USB_CTRL="-device qemu-xhci,id=usbctl" ;;
esac
USB_BUS=",bus=usbctl.0"
echo "run-networking.sh: USB $USB_VERSION host controller (${USB_CTRL#-device })"

# --drive images: each gets the next port (1..5) on the boot ich9-ahci
# controller, enumerated as guest /dev/storage/sata1, sata2, ... The list is
# newline-separated so paths with spaces survive.
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

# --drive-ctrl images: one ich9-ahci HBA per image (id=ahci1, ahci2, ...),
# each with a single disk on its port 0.
#
# substrate probes PCI slots in descending order, so the highest-slot HBA
# enumerates first as sata0. To keep the boot disk on sata0, pin the boot
# controller to the top slot (0x1f, on the qemu line below) and walk the extra
# controllers down from 0x1e; the guest then sees boot=sata0, then sata1,
# sata2, ... in --drive-ctrl order.
#
# The AHCI driver caps total HBAs at AHCI_MAX_CONTROLLERS (4), so at most 3
# --drive-ctrl images; raise that constant and rebuild for more.
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

# --floppy images: attach each as a diskette (if=floppy). A PC has two drives,
# so at most two images: index 0 is fd0 (A:), index 1 is fd1 (B:).
FLOPPY_ARGS=""
fidx=0
OLDIFS=$IFS
IFS='
'
for f in $FLOPPY_IMAGES; do
    [ -n "$f" ] || continue
    if [ ! -f "$f" ]; then
        echo "run-networking.sh: --floppy file not found: $f" >&2
        exit 1
    fi
    if [ "$fidx" -gt 1 ]; then
        echo "run-networking.sh: at most 2 --floppy images (drives fd0 and fd1)" >&2
        exit 1
    fi
    FLOPPY_ARGS="$FLOPPY_ARGS -drive file=$f,format=raw,if=floppy,index=$fidx"
    echo "run-networking.sh: floppy $f -> guest fd$fidx"
    fidx=$((fidx + 1))
done
IFS=$OLDIFS

APPEND="root=/dev/storage/sata0 trap"
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
if [ "$SMP" -gt 1 ]; then
    echo "run-networking.sh: SMP with $SMP virtual CPUs"
fi
SNAPSHOT_ARG=""
if [ "$SNAPSHOT" -eq 1 ]; then
    SNAPSHOT_ARG="-snapshot"
    echo "run-networking.sh: -snapshot enabled; all disk writes are temporary (on-disk images stay pristine)"
fi

# --usb-host / --usb-audio-host: pass real host USB devices through via
# -device usb-host, one per queued spec. A spec is VID:PID in hex (05ac:110b,
# by vendor/product id) or BUS.ADDR in decimal (1.5, by physical bus/address,
# to pick one of several identical devices). Devices attach to the same UHCI
# root hub as usb-kbd/usb-mouse.
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
    USB_HOST_ARGS="$USB_HOST_ARGS -device usb-host,$match,id=usbhost$uidx$USB_BUS"
    echo "run-networking.sh: passing host USB device $spec through to the guest" \
         "(claimed from the host driver; run as root or grant /dev/bus/usb access if it fails)"
    uidx=$((uidx + 1))
done
IFS=$OLDIFS

# Audio device selection. Default: emulated AC'97 with a host backend.
#   --usb-audio       emulated USB Audio Class device (UAC 1.0); the uac driver
#                     binds it as /dev/audio0.
#   --usb-audio-host  a --usb-host passthrough (queued above) that also drops
#                     the AC'97, so a real USB audio device (default EarPods)
#                     is the guest's only audio_dev (/dev/audio0), playing on
#                     the physical hardware.
# Both USB audio modes drop the AC'97; generic --usb-host passthrough does not.
#
# Host backend ($AUDIODRV): 'pa' when a PulseAudio/PipeWire server is reachable
# (routes to the host mixer on both), else 'sdl'. 'sdl' often does not feed a
# PipeWire mixer (no sink-input, guest audio silent), so it is avoided when
# pulse/pipewire is present.
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
    # The passthrough device is already in USB_HOST_ARGS; drop the AC'97 so it
    # is the guest's sole audio_dev (/dev/audio0), playing on the physical
    # device with no host backend.
    AUDIO_ARGS=""
    echo "run-networking.sh: real USB audio device passed through -> guest /dev/audio0 (plays on the physical device)"
elif [ "$USB_AUDIO" -eq 1 ]; then
    AUDIO_ARGS="-audiodev $AUDIODRV,id=audio0 -device usb-audio,audiodev=audio0$USB_BUS"
    echo "run-networking.sh: emulated USB Audio Class device (UAC 1.0) -> guest /dev/audio0"
else
    AUDIO_ARGS="-audio driver=$AUDIODRV,model=ac97,id=audio0"
fi

# NETDEV_ARGS: the -netdev/-device pair for the chosen back-end. In macvtap
# mode the guest's tap is passed as fd 3 (opened with `exec` below) so one
# qemu invocation serves both back-ends.
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

# --debug: expose QEMU's GDB stub. -gdb tcp::PORT listens; -S (GDBHALT=1) also
# freezes the CPU at reset until the debugger continues. Point gdb at the
# kernel ELF for symbols (sys/kernel.bin carries them).
DEBUG_ARGS=""
if [ "$DEBUG" -eq 1 ]; then
    # serial_debug is off by default (it slows the serial console); enable it.
    APPEND="$APPEND serial_debug"
    echo "run-networking.sh: kernel serial_debug output enabled"
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

# Find the rootfs image. Decompress rootfs.img.zst in place if only the
# compressed form exists; a present rootfs.img takes precedence.
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

# qemu runs in the foreground (not exec'd) so the EXIT trap can tear the
# macvtap down when it exits.
#
# i8042=off disables the emulated PS/2 controller (keyboard and mouse).
# substrate's PS/2 mouse path is unreliable, and usb-kbd/usb-mouse below cover
# input, so this leaves a single clean USB pointer on /dev/input/event0.
qemu-system-i386 -cpu qemu32,+sse,+sse2 $ACCEL_ARG \
  -smp "$SMP" \
  -m 512M \
  -machine pc,i8042=off \
  $SNAPSHOT_ARG \
  -drive file=rootfs.img,format=raw,if=none,id=drive0 \
  -device ich9-ahci,id=sata0$BOOT_AHCI_ADDR \
  -device ide-hd,bus=sata0.0,unit=0,drive=drive0 \
  $EXTRA_DRIVE_ARGS \
  $EXTRA_CTRL_ARGS \
  $FLOPPY_ARGS \
  $USB_CTRL -device usb-kbd$USB_BUS -device usb-mouse$USB_BUS \
  $USB_HOST_ARGS \
  $NETDEV_ARGS \
  -kernel "$KERNEL" \
  -append "$APPEND" \
  $GFX_ARGS \
  $DEBUG_ARGS \
  -serial stdio \
  $AUDIO_ARGS
