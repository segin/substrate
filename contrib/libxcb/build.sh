#!/bin/sh
#
# contrib/libxcb/build.sh — cross-build libxcb for substrate.
# Produces:
#   /usr/lib/libxcb.so.1 + libxcb.a               core XCB library
#   /usr/lib/libxcb-<ext>.so.0 + .a               extension libraries
#                                                 (shm, render, ...)
#   /usr/include/xcb/*.h                          XCB headers
#   /usr/lib/pkgconfig/xcb*.pc                    metadata
#
# libxcb is the modern X C Binding.  Its build runs xcb-proto's
# xcbgen Python code generator (on the host) to turn the protocol
# XML into C source, then cross-compiles that.
#
# Depends on contrib/{xcb-proto,libXau,xorgproto} staged first.
#
# Env:
#   STAGE1_PREFIX     substrate toolchain prefix (default /opt/substrate)
#   XCBPROTO_STAGE    default ${SUBSTRATE_TOP}/dist-xcb-proto
#   LIBXAU_STAGE      default ${SUBSTRATE_TOP}/dist-libXau
#   XORGPROTO_STAGE   default ${SUBSTRATE_TOP}/dist-xorgproto
#   DESTDIR           staging dir (default ${SUBSTRATE_TOP}/dist-libxcb)
#   JOBS              parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.17.0"
TREE_DIR="${HERE}/build/libxcb-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XCBPROTO_STAGE:=${SUBSTRATE_TOP}/dist-xcb-proto}"
: "${LIBXAU_STAGE:=${SUBSTRATE_TOP}/dist-libXau}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-xorgproto}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-libxcb}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
for dep in "${XCBPROTO_STAGE}:xcb-proto" "${LIBXAU_STAGE}:libXau" \
           "${XORGPROTO_STAGE}:xorgproto"; do
    d=${dep%:*}; n=${dep#*:}
    [ -d "${d}/usr" ] || {
        echo "build.sh: ${n} not staged at ${d} — build contrib/${n} first" >&2
        exit 1
    }
done

# Locate xcb-proto's staged xcbgen Python package + protocol XML.
XCBPYTHONDIR=$(ls -d "${XCBPROTO_STAGE}"/usr/lib/python*/site-packages 2>/dev/null | head -1)
XCBINCLUDEDIR="${XCBPROTO_STAGE}/usr/share/xcb"
[ -n "${XCBPYTHONDIR}" ] && [ -d "${XCBPYTHONDIR}/xcbgen" ] || {
    echo "build.sh: xcbgen not found under ${XCBPROTO_STAGE}" >&2
    exit 1
}

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Restrict pkg-config to the staged X11 deps (+ the bundled
# pthread-stubs.pc) — never the host's /usr/lib/pkgconfig.
export PKG_CONFIG_LIBDIR="${XCBPROTO_STAGE}/usr/lib/pkgconfig:${LIBXAU_STAGE}/usr/lib/pkgconfig:${XORGPROTO_STAGE}/usr/lib/pkgconfig:${HERE}/pkgconfig"
export CPPFLAGS="-I${XCBPROTO_STAGE}/usr/include -I${LIBXAU_STAGE}/usr/include -I${XORGPROTO_STAGE}/usr/include"
export LDFLAGS="-L${LIBXAU_STAGE}/usr/lib"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --includedir=/usr/include \
    --enable-shared \
    --enable-static \
    --without-doxygen \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

# xcb-proto's pkg-config variables resolve to on-target paths
# (/usr/share/xcb, /usr/lib/python*/site-packages); override them
# at make time with the real host staging paths so the xcbgen code
# generator runs against the freshly-built xcb-proto.
echo "==> make -j${JOBS}"
make -j"${JOBS}" \
    XCBPROTO_XCBINCLUDEDIR="${XCBINCLUDEDIR}" \
    XCBPROTO_XCBPYTHONDIR="${XCBPYTHONDIR}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}" \
    XCBPROTO_XCBINCLUDEDIR="${XCBINCLUDEDIR}" \
    XCBPROTO_XCBPYTHONDIR="${XCBPYTHONDIR}"

# Drop libtool archives — substrate links against .a / .so directly.
rm -f "${DESTDIR}"/usr/lib/*.la

# Post-patch every produced .so OSABI byte to ELFOSABI_SUBSTRATE
# (0x40).  substrate's cross-ld stamps SYSV otherwise.
_n=0
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    _n=$((_n + 1))
done
echo "  OSABI->substrate on ${_n} shared objects"

echo "==> Done.  libxcb staged under ${DESTDIR}"
