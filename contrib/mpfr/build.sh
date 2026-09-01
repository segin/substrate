#!/bin/sh
# build.sh — configure + build + install GNU MPFR for substrate.
#
# Depends on contrib/gmp being staged first: MPFR is built on top of GMP
# and its configure links a probe against -lgmp.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="4.2.2"
TREE_DIR="${HERE}/build/mpfr-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-mpfr}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${SR}/include/gmp.h" ] || {
    echo "build.sh: gmp not in the cross sysroot at ${SR}" >&2
    echo "         build contrib/gmp first" >&2
    exit 1
}

# Same two one-line retrofits the other autotools ports need: teach the
# bundled config.sub the substrate OS, and teach libtool that it is
# shared-library-capable.  Without the second, --enable-shared silently
# produces only libmpfr.a.
for sub in "${TREE_DIR}/config.sub" "${TREE_DIR}/configfsf.sub"; do
    [ -f "${sub}" ] || continue
    grep -q 'substrate\*' "${sub}" || sed -i 's/\(| sortix\* \)/\1| substrate* /' "${sub}"
done
if ! grep -q 'gnu\* | substrate\*)' "${TREE_DIR}/configure"; then
    sed -i 's/linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*)/linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*)/g' \
        "${TREE_DIR}/configure"
fi

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --build="$("${TREE_DIR}/config.guess")" \
    --prefix=/usr \
    --enable-shared --enable-static \
    --with-gmp="${SR}" \
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
for so in $(find "${DESTDIR}" -name 'libmpfr*.so*' -type f 2>/dev/null); do
    cur=$(od -An -tx1 -j7 -N1 "${so}" | tr -d ' ')
    [ "${cur}" = "40" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none 2>/dev/null && _n=$((_n+1))
done
echo "  OSABI->substrate on ${_n} shared objects"

# Mirror into the cross sysroot so gdb's --with-mpfr=${SR} finds it.
mkdir -p "${SR}/lib" "${SR}/include"
cp -a "${DESTDIR}/usr/lib/." "${SR}/lib/" 2>/dev/null || true
cp -a "${DESTDIR}/usr/include/." "${SR}/include/" 2>/dev/null || true

# Drop the libtool archive from the cross sysroot for the same reason
# contrib/gmp does: its libdir='/usr/lib' makes a cross-link's libtool
# resolve -lmpfr to the BUILD HOST's /usr/lib/libmpfr.so, which is the
# wrong architecture ("file in wrong format").  The dist-overlay copy keeps
# it, where /usr/lib is the correct target path.
rm -f "${SR}/lib/libmpfr.la"

echo "==> Done.  Staged at ${DESTDIR}/usr/{lib/libmpfr.so*,include/mpfr.h}; mirrored into ${SR}"
