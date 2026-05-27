#!/bin/sh
# run-auto-test.sh — boot substrate headlessly with a specific binary as
# init=, capture serial output, time out after N seconds.  Designed for
# iterating on kernel bugs without driving QEMU by hand.
#
# Usage:
#   ./run-auto-test.sh /tmp/torture_ipc
#   ./run-auto-test.sh /tmp/torture_pty
#   TIMEOUT=20 ./run-auto-test.sh /tmp/torture_unix

set -eu

INIT_PATH="${1:-/tmp/torture_ipc}"
INITARG="${INITARG:-}"
TIMEOUT="${TIMEOUT:-45}"
LOG="$(mktemp -t substrate-test-XXXXXX.log)"
trap 'rm -f "$LOG"' EXIT

if [ ! -f rootfs.img ]; then
    echo "run-auto-test: rootfs.img missing — run ./build-rootfs.sh --image first" >&2
    exit 1
fi
if [ ! -f sys/kernel.multiboot ]; then
    echo "run-auto-test: sys/kernel.multiboot missing — run make -C sys" >&2
    exit 1
fi

echo "==> Booting substrate with init=$INIT_PATH (timeout ${TIMEOUT}s)"

# -display none      headless
# -serial stdio      kernel + userland output streams to stdout
# -no-reboot         halt on triple-fault instead of looping
# -m 128M            same as run.sh
# -device ahci + -drive if=none,id= + -device ide-hd attaches the rootfs
#                    as an AHCI/SATA disk (substrate sees it at
#                    /dev/storage/sata0).  IDE PIO has been flaking under
#                    -no-reboot with high I/O load.
# -append init=...   substrate honors init= in cmdline (sys/kern/main.c:712)
timeout "$TIMEOUT" qemu-system-i386 \
    -cpu qemu32,+sse,+sse2 -accel kvm \
    -kernel sys/kernel.multiboot \
    -m 128M \
    -display none \
    -serial stdio \
    -no-reboot \
    -device ahci,id=ahci0 \
    -drive file=rootfs.img,format=raw,if=none,id=rootdisk \
    -device ide-hd,drive=rootdisk,bus=ahci0.0 \
    -usb -device usb-kbd \
    -append "serial_debug root=/dev/storage/sata0 init=$INIT_PATH${INITARG:+ initarg='$INITARG'}" \
    > "$LOG" 2>&1 || true

# Strip CR (substrate's serial uses \r\n).  Print only the test-program's
# section: from "torture_" through "Result: ..." (or the next 80 lines if
# we don't find Result yet — the test may have wedged before finishing).
echo "==> Serial transcript:"
echo "-----------------------------------------------------------------"
tr -d '\r' < "$LOG" | awk '
    /torture_/ { in_test = 1 }
    in_test    { print }
    /^Result:/ { found = 1; exit }
    END        { if (!found) print "(test did not reach Result: line)" }
'
echo "-----------------------------------------------------------------"

# Summarize
result_line=$(tr -d '\r' < "$LOG" | grep -E '^Result:' | head -1 || true)
if [ -n "$result_line" ]; then
    echo "==> $result_line"
    case "$result_line" in
        *FAILED*) exit 1 ;;
        *) exit 0 ;;
    esac
else
    echo "==> No Result: line emitted (test hung or panicked before completing)"
    exit 2
fi
