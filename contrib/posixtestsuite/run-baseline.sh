#!/bin/sh
#
# contrib/posixtestsuite/run-baseline.sh — build an OPTS test image from
# the staged binaries, boot substrate headlessly with the test image as a
# second disk, drive the whole suite in ONE boot (userland watchdog per
# test), and aggregate a per-area PASS/FAIL/UNSUPPORTED/... baseline.
#
# A test that panics the kernel is detected (dangling "OPTS|START" with no
# matching RESULT, or a serial stall), added to the skip-list, recorded as
# PANIC, and the run resumes on a fresh boot — up to MAX_BOOTS times.
#
# Env knobs:
#   SUBSTRATE_TOP   repo root holding rootfs.img + sys/kernel.multiboot
#   ACCEL           qemu accelerator: tcg (default, correct) | kvm (fast)
#   OPTS_TIMEOUT    per-test watchdog seconds inside the guest (default 10)
#   STALL           host: kill+resume if serial silent this many s (default 90)
#   HOST_TIMEOUT    host: hard cap per boot in seconds (default 5400)
#   MAX_BOOTS       max reboots to step over panics (default 12)
#   MANIFEST_OVERRIDE  path to an alternate manifest (subset runs)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-posixtestsuite}"
OUTROOT="${DESTDIR}/opt/posixtestsuite"
: "${ACCEL:=tcg}"
: "${OPTS_TIMEOUT:=10}"
: "${STALL:=90}"
: "${HOST_TIMEOUT:=5400}"
: "${MAX_BOOTS:=40}"

ROOTFS="${SUBSTRATE_TOP}/rootfs.img"
KERNEL="${SUBSTRATE_TOP}/sys/kernel.multiboot"
DRIVER="${HERE}/opts-driver.sh"
MANIFEST="${MANIFEST_OVERRIDE:-${OUTROOT}/manifest.txt}"

for f in "${ROOTFS}" "${KERNEL}" "${DRIVER}" "${MANIFEST}" "${OUTROOT}/bin"; do
    [ -e "${f}" ] || { echo "run-baseline: missing ${f}" >&2; exit 1; }
done

WORK="$(mktemp -d)"
RESULTS="${WORK}/results.psv"          # aggregated <area>|<name>|<verdict>|<rc>
SKIP="${WORK}/skip.txt"                # accumulates completed + panic culprits
: > "${RESULTS}"; : > "${SKIP}"
echo "run-baseline: work dir ${WORK}  accel=${ACCEL}  timeout=${OPTS_TIMEOUT}s"

ROOTCOPY="${WORK}/rootfs-opts.img"
echo "==> sparse-copying rootfs + injecting driver"
cp --sparse=always "${ROOTFS}" "${ROOTCOPY}"
debugfs -w -R "rm /opts-driver.sh" "${ROOTCOPY}" >/dev/null 2>&1 || true
debugfs -w -R "write ${DRIVER} /opts-driver.sh" "${ROOTCOPY}" >/dev/null 2>&1
debugfs -w -R "sif /opts-driver.sh mode 0100755" "${ROOTCOPY}" >/dev/null 2>&1

# Refresh the two runtime libs the new-area tests need (into the DISPOSABLE
# boot copy only — never the real rootfs.img).  The shipped rootfs libpthread
# predates named POSIX semaphores (no sem_open); librt.so.0 (mq_*/aio_*) is
# not shipped at all.  A handful of writes into a throwaway sparse copy is
# safe (unlike mass injection into the real image).
inject_lib() {   # inject_lib <hostfile> <name>
    [ -f "$1" ] || { echo "   (skip lib $2: $1 missing)"; return; }
    debugfs -w -R "rm /lib/$2" "${ROOTCOPY}" >/dev/null 2>&1 || true
    debugfs -w -R "write $1 /lib/$2" "${ROOTCOPY}" >/dev/null 2>&1
    debugfs -w -R "sif /lib/$2 mode 0100755" "${ROOTCOPY}" >/dev/null 2>&1
    echo "   refreshed /lib/$2"
}
# libc.so.0 too: the clock/sched error-path (EINVAL on bad clock/priority),
# the sigqueue/RT-signal and pthread_atfork fixes live in libc — not only in
# libpthread — so the disposable boot copy must carry the freshly built libc
# or the dynamically-linked tests bind the stale on-image libc and the fixes
# do not take effect.  Injected into the throwaway sparse copy only.
inject_lib "${SUBSTRATE_TOP}/lib/c/libc.so.0" libc.so.0
inject_lib "${SUBSTRATE_TOP}/lib/pthread/libpthread.so.0" libpthread.so.0
inject_lib "${SUBSTRATE_TOP}/lib/rt/librt.so.0" librt.so.0
# libsys.so.0 carries the raw syscall() signal-safety fix (the thunk no
# longer strands its return address below esp across int $0x80, so a signal
# delivered on a syscall return can't clobber it).  Without refreshing it here
# the aio/RT-signal-heavy tests (lio_listio/3-1,4-1,10-1,14-1,15-1) still bind
# the stale on-image libsys and SIGSEGV at EIP=0.
inject_lib "${SUBSTRATE_TOP}/lib/sys/libsys.so.0" libsys.so.0

TESTIMG="${WORK}/opts-test.img"

build_test_image() {
    stage="${WORK}/stage"; rm -rf "${stage}"; mkdir -p "${stage}/bin"
    cp -a "${OUTROOT}/bin/." "${stage}/bin/"
    cp "${MANIFEST}" "${stage}/manifest.txt"
    cp "${SKIP}" "${stage}/skip.txt"
    rm -f "${TESTIMG}"
    # size the image to comfortably hold the ELF tree
    mb=$(du -sm "${stage}" | cut -f1); mb=$((mb + 64))
    mke2fs -q -F -t ext2 -b 1024 -N 20000 -d "${stage}" "${TESTIMG}" "${mb}M"
}

boot_once() {   # boot_once <serial-log>
    log="$1"; : > "${log}"
    qemu-system-i386 \
        -cpu qemu32,+sse,+sse2,+rdrand -accel "${ACCEL}" \
        -kernel "${KERNEL}" -m 512M -display none -no-reboot \
        -serial file:"${log}" \
        -device ahci,id=ahci0 \
        -drive file="${ROOTCOPY}",format=raw,if=none,id=root0,snapshot=on \
        -device ide-hd,drive=root0,bus=ahci0.0 \
        -drive file="${TESTIMG}",format=raw,if=none,id=test0,snapshot=on \
        -device ide-hd,drive=test0,bus=ahci0.1 \
        -usb -device usb-kbd \
        -append "serial_debug root=/dev/storage/sata0 init=/opts-driver.sh OPTS_TIMEOUT=${OPTS_TIMEOUT}" \
        >/dev/null 2>&1 &
    qpid=$!
    # Poll: finish on OPTS|DONE, on qemu exit, on stall, or host timeout.
    start=$(date +%s); lastsize=0; laststamp=${start}
    while : ; do
        sleep 3
        if ! kill -0 "${qpid}" 2>/dev/null; then echo "qemu-exited"; return 0; fi
        if grep -q 'OPTS|DONE' "${log}" 2>/dev/null; then
            kill -9 "${qpid}" 2>/dev/null; wait "${qpid}" 2>/dev/null || true
            echo "done"; return 0
        fi
        now=$(date +%s)
        sz=$(wc -c < "${log}" 2>/dev/null || echo 0)
        if [ "${sz}" != "${lastsize}" ]; then lastsize=${sz}; laststamp=${now}; fi
        if [ $((now - laststamp)) -ge "${STALL}" ]; then
            kill -9 "${qpid}" 2>/dev/null; wait "${qpid}" 2>/dev/null || true
            echo "stalled"; return 0
        fi
        if [ $((now - start)) -ge "${HOST_TIMEOUT}" ]; then
            kill -9 "${qpid}" 2>/dev/null; wait "${qpid}" 2>/dev/null || true
            echo "host-timeout"; return 0
        fi
    done
}

# Ingest RESULT lines from a serial log into RESULTS (first real verdict wins),
# and add completed tests to SKIP so the next boot doesn't re-run them.
ingest() {   # ingest <serial-log>
    tr -d '\r' < "$1" | grep '^OPTS|RESULT|' | while IFS='|' read -r _ _ area name verdict rc; do
        key="${area}/${name}"
        case "${verdict}" in SKIP|NOBIN) continue ;; esac
        grep -q "^${area}|${name}|" "${RESULTS}" 2>/dev/null && continue
        printf '%s|%s|%s|%s\n' "${area}" "${name}" "${verdict}" "${rc}" >> "${RESULTS}"
        echo "${key}" >> "${SKIP}"
    done
}

boot=0
while [ "${boot}" -lt "${MAX_BOOTS}" ]; do
    boot=$((boot + 1))
    echo "==> boot #${boot} (building test image with $(wc -l < "${SKIP}") skipped)"
    build_test_image
    log="${WORK}/serial-${boot}.log"
    outcome=$(boot_once "${log}")
    ingest "${log}"
    echo "    outcome=${outcome}   results=$(wc -l < "${RESULTS}")"

    if grep -q 'OPTS|DONE' "${log}" 2>/dev/null; then
        echo "==> driver reported DONE"
        break
    fi
    # Kernel wedge/panic.  Execution is sequential and every completed test
    # was just added to SKIP by ingest(); prior culprits are in SKIP too.
    # So the wedge point is simply the first manifest entry not yet skipped
    # — this covers both a dangling OPTS|START and a stall that struck in
    # the gap between a RESULT and the next START.
    culprit=$(grep -vxF -f "${SKIP}" "${MANIFEST}" | head -1)
    if [ -n "${culprit}" ]; then
        echo "==> PANIC/wedge culprit: ${culprit} (${outcome})"
        printf '%s|%s|PANIC|kernel\n' "${culprit%%/*}" "${culprit#*/}" >> "${RESULTS}"
        echo "${culprit}" >> "${SKIP}"
    else
        echo "==> all tests handled but no DONE (late wedge); stopping"
        break
    fi
done

# ---------------- aggregate report ----------------
REPORT="${OUTROOT}/baseline-report.txt"
{
    echo "Open POSIX Test Suite — Substrate first conformance baseline"
    echo "accel=${ACCEL}  per-test-timeout=${OPTS_TIMEOUT}s  boots=${boot}"
    echo "kernel=${KERNEL}"
    echo
    areas="threads signals semaphores timers mmap sched message_queues aio other"
    printf '%-12s %5s %5s %5s %5s %5s %5s %5s %5s %6s\n' \
        AREA PASS FAIL UNRES UNSUP UNTST TMOUT CRASH PANIC TOTAL
    for a in ${areas}; do
        set -- 0 0 0 0 0 0 0 0 0
        p=0 f=0 u=0 s=0 t=0 to=0 c=0 pa=0 tot=0
        while IFS='|' read -r area name verdict rc; do
            [ "${area}" = "${a}" ] || continue
            tot=$((tot+1))
            case "${verdict}" in
                PASS) p=$((p+1));; FAIL) f=$((f+1));; UNRESOLVED) u=$((u+1));;
                UNSUPPORTED) s=$((s+1));; UNTESTED) t=$((t+1));;
                TIMEOUT) to=$((to+1));; CRASH) c=$((c+1));; PANIC) pa=$((pa+1));;
            esac
        done < "${RESULTS}"
        printf '%-12s %5s %5s %5s %5s %5s %5s %5s %5s %6s\n' "$a" "$p" "$f" "$u" "$s" "$t" "$to" "$c" "$pa" "$tot"
    done
    echo
    # totals
    awk -F'|' '{v[$3]++; n++} END {
        printf "TOTAL ran=%d  PASS=%d FAIL=%d UNRESOLVED=%d UNSUPPORTED=%d UNTESTED=%d TIMEOUT=%d CRASH=%d PANIC=%d\n",
        n, v["PASS"], v["FAIL"], v["UNRESOLVED"], v["UNSUPPORTED"], v["UNTESTED"], v["TIMEOUT"], v["CRASH"], v["PANIC"]
    }' "${RESULTS}"
    echo
    echo "=== FAIL / TIMEOUT / CRASH / PANIC (substrate conformance gaps) ==="
    grep -E '\|(FAIL|TIMEOUT|CRASH|PANIC)\|' "${RESULTS}" | sort || true
} | tee "${REPORT}"

cp "${RESULTS}" "${OUTROOT}/baseline-results.psv"
echo
echo "==> baseline report: ${REPORT}"
echo "==> raw results:     ${OUTROOT}/baseline-results.psv"
echo "==> work dir (logs): ${WORK}"
