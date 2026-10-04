#!/bin/bash
#
# boot-test.sh — boot rootfs64.img on the 64-bit kernel and run the smoke
# test on it (smokeinit + smoke.sh, both staged into the image by
# build64.sh).
#
# The kernel is loaded directly (qemu -kernel sys/kernel-x86_64.bin), not
# through the image's GRUB: what is under test is the 64-bit userland and
# the root filesystem, and this way the test needs no firmware.  The image
# is opened with -snapshot, so it is never written to.
#
# Env:
#   IMAGE     image to boot                  (default: rootfs64.img)
#   KERNEL    kernel for qemu -kernel        (default: sys/kernel-x86_64.bin)
#   TIMEOUT   seconds to wait for a verdict  (default: 600; without KVM
#             the guest is emulated and a boot takes minutes)
#   MEM       guest memory                   (default: 1G)
#
# Exit status: 0 if the guest printed "SMOKE64: PASS", 1 otherwise.  The
# serial log is left in $LOG (default: tests/rootfs64/boot-test.log).

set -u

TOP="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE="${IMAGE:-$TOP/rootfs64.img}"
KERNEL="${KERNEL:-$TOP/sys/kernel-x86_64.bin}"
TIMEOUT="${TIMEOUT:-600}"
MEM="${MEM:-1G}"
LOG="${LOG:-$TOP/tests/rootfs64/boot-test.log}"

for f in "$IMAGE" "$KERNEL"; do
    [ -f "$f" ] || { echo "boot-test.sh: missing $f" >&2; exit 1; }
done
command -v qemu-system-x86_64 >/dev/null 2>&1 || {
    echo "boot-test.sh: qemu-system-x86_64 not found" >&2; exit 1; }

ACCEL=()
if [ -w /dev/kvm ]; then
    ACCEL=(-accel kvm)
else
    echo "boot-test.sh: no /dev/kvm, emulating (slow)"
fi

: > "$LOG"
qemu-system-x86_64 "${ACCEL[@]}" -m "$MEM" -display none -no-reboot \
    -serial stdio -kernel "$KERNEL" \
    -drive "file=$IMAGE,format=raw,if=virtio,snapshot=on" \
    -append "serial_debug root=LABEL=sub-root64 init=/usr/libexec/rootfs64/smokeinit" \
    > "$LOG" 2>&1 &
QEMU=$!

end=$((SECONDS + TIMEOUT))
verdict=
while [ $SECONDS -lt $end ] && kill -0 "$QEMU" 2>/dev/null; do
    if grep -qa 'SMOKE64: PASS' "$LOG"; then verdict=pass; break; fi
    if grep -qa 'SMOKE64: FAIL\|KERNEL PANIC' "$LOG"; then verdict=fail; break; fi
    sleep 1
done
kill "$QEMU" 2>/dev/null
wait "$QEMU" 2>/dev/null

echo "--- guest output ---"
tr -d '\r' < "$LOG" | grep -a '^ok \|^FAIL \|^skip \|^     \|^smoke:\|SMOKE64\|PANIC' || true
case "$verdict" in
    pass) echo "boot-test.sh: PASS"; exit 0 ;;
    fail) echo "boot-test.sh: FAIL (see $LOG)"; exit 1 ;;
    *)    echo "boot-test.sh: no verdict within ${TIMEOUT}s (see $LOG)"
          tr -d '\r' < "$LOG" | tail -30
          exit 1 ;;
esac
