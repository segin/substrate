#!/bin/sh
# run-torture-halt.sh <torture_halt binary> [kernel]
#
# Boots the kernel (default sys/kernel.multiboot) with <binary> as init on
# a throwaway copy of rootfs.img, under QEMU with -no-reboot so that a
# reset ends QEMU.  torture_halt asks for reboot(RB_HALT_SYSTEM); the test
# passes if the kernel reports the halt and QEMU is still running five
# seconds later, i.e. the machine halted instead of resetting.
set -u
TOP=$(cd "$(dirname "$0")/../.." && pwd)
BIN=$1
KERNEL=${2:-$TOP/sys/kernel.multiboot}
OFF=$((104448 * 512))
T=$(mktemp -d)
trap 'kill "$QP" 2>/dev/null; rm -rf "$T"' EXIT

cp --reflink=auto "$TOP/rootfs.img" "$T/img"
printf 'write %s /torture_halt\nsif /torture_halt mode 0100755\n' "$BIN" |
    debugfs -w -f - "$T/img?offset=$OFF" >/dev/null 2>&1

qemu-system-i386 -m 256M -kernel "$KERNEL" -display none -serial stdio \
    -no-reboot -drive file="$T/img",format=raw,if=virtio \
    -append "serial_debug root=LABEL=sub-root init=/torture_halt" \
    > "$T/log" 2>&1 &
QP=$!

i=0
while [ $i -lt 120 ] && ! grep -q "torture_halt: halting" "$T/log"; do
    kill -0 "$QP" 2>/dev/null || break
    sleep 0.5
    i=$((i + 1))
done
sleep 5

halted=0
grep -q "System halted" "$T/log" && halted=1
if kill -0 "$QP" 2>/dev/null && [ $halted = 1 ]; then
    echo "  ok   the machine halted and stayed down"
    echo "Result: PASSED"
    exit 0
fi
if kill -0 "$QP" 2>/dev/null; then
    echo "  FAIL QEMU still running but no \"System halted\""
else
    echo "  FAIL QEMU exited: the machine was reset"
fi
tr -d '\r' < "$T/log" | tail -5
echo "Result: FAILED"
exit 1
