#!/bin/sh
#
# build.sh — build tzcode (zic, zdump) for substrate and compile the
# timezone database into TZif binaries.
#
# The HOST zic is what compiles the source `.tab`/`Africa`/`America`/
# etc. files into TZif binaries — output format is portable, so a
# host-built zic produces the same files a target-built zic would.
# We use the HOST toolchain to produce zic, then run zic to populate
# ${DESTDIR}/usr/share/zoneinfo.
#
# A separate substrate-target build of zic+zdump is staged under
# ${DESTDIR}/usr/sbin/zic and zdump so admins on the image can
# recompile zones if they install a newer tzdata.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-tzdata)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2024a"
TREE_DIR="${HERE}/build/tz-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-tzdata}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# --- Host zic + zdump (used to compile the zones) ---
# tzcode's Makefile uses make's default `CC := c99`; pass CC=cc
# explicitly so the host build uses our system compiler instead of
# the c99-wrapper-script glibc ships (which silently injects -static
# on some hosts).
echo "==> Host build of zic/zdump"
make -j"${JOBS}" \
    CC="${HOST_CC:-gcc}" \
    CFLAGS="-DHAVE_GETTEXT=0 -DTHREAD_SAFE=0 -DUSE_LTZ=0" \
    zic zdump

mkdir -p "${DESTDIR}/usr/share/zoneinfo"
echo "==> Compiling zoneinfo with host zic into ${DESTDIR}/usr/share/zoneinfo"
./zic -d "${DESTDIR}/usr/share/zoneinfo" \
    africa antarctica asia australasia europe northamerica southamerica \
    etcetera factory backward backzone

# Default /etc/localtime → UTC.  Admins on-image can ln -sf for a
# different default.
mkdir -p "${DESTDIR}/etc"
ln -sf /usr/share/zoneinfo/UTC "${DESTDIR}/etc/localtime"

# --- Target zic + zdump (installed onto the image) ---
echo "==> Cross-build of zic/zdump for substrate"
PATH="${STAGE1_PREFIX}/bin:${PATH}" \
    make -j"${JOBS}" clean
PATH="${STAGE1_PREFIX}/bin:${PATH}" \
    # Dynamic-link against substrate's libc.so.0.  -static fails on
    # substrate's cross-ld — the gcc spec injects -l:libc.so.0 which
    # -static then trips over as "attempted static link of dynamic
    # object".  Dynamic is fine; ld.so + libc.so.0 are present on
    # every substrate image.
    # --copy-dt-needed-entries lets the linker resolve symbols
    # through libc.so.0's DT_NEEDED chain (e.g. getrandom in
    # libsys.so.0) without forcing every caller to spell out -lsys.
    make -j"${JOBS}" \
        CC=i386-unknown-substrate-gcc \
        CFLAGS="-march=i486 -mtune=i486 -DHAVE_GETTEXT=0 -DTHREAD_SAFE=0 -DUSE_LTZ=0 -DHAVE_STRFTIME_L=0 -fno-pie" \
        LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries" \
        zic zdump

mkdir -p "${DESTDIR}/usr/sbin"
cp zic "${DESTDIR}/usr/sbin/zic"
cp zdump "${DESTDIR}/usr/sbin/zdump"

echo "==> Done.  Zoneinfo and zic/zdump staged under ${DESTDIR}"
