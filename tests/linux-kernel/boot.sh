#!/bin/sh
# tests/linux-kernel/boot.sh — boot the Linux kernel that substrate's own
# cc, as and ld built (contrib/linux-kernel/build.sh) in qemu, with the
# initramfs whose /init the same tools built, and check that /init ran:
# it prints "SUBSTRATE-LINUX-INIT: PASS" on the serial console and powers
# the machine off.
#
# Needs qemu-system-i386 and the build's outputs (build.sh first), or
# BZIMAGE and INITRAMFS naming others.  The serial log is kept in
# LOG (default: a temporary file, printed on failure).
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
TOP="$(cd "${HERE}/../.." && pwd)"
OUT="${TOP}/contrib/linux-kernel/build/out"
: "${BZIMAGE:=${OUT}/bzImage}"
: "${INITRAMFS:=${OUT}/initramfs.cpio}"
: "${QEMU:=qemu-system-i386}"
: "${TIMEOUT:=120}"

for f in "${BZIMAGE}" "${INITRAMFS}"; do
    [ -f "${f}" ] || { echo "boot.sh: no ${f} (run contrib/linux-kernel/build.sh)" >&2; exit 2; }
done
command -v "${QEMU}" >/dev/null 2>&1 || { echo "boot.sh: no ${QEMU}" >&2; exit 2; }

own_log=0
if [ -z "${LOG:-}" ]; then
    LOG=$(mktemp)
    own_log=1
fi

timeout "${TIMEOUT}" "${QEMU}" -m 128 -nographic -no-reboot -serial mon:stdio \
    -kernel "${BZIMAGE}" -initrd "${INITRAMFS}" \
    -append "console=ttyS0 earlyprintk=serial,ttyS0,115200 panic=-1" \
    < /dev/null > "${LOG}" 2>&1
rc=$?

if grep -q 'SUBSTRATE-LINUX-INIT: PASS' "${LOG}"; then
    grep 'SUBSTRATE-LINUX-INIT' "${LOG}"
    echo "ok: the substrate-built kernel booted to its substrate-built /init"
    [ "${own_log}" = 1 ] && rm -f "${LOG}"
    exit 0
fi
echo "FAIL: /init did not report (qemu rc=${rc}); serial log:" >&2
tail -60 "${LOG}" >&2
[ "${own_log}" = 1 ] && rm -f "${LOG}"
exit 1
