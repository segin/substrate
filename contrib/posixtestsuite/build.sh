#!/bin/sh
#
# contrib/posixtestsuite/build.sh — cross-compile the Open POSIX Test
# Suite (OPTS, the conformance tests shipped inside LTP) for substrate.
#
# Each OPTS test is a single .c that links against lib/common.c (which
# provides main() -> test_main()) and exits with a POSIX-test result
# code (PTS_PASS=0, PTS_FAIL=1, PTS_UNRESOLVED=2, PTS_UNSUPPORTED=4,
# PTS_UNTESTED=5).  We bypass upstream's per-directory Makefile harness
# and drive the cross-compile ourselves: glob the .c per interface,
# compile each against substrate libc + -Iinclude, link -lpthread -lm.
#
# Areas built:  threads (pthread_*), signals (sig*/kill/raise/...),
#               semaphores (sem_* incl. named sem_open/close/unlink),
#               timers (clock*/timer_*/nanosleep/time),
#               mmap (mmap/munmap/mlock*/shm_*), sched (sched_*),
#               message_queues (mq_*, functional/mqueues),
#               aio (aio_*, lio_listio), and "other"
#               (access/fork/getpid/str*/time-conversion) as a bonus.
# Nothing is skipped wholesale any more: as of main@5100a8e6 substrate
#               ships <mqueue.h>, <aio.h>, struct sigevent, named POSIX
#               semaphores (ksem) and a userspace librt, so mq/aio/named-sem
#               build.  Individual tests that need a POSIX symbol substrate
#               still lacks (e.g. _SC_ASYNCHRONOUS_IO, SIGRTMIN, siginfo
#               si_value) are recorded as build failures — a legit gap, not
#               hacked around.
#
# Output tree ($DESTDIR/opt/posixtestsuite):
#   bin/<area>/<relpath>          one ELF per buildable test
#   manifest.txt                  "<area>/<relpath>" per runnable test
#   build-report.txt              per-area BUILD-OK / BUILD-FAIL counts
#   build-failures.txt            "<area>/<relpath>  <first error line>"
#   opts-driver.sh                the on-target runner (copied in)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="ltp-open_posix_testsuite"
TREE="${HERE}/build/ltp/testcases/open_posix_testsuite"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-posixtestsuite}"
: "${CC:=${STAGE1_PREFIX}/bin/i386-unknown-substrate-gcc}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

[ -d "${TREE}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Make librt link dynamically: the sysroot usually ships only librt.a, so
# mirror the freshly built librt.so.0 (+ .so link) in so -lrt resolves to
# the shared object.  Falls back silently if either path is absent.
SRLIB="${STAGE1_PREFIX}/i386-unknown-substrate/lib"
if [ -f "${SUBSTRATE_TOP}/lib/rt/librt.so.0" ] && [ ! -e "${SRLIB}/librt.so" ]; then
    cp "${SUBSTRATE_TOP}/lib/rt/librt.so.0" "${SRLIB}/librt.so.0" 2>/dev/null &&
        ln -sf librt.so.0 "${SRLIB}/librt.so" 2>/dev/null &&
        echo "==> installed dynamic librt.so.0 into sysroot"
fi

CFLAGS="-std=gnu99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -w -fno-pie -Iinclude"
# Universal link line.  librt.a (static) supplies the mq_*/aio_* wrappers
# (they call raw syscall() from libsys, so -lsys is required); named POSIX
# semaphores (sem_open/close/unlink) live in libpthread; -lm covers the
# clock/math tests.  Unreferenced archives contribute nothing, so this is
# safe for every area.
LDLIBS="-lrt -lpthread -lsys -lm"

OUTROOT="${DESTDIR}/opt/posixtestsuite"
BINROOT="${OUTROOT}/bin"
rm -rf "${OUTROOT}"
mkdir -p "${BINROOT}"

MANIFEST="${OUTROOT}/manifest.txt"; : > "${MANIFEST}"
REPORT="${OUTROOT}/build-report.txt"; : > "${REPORT}"
FAILS="${OUTROOT}/build-failures.txt"; : > "${FAILS}"
ERRTMP="$(mktemp)"; trap 'rm -f "${ERRTMP}"' EXIT

cd "${TREE}"

# Map an interface/functional dir to a baseline area, or "" to skip.
classify() {
    case "$1" in
        testfrmw)                       echo "" ;;
        aio_*|lio_listio)               echo aio ;;
        mq_*|mqueues)                   echo message_queues ;;
        sig*|kill|killpg|raise|pthread_kill|pthread_sigmask) echo signals ;;
        pthread_*)                      echo threads ;;
        sem_*|semaphores)               echo semaphores ;;
        clock*|timer_*|timers|nanosleep|time) echo timers ;;
        mmap|munmap|mlock|mlockall|munlock|munlockall|shm_open|shm_unlink) echo mmap ;;
        sched_*|schedule)               echo sched ;;
        condvar)                        echo threads ;;
        clocks)                         echo timers ;;
        *)                              echo other ;;
    esac
}

# Per-area counters kept in files (POSIX sh has no assoc arrays).
CNTDIR="$(mktemp -d)"; trap 'rm -f "${ERRTMP}"; rm -rf "${CNTDIR}"' EXIT
bump() { f="${CNTDIR}/$1.$2"; n=0; [ -f "$f" ] && n=$(cat "$f"); echo $((n+1)) > "$f"; }

total_ok=0; total_fail=0

# $1 = source .c (relative to TREE), $2 = area, $3 = key (area/relpath)
compile_one() {
    src="$1"; area="$2"; key="$3"
    out="${BINROOT}/${key}"
    mkdir -p "$(dirname "${out}")"
    case "${src}" in
        *buildonly*)
            # Compile-only test: success == PASS, produces no runnable bin.
            if ${CC} ${CFLAGS} -c "${src}" -o /dev/null 2>"${ERRTMP}"; then
                bump "${area}" ok; total_ok=$((total_ok+1))
            else
                bump "${area}" fail; total_fail=$((total_fail+1))
                printf '%s  %s\n' "${key}" "$(head -1 "${ERRTMP}")" >> "${FAILS}"
            fi
            return
            ;;
    esac
    if ${CC} ${CFLAGS} "${src}" lib/common.c -o "${out}" ${LDLIBS} 2>"${ERRTMP}"; then
        bump "${area}" ok; total_ok=$((total_ok+1))
        echo "${key}" >> "${MANIFEST}"
    else
        bump "${area}" fail; total_fail=$((total_fail+1))
        rm -f "${out}"
        printf '%s  %s\n' "${key}" "$(grep -m1 -E 'error:|undefined reference' "${ERRTMP}" | head -1)" >> "${FAILS}"
    fi
}

echo "==> Compiling conformance tests"
for ifacedir in conformance/interfaces/*/; do
    iface="$(basename "${ifacedir}")"
    area="$(classify "${iface}")"
    [ -n "${area}" ] || continue
    for src in "${ifacedir}"*.c; do
        [ -f "${src}" ] || continue
        base="$(basename "${src}" .c)"
        compile_one "${src}" "${area}" "${area}/${iface}/${base}"
    done
done

echo "==> Compiling functional tests"
if [ -d functional ]; then
    find functional -name '*.c' | while IFS= read -r src; do
        top="$(echo "${src}" | cut -d/ -f2)"
        area="$(classify "${top}")"
        [ -n "${area}" ] || continue
        rel="${src#functional/}"; rel="${rel%.c}"
        echo "FUNC ${area} ${src} functional/${rel}"
    done > "${CNTDIR}/func.list"
    while read -r _tag area src key; do
        compile_one "${src}" "${area}" "${area}/functional/${key#functional/}"
    done < "${CNTDIR}/func.list"
fi

# Copy the on-target driver into the staged tree.
cp "${HERE}/opts-driver.sh" "${OUTROOT}/opts-driver.sh"
chmod +x "${OUTROOT}/opts-driver.sh"

# ---- build report -------------------------------------------------------
{
    echo "Open POSIX Test Suite — substrate cross-build report"
    echo "CC: ${CC}"
    echo "CFLAGS: ${CFLAGS}"
    echo "LDLIBS: ${LDLIBS}"
    echo
    printf '%-14s %8s %8s\n' "AREA" "BUILD-OK" "BUILD-FAIL"
    printf '%-14s %8s %8s\n' "----" "--------" "----------"
    for area in threads signals semaphores timers mmap sched message_queues aio other; do
        ok=0; fl=0
        [ -f "${CNTDIR}/${area}.ok" ]   && ok=$(cat "${CNTDIR}/${area}.ok")
        [ -f "${CNTDIR}/${area}.fail" ] && fl=$(cat "${CNTDIR}/${area}.fail")
        printf '%-14s %8s %8s\n' "${area}" "${ok}" "${fl}"
    done
    echo
    printf 'TOTAL build-ok=%s build-fail=%s runnable=%s\n' \
        "${total_ok}" "${total_fail}" "$(wc -l < "${MANIFEST}")"
    echo
    echo "No areas are skipped wholesale.  Per-test build failures (missing"
    echo "substrate POSIX symbols, e.g. _SC_ASYNCHRONOUS_IO / SIGRTMIN /"
    echo "siginfo si_value / assorted pthread_* timed/barrier/spin APIs) are"
    echo "listed in build-failures.txt."
} | tee "${REPORT}"

echo
echo "==> Staged under ${OUTROOT}"
echo "==> Runnable binaries: $(wc -l < "${MANIFEST}")   Build failures: ${total_fail}"
