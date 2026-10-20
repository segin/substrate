#!/bin/sh
# run-torture.sh <torture binary> [file ...]
#
# Boots the kernel with a guest torture test (built by `make torture`) as
# init and reports its "Result:" line.  The binary goes in /tmp of a
# throwaway copy of the root image, each further file in / under its own
# name (torture_setuid wants /setuid_helper and /setuid_preload.so).  The
# exit status is 0 when the test printed a passing Result line.
#
# Needs a built rootfs.img and kernel; the environment overrides where:
#   KERNEL     kernel to boot            (default sys/kernel.multiboot)
#   IMAGE      partitioned root image    (default rootfs.img)
#   TIMEOUT    seconds to wait for Result (default 180)
#   QEMU_ARGS  extra QEMU arguments: the disk, network device or sound
#              card a test's header comment says it needs
#
# The copy is made beside IMAGE so that a reflink can share its blocks.
set -u
TOP=$(cd "$(dirname "$0")/../.." && pwd)
[ $# -ge 1 ] || { echo "usage: $0 <torture binary> [file ...]" >&2; exit 2; }
BIN=$1
shift
NAME=$(basename "$BIN")
KERNEL=${KERNEL:-$TOP/sys/kernel.multiboot}
IMAGE=${IMAGE:-$TOP/rootfs.img}
TIMEOUT=${TIMEOUT:-180}
QEMU_ARGS=${QEMU_ARGS:-}
OFF=$((104448 * 512))           # the ext2 root partition (p2)

for f in "$BIN" "$KERNEL" "$IMAGE" "$@"; do
    [ -f "$f" ] || { echo "run-torture: $f: not found" >&2; exit 2; }
done

T=$(mktemp -d "$(dirname "$IMAGE")/.run-torture.XXXXXX") || exit 2
QP=
trap '[ -n "$QP" ] && kill "$QP" 2>/dev/null; rm -rf "$T"' EXIT INT TERM

cp --reflink=auto "$IMAGE" "$T/img" || exit 2
{
    printf 'rm /tmp/%s\nwrite %s /tmp/%s\nsif /tmp/%s mode 0100755\n' \
        "$NAME" "$BIN" "$NAME" "$NAME"
    for f in "$@"; do
        b=$(basename "$f")
        printf 'rm /%s\nwrite %s /%s\nsif /%s mode 0100755\n' "$b" "$f" "$b" "$b"
    done
} | debugfs -w -f - "$T/img?offset=$OFF" >/dev/null 2>&1

# shellcheck disable=SC2086
qemu-system-i386 -m 256M -kernel "$KERNEL" -display none -serial stdio \
    -no-reboot -drive file="$T/img",format=raw,if=virtio $QEMU_ARGS \
    -append "serial_debug root=LABEL=sub-root init=/tmp/$NAME" \
    > "$T/log" 2>&1 &
QP=$!

i=0
while [ $i -lt $((TIMEOUT * 2)) ] && ! grep -q "Result:" "$T/log"; do
    kill -0 "$QP" 2>/dev/null || break
    sleep 0.5
    i=$((i + 1))
done
sleep 1

# Everything from the moment the kernel starts the test.
tr -d '\r' < "$T/log" | sed -n '/kinit: Trying/,$p'
line=$(tr -d '\r' < "$T/log" | grep "Result:" | tail -1)
case "$line" in
    "")      echo "run-torture: $NAME printed no Result line"; exit 1 ;;
    *FAIL*)  exit 1 ;;
    *PASS*)  exit 0 ;;
    *)       exit 1 ;;
esac
