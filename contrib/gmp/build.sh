#!/bin/sh
# build.sh — configure + build + install GNU GMP for substrate.
#
# --disable-assembly: GMP's hand-written x86 asm is CPU-path-selected from
# the host triplet; the portable C ("none") path is correct everywhere and
# avoids the substrate-target asm/config.sub friction for this first port
# (kcalc needs correctness, not GMP's peak throughput).  ABI=32 for i386.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="6.3.0"
TREE_DIR="${HERE}/build/gmp-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-gmp}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Teach GMP's bundled FSF config.sub the substrate OS (it rejects an
# unknown OS otherwise).  GMP's own config.sub is a CPU-remapping wrapper
# around configfsf.sub, which carries the FSF OS list — same one-line
# insert the other autotools ports use (after the `sortix*` token).
if ! grep -q 'substrate\*' "${TREE_DIR}/configfsf.sub"; then
    sed -i 's/\(| sortix\* \)/\1| substrate* /' "${TREE_DIR}/configfsf.sub"
fi

# GMP ships libtool 2.4.6 (2015), whose dynamic-linker case doesn't treat an
# unknown GNU-ish OS as shared-lib-capable, so `--enable-shared` silently
# produced only libgmp.a.  Give substrate the linux treatment in every
# `case $host_os` shared-lib branch so libtool emits version_type=linux +
# the proper soname/library_names/archive_cmds and builds libgmp.so.
# (libiconv builds fine because its libtool 2.4.7 already handles this.)
if ! grep -q 'gnu\* | substrate\*)' "${TREE_DIR}/configure"; then
    sed -i 's/linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*)/linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*)/g' \
        "${TREE_DIR}/configure"
fi

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --build="$("${TREE_DIR}/configfsf.guess" 2>/dev/null || "${TREE_DIR}/config.guess")" \
    --prefix=/usr \
    --enable-shared --enable-static \
    --disable-assembly \
    ABI=32 \
    CC="i386-unknown-substrate-gcc" \
    CFLAGS="-std=gnu17 -march=i486 -mtune=i486 -O2 -g"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# Cross-ld normally stamps OSABI=64, but stamp defensively in case a host
# libtool relink slipped a SysV (0) byte in.
_n=0
for so in $(find "${DESTDIR}" -name 'libgmp*.so*' -type f 2>/dev/null); do
    cur=$(od -An -tx1 -j7 -N1 "${so}" | tr -d ' ')
    [ "${cur}" = "40" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none 2>/dev/null && _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"

# Mirror into the cross sysroot so later configure probes (kcalc's
# gmp_asprintf check) and links find libgmp + gmp.h.
mkdir -p "${SR}/lib" "${SR}/include"
cp -a "${DESTDIR}/usr/lib/." "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}/usr/include/." "${SR}/include/" 2>/dev/null || true

echo "==> Done.  Staged at ${DESTDIR}/usr/{lib/libgmp.so*,include/gmp.h}; mirrored into ${SR}"
