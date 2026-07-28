#!/bin/sh
# Run substrate under qemu with networking.
#
# Options and environment variables: ./run-networking.sh --help
#
# $MEM sets the guest RAM size (default 8G; any qemu -m syntax, e.g. MEM=512M).
# Note what the guest can actually do with it: qemu puts only about 3 GiB below
# the 4 GiB PCI hole and the remainder above it, which a non-PAE 32-bit kernel
# cannot address, so substrate reports ~3071 MiB of an 8G guest.  Of that it
# uses the first 992 MiB -- PMM_DIRECTMAP_PHYS_LIMIT, where the higher-half
# direct map stops below the signal trampoline.  The rest is inert.  Sizing the
# guest well past that ceiling is still worth doing: it is what shook out the
# trampoline corruption (a47480ceb), which was invisible under 992 MiB.
set -eu

MEM=${MEM:-8G}

# The flag reference lives in usage() rather than in a comment here: it is the
# same text either way, and a copy that only exists in the source is a copy
# nobody reads and everybody forgets to update.  `./run-networking.sh --help`.
usage() {
    cat <<'EOF'
Usage: ./run-networking.sh [options]

Boots substrate under qemu.  With no options: direct kernel boot of
sys/kernel.bin, root found by volume label, macvtap networking, 8G of RAM.

Boot mode
  --boot=MODE        How the machine starts.  The three modes exercise
                     genuinely different code paths, so a change that works in
                     one can still be broken in another -- test the one you
                     care about.
                       kernel  (default) qemu loads sys/kernel.bin via -kernel.
                               No bootloader involved.  Fastest edit-run loop,
                               and the only mode where --gfx, --debug's
                               serial_debug and $ROOT mean anything.
                       bios    boot rootfs.img as real hardware would: the BIOS
                               runs GRUB from the MBR + post-MBR gap, GRUB finds
                               the root by LABEL and loads /vmunix from it.
                       uefi    same image, firmware side: OVMF runs
                               /EFI/BOOT/BOOTX64.EFI off the FAT32 ESP, which
                               loads /vmunix via multiboot2.  Needs edk2/OVMF,
                               and runs qemu-system-x86_64 because the x64
                               firmware needs a 64-bit CPU (the guest kernel is
                               still 32-bit).
                     In bios/uefi the kernel and its boot arguments come from
                     the grub.cfg baked into the image, NOT from this script.
                     Rebuild with ./build-rootfs.sh --image to pick up a new
                     kernel; edit the ESP's /boot/grub/grub.cfg for boot args.

Machine
  --kvm              Use -accel kvm.  Default is TCG: KVM has an i386 coherence
                     bug that corrupts a single-byte read right after SIGALRM
                     reaches a userland handler (tests/lib/c/torture_heap_stdio
                     sc9f; passes under TCG).  Use --kvm for speed when that
                     risk is acceptable.
  --smp[=N]          Boot with N CPUs.  Bare --smp means 2; default is 1.
                     Substrate SMP boots multi-core cleanly, but races under
                     sustained load may remain.
  --gfx[=WxH@bpp]    Bring up substrate's framebuffer driver (adds vga=<mode>
                     and -vga std).  Bare --gfx is 1024x768@32 (BGA linear);
                     e.g. --gfx=640x480@4 (planar VGA), --gfx=800x600@16.
                     Ignored in --boot=bios/uefi, where GRUB picks the mode.
  --snapshot         Back every writable disk with a throwaway overlay, so guest
                     writes are discarded on exit and the images stay pristine.
                     Use this for test boots.

Networking
  --user             QEMU user-mode (slirp) NAT instead of macvtap: gateway
                     10.0.2.2, guest 10.0.2.15.  No sudo and no host NIC, so it
                     works over wifi and VPNs.  Inbound needs $HOSTFWD.
                     The default is macvtap, which bridges the guest onto a real
                     host interface -- the guest gets its own MAC on the LAN and
                     DHCP reaches the real router, but host<->guest traffic is
                     impossible.  It needs sudo and an Ethernet-like
                     default-route NIC; it fails on wifi and point-to-point
                     tun/VPN devices ("RTNETLINK answers: Invalid argument").

Storage
  --drive FILE       Attach a raw image on the next free port of the boot AHCI
                     controller.  Repeatable.  The boot disk holds sata0.0, so
                     extras land on ports 1..5 as guest /dev/storage/sata1,
                     sata2, ... (mount them yourself: mount /dev/storage/sata1
                     /mnt ext2).  ICH9 AHCI has 6 ports, so 5 extra drives max.
  --drive-ctrl FILE  Like --drive but on a dedicated ich9-ahci controller per
                     image, exercising multiple-HBA support.  Up to 3
                     (AHCI_MAX_CONTROLLERS is 4 including the boot HBA).  Disks
                     appear as /dev/storage/sataN in PCI-probe order; if only
                     the first HBA binds, fall back to --drive.
  --floppy FILE      Attach FILE as a diskette.  Repeatable for the PC's two
                     drives: first is fd0 (A:), second fd1 (B:).  Raw; a 1.44 MB
                     diskette is 1474560 bytes.
  --virtio           Put the root filesystem on virtio-blk instead of AHCI.  The
                     kernel still finds it by label.  The boot ich9-ahci is
                     created empty so --drive keeps working, and since port 0 is
                     free, --drive images start there.

USB and audio
  --usb-version=VER  Emulated USB host controller: 1.1 = UHCI (default),
                     2.0 = EHCI, 3.0 = xHCI.  All USB devices attach to it.
                     Substrate only drives UHCI today; 2.0/3.0 need the
                     in-progress drivers or the guest sees no USB at all.
  --usb-host SPEC    Pass a real host USB device through (storage, HID, serial,
                     audio, ...).  SPEC is VID:PID in hex (05ac:110b, from
                     lsusb) or BUS.ADDR in decimal (1.5, to pick one of several
                     identical devices).  Repeatable.  QEMU claims the device
                     from its host driver, so this needs root or access to the
                     matching /dev/bus/usb/BUS/DEV node.  These share the root
                     hub with usb-kbd/usb-mouse, so grab only a couple.
  --usb-audio        Replace the emulated AC'97 with QEMU's USB Audio Class
                     device (UAC 1.0); substrate's uac driver binds it as
                     /dev/audio0.
  --usb-audio-host[=VID:PID]
                     A --usb-host passthrough that also drops the AC'97, so a
                     real USB audio device is the guest's only audio device and
                     sound plays on the physical hardware.  Defaults to
                     05ac:110b (Apple EarPods).  Excludes --usb-audio.

Debugging
  --debug            Enable the serial_debug boot argument (verbose kernel
                     serial output, off by default because it slows the console)
                     and start QEMU's GDB stub on tcp::1234.  In image modes the
                     command line comes from grub.cfg, so this only adds the
                     stub -- pick the "serial console + verbose" GRUB entry for
                     the equivalent.
  --help, -h         This message.

Environment
  MEM        Guest RAM (default 8G; any qemu -m syntax).  See the note at the
             top of this script: substrate can only address the first 992 MiB.
  ROOT       Root filesystem for --boot=kernel (default LABEL=sub-root).  Set
             it to e.g. /dev/storage/sata0 for images predating the label
             layout.
  NIC        Host interface to bridge in macvtap mode (default: the
             default-route interface).
  MACVTAP    Name of the macvtap link to create (default macvtap0).
  HOSTFWD    Port forwards for --user, e.g. HOSTFWD=hostfwd=tcp::2222-:22
  AUDIODRV   Host audio backend (default: pa when PulseAudio/PipeWire is
             reachable, else sdl).
  GDBPORT    Port for the --debug GDB stub (default 1234).
  GDBHALT    GDBHALT=1 freezes the CPU at reset so breakpoints can be set
             before the kernel runs.

Examples
  ./run-networking.sh --user --kvm --snapshot     quick throwaway test boot
  ./run-networking.sh --boot=uefi --snapshot      check the UEFI path
  MEM=512M ./run-networking.sh --boot=bios        BIOS boot, smaller guest
  ./run-networking.sh --debug --user              boot and wait for gdb :1234
EOF
}

BOOTMODE=kernel            # kernel = -kernel sys/kernel.bin, bios/uefi = boot rootfs.img
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
VIRTIO=0                   # 1 = root on virtio-blk instead of AHCI
while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)  usage; exit 0 ;;
        --boot=*)   BOOTMODE="${1#--boot=}"
                    case "$BOOTMODE" in
                        kernel|bios|uefi) : ;;
                        *) echo "run-networking.sh: --boot must be kernel, bios, or uefi (got '$BOOTMODE')" >&2; exit 1 ;;
                    esac ;;
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
        --virtio)   VIRTIO=1 ;;
        --usb-version=*)
            USB_VERSION="${1#--usb-version=}"
            case "$USB_VERSION" in
                1.1|2.0|3.0) : ;;
                *) echo "run-networking.sh: --usb-version must be 1.1, 2.0, or 3.0 (got '$USB_VERSION')" >&2; exit 1 ;;
            esac ;;
        *)
            echo "run-networking.sh: unknown argument '$1'" >&2
            echo "Try './run-networking.sh --help'." >&2
            exit 1 ;;
    esac
    shift
done

# One teardown for everything this script creates behind qemu's back. Both the
# macvtap link and the UEFI variable store are set up conditionally further
# down, so the handler tests for each rather than being installed twice -- a
# second `trap ... EXIT` would silently replace the first and leak whichever
# resource was registered earlier.
MACVTAP_DEV=""
OVMF_VARS=""
cleanup() {
    if [ -n "$MACVTAP_DEV" ]; then
        sudo ip link delete "$MACVTAP_DEV" 2>/dev/null || true
    fi
    if [ -n "$OVMF_VARS" ]; then
        rm -f "$OVMF_VARS" || true
    fi
    return 0
}
trap cleanup EXIT INT TERM

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
# With --virtio the root disk is not on AHCI, so port 0 is free and the first
# extra drive can use it -- keeping AHCI port N == guest sataN.
if [ "$VIRTIO" -eq 1 ]; then port=0; else port=1; fi
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
        # ICH9 AHCI has 6 ports; the boot disk holds port 0 unless --virtio
        # moved the root filesystem off AHCI entirely.
        if [ "$VIRTIO" -eq 1 ]; then
            echo "run-networking.sh: at most 6 --drive images with --virtio (AHCI ports 0-5);" \
                 "use --drive-ctrl for more" >&2
        else
            echo "run-networking.sh: at most 5 extra --drive images (AHCI ports 1-5);" \
                 "add a second '-device ich9-ahci' for more" >&2
        fi
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
# controller to the top of a slot window and walk the extra controllers down
# from there; the guest then sees boot=sata0, then sata1, sata2, ... in
# --drive-ctrl order.
#
# The window starts at 0x1e, not 0x1f. Slot 0x1f is free on the i440FX "pc"
# machine but is the ICH9-LPC southbridge on q35 -- which --boot=uefi uses --
# so pinning there killed the run outright:
#
#   qemu-system-x86_64: -device ich9-ahci,id=sata0,addr=0x1f: PCI: slot 31
#   function 0 not available for ich9-ahci, in use by ICH9-LPC
#
# 0x1e down to 0x18 are free on both machines, and only four slots are ever
# needed (boot + 3), so one window serves every boot mode. Only the ordering
# among our own HBAs matters, not being the highest slot on the bus.
#
# The AHCI driver caps total HBAs at AHCI_MAX_CONTROLLERS (4), so at most 3
# --drive-ctrl images; raise that constant and rebuild for more.
AHCI_TOP_SLOT=30                # 0x1e
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
    BOOT_AHCI_ADDR=",addr=$(printf '0x%x' "$AHCI_TOP_SLOT")"
    ctrladdr=$(printf '0x%x' $((AHCI_TOP_SLOT - cidx)))   # 0x1d, 0x1c, 0x1b
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

# How rootfs.img is attached. Default: port 0 of the boot ich9-ahci, so the
# guest sees /dev/storage/sata0. --virtio instead hands it to a virtio-blk-pci
# device, which the guest's virtio-blk driver registers as
# /dev/storage/virtio0. The AHCI controller is created either way so --drive
# keeps working.
if [ "$VIRTIO" -eq 1 ]; then
    ROOT_DEV_ARGS="-device virtio-blk-pci,drive=drive0,id=vblk0"
    echo "run-networking.sh: rootfs.img on virtio-blk"
else
    ROOT_DEV_ARGS="-device ide-hd,bus=sata0.0,unit=0,drive=drive0"
fi

# Root filesystem, by label.  rootfs.img is a partitioned disk -- an MBR with a
# FAT32 ESP and the ext2 root -- so the old root=/dev/storage/sata0 now names
# the whole disk (partition table and all) and the mount simply fails.  The
# right handle is the volume label: it is the same string whether the image is
# on AHCI, virtio or IDE, whether the root landed on p2 or somewhere else, and
# it is what GRUB and /etc/fstab already use, so all three boot modes agree.
# $ROOT overrides it for images that predate the labelled layout, e.g.
#   ROOT=/dev/storage/sata0 ./run-networking.sh
ROOT_DEV=${ROOT:-LABEL=sub-root}
APPEND="root=$ROOT_DEV trap"
if [ "$BOOTMODE" = kernel ]; then
    echo "run-networking.sh: root=$ROOT_DEV"
fi
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

    # Tear down any leftover macvtap from a previous run before recreating.
    sudo ip link delete "$MACVTAP" 2>/dev/null || true

    if ! sudo ip link add link "$NIC" name "$MACVTAP" type macvtap mode bridge; then
        echo "run-networking.sh: macvtap on '$NIC' failed (wifi/tun/VPN NICs" \
             "are not supported); use --user for QEMU internal networking" >&2
        exit 1
    fi
    MACVTAP_DEV="$MACVTAP"      # now owned by cleanup()
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

# Prefer a kernel in the current directory; fall back to sys/. Only the
# direct-kernel mode loads one from the host -- bios/uefi boot /vmunix out of
# the image, so a missing sys/kernel.bin is not an error there.
KERNEL=""
if [ "$BOOTMODE" = kernel ]; then
    if [ -f kernel.bin ]; then
        KERNEL=kernel.bin
    elif [ -f sys/kernel.bin ]; then
        KERNEL=sys/kernel.bin
    else
        echo "run-networking.sh: kernel.bin not found in . or sys/" >&2
        exit 1
    fi
fi

# --debug: expose QEMU's GDB stub. -gdb tcp::PORT listens; -S (GDBHALT=1) also
# freezes the CPU at reset until the debugger continues. Point gdb at the
# kernel ELF for symbols (sys/kernel.bin carries them).
DEBUG_ARGS=""
if [ "$DEBUG" -eq 1 ]; then
    # serial_debug is off by default (it slows the serial console); enable it.
    # In image modes the kernel command line comes from grub.cfg, so this can
    # only add the GDB stub -- pick the "serial console + verbose" GRUB entry
    # for the equivalent of serial_debug.
    if [ "$BOOTMODE" = kernel ]; then
        APPEND="$APPEND serial_debug"
        echo "run-networking.sh: kernel serial_debug output enabled"
    else
        echo "run-networking.sh: --boot=$BOOTMODE: serial_debug comes from grub.cfg," \
             "not this script; select the 'serial console + verbose' entry in the menu"
    fi
    # Symbols still come from the host build; the image's /vmunix is the
    # framebuffer variant of the same tree.
    SYMFILE=${KERNEL:-sys/kernel.fb.bin}
    GDBPORT=${GDBPORT:-1234}
    DEBUG_ARGS="-gdb tcp::$GDBPORT"
    HALTNOTE=""
    if [ "${GDBHALT:-0}" = 1 ]; then
        DEBUG_ARGS="$DEBUG_ARGS -S"
        HALTNOTE=" (CPU halted at reset; run 'continue' in gdb to boot)"
    fi
    echo "run-networking.sh: GDB stub on tcp::$GDBPORT$HALTNOTE"
    echo "    connect: gdb -ex 'symbol-file $SYMFILE' -ex 'target remote :$GDBPORT'"
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

# ---------------------------------------------------------------------------
# Boot mode. Everything below picks the emulator binary, machine type, CPU and
# firmware; the device model (disks, USB, net, audio) is identical in all three
# so a bug reproduced under one mode can be chased under another.
# ---------------------------------------------------------------------------
QEMU_BIN=qemu-system-i386
QEMU_CPU="qemu32,+sse,+sse2"
QEMU_MACHINE="pc,i8042=off"

# Guard the image modes: GRUB lives in the MBR (BIOS) and on the FAT32 ESP
# (UEFI), so a bare-filesystem rootfs.img -- what build-rootfs.sh produced
# before the partitioned layout -- silently fails to boot with no diagnostic.
# Check for the 0x55AA boot signature rather than letting the user stare at a
# blinking cursor.
if [ "$BOOTMODE" != kernel ]; then
    sig=$(dd if=rootfs.img bs=1 skip=510 count=2 2>/dev/null | od -An -tx1 | tr -d ' \n')
    if [ "$sig" != "55aa" ]; then
        echo "run-networking.sh: rootfs.img has no MBR boot signature (found '${sig:-nothing}')." \
             "--boot=$BOOTMODE needs the partitioned GRUB image; rebuild with" \
             "./build-rootfs.sh --image" >&2
        exit 1
    fi
fi

case "$BOOTMODE" in
    kernel)
        echo "run-networking.sh: direct kernel boot ($KERNEL)"
        ;;
    bios)
        echo "run-networking.sh: BIOS boot from rootfs.img (GRUB in the MBR -> /vmunix)"
        echo "run-networking.sh: kernel + boot args come from the image's grub.cfg, not this script"
        ;;
    uefi)
        # The x64 OVMF build needs a 64-bit CPU to start, so this mode runs the
        # x86_64 emulator with a 64-bit CPU model. The guest kernel is still
        # 32-bit -- GRUB drops to protected mode for the multiboot2 handoff.
        QEMU_BIN=qemu-system-x86_64
        QEMU_CPU="qemu64,+rdrand"
        QEMU_MACHINE="q35,i8042=off"

        command -v "$QEMU_BIN" >/dev/null 2>&1 || {
            echo "run-networking.sh: --boot=uefi needs $QEMU_BIN" >&2; exit 1; }

        # Distros disagree on where OVMF lives; take the first match.
        OVMF_CODE=""
        OVMF_VARS_TEMPLATE=""
        for d in /usr/share/edk2/x64 /usr/share/OVMF /usr/share/ovmf/x64 \
                 /usr/share/qemu/edk2-x86_64 /usr/share/edk2-ovmf/x64; do
            for c in OVMF_CODE.4m.fd OVMF_CODE.fd edk2-x86_64-code.fd; do
                if [ -z "$OVMF_CODE" ] && [ -f "$d/$c" ]; then
                    OVMF_CODE="$d/$c"
                fi
            done
            for v in OVMF_VARS.4m.fd OVMF_VARS.fd edk2-i386-vars.fd; do
                if [ -z "$OVMF_VARS_TEMPLATE" ] && [ -f "$d/$v" ]; then
                    OVMF_VARS_TEMPLATE="$d/$v"
                fi
            done
        done
        if [ -z "$OVMF_CODE" ] || [ -z "$OVMF_VARS_TEMPLATE" ]; then
            echo "run-networking.sh: --boot=uefi needs OVMF firmware (install edk2-ovmf)." \
                 "Looked for OVMF_CODE*.fd / OVMF_VARS*.fd under /usr/share/{edk2,OVMF,ovmf}" >&2
            exit 1
        fi

        # The variable store is written by the firmware, so it cannot be the
        # read-only system copy: give each run a private scratch copy and throw
        # it away on exit. Otherwise NVRAM boot entries accumulate across runs.
        OVMF_VARS=$(mktemp -t substrate-ovmf-vars-XXXXXX.fd)
        cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"

        echo "run-networking.sh: UEFI boot from rootfs.img (OVMF -> /EFI/BOOT/BOOTX64.EFI -> /vmunix)"
        echo "run-networking.sh: firmware $OVMF_CODE"
        echo "run-networking.sh: kernel + boot args come from the image's grub.cfg, not this script"
        ;;
esac

if [ "$BOOTMODE" != kernel ] && [ "$GFX" -eq 1 ]; then
    echo "run-networking.sh: --gfx has no effect with --boot=$BOOTMODE:" \
         "/vmunix is the framebuffer kernel and GRUB picks the mode"
fi

# qemu runs in the foreground (not exec'd) so the EXIT trap can tear the
# macvtap down when it exits.
#
# i8042=off disables the emulated PS/2 controller (keyboard and mouse).
# substrate's PS/2 mouse path is unreliable, and usb-kbd/usb-mouse below cover
# input, so this leaves a single clean USB pointer on /dev/input/event0.
#
# The mode-specific arguments go in "$@" rather than a string: -append carries
# spaces and the firmware paths could too, and only the positional parameters
# survive that intact through an unquoted expansion.  They are free to reuse --
# the script's own arguments were consumed by the parser above.
set --
case "$BOOTMODE" in
    kernel)
        set -- -kernel "$KERNEL" -append "$APPEND"
        ;;
    uefi)
        # unit=0 is the firmware (read-only), unit=1 the variable store. The
        # variable store must be writable, hence the throwaway copy.
        set -- -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE" \
               -drive "if=pflash,format=raw,unit=1,file=$OVMF_VARS"
        ;;
esac

"$QEMU_BIN" -cpu "$QEMU_CPU" $ACCEL_ARG \
  -smp "$SMP" \
  -m "$MEM" \
  -machine "$QEMU_MACHINE" \
  $SNAPSHOT_ARG \
  "$@" \
  -drive file=rootfs.img,format=raw,if=none,id=drive0 \
  -device ich9-ahci,id=sata0$BOOT_AHCI_ADDR \
  $ROOT_DEV_ARGS \
  $EXTRA_DRIVE_ARGS \
  $EXTRA_CTRL_ARGS \
  $FLOPPY_ARGS \
  $USB_CTRL -device usb-kbd$USB_BUS -device usb-mouse$USB_BUS \
  $USB_HOST_ARGS \
  $NETDEV_ARGS \
  $GFX_ARGS \
  $DEBUG_ARGS \
  -serial stdio \
  $AUDIO_ARGS
