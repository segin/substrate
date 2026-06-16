#!/bin/sh
#
# contrib/libXau/build.sh — cross-build libXau for substrate.
# Produces:
#   /usr/lib/libXau.a + libXau.so.6 + libXau.so
#   /usr/include/X11/Xauth.h
#   /usr/lib/pkgconfig/xau.pc
#
# libXau handles the X authority file (~/.Xauthority).  libxcb
# links it for connection authorisation.
#
# Depends on contrib/xorgproto being staged first.
#
# Env:
#   STAGE1_PREFIX     substrate toolchain prefix (default /opt/substrate)
#   XORGPROTO_STAGE   default ${SUBSTRATE_TOP}/dist-overlay/dist-xorgproto
#   DESTDIR           staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-libXau)
#   JOBS              parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.0.12"
TREE_DIR="${HERE}/build/libXau-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-xorgproto}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-libXau}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -d "${XORGPROTO_STAGE}/usr/include/X11" ] || {
    echo "build.sh: xorgproto not staged at ${XORGPROTO_STAGE}" >&2
    echo "         build contrib/xorgproto first" >&2
    exit 1
}

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

export PKG_CONFIG_PATH="${XORGPROTO_STAGE}/usr/lib/pkgconfig"
export CPPFLAGS="-I${XORGPROTO_STAGE}/usr/include"

# --disable-xthreads: libXau's optional thread-safety pulls in
# X11/Xthreads.h, which typedefs xthread_key_t from pthread_key_t.
# Substrate's libpthread implements no TLS-key API (no
# pthread_key_create / get/setspecific), so there is no
# pthread_key_t.  Disabling xthreads drops the internal mutex
# around the auth-file cache; libXau stays fully functional.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --includedir=/usr/include \
    --enable-shared \
    --enable-static \
    --disable-xthreads \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

# Drop libtool archives — substrate links against .a / .so directly.
rm -f "${DESTDIR}"/usr/lib/*.la

# Post-patch every produced .so OSABI byte to ELFOSABI_SUBSTRATE
# (0x40).  substrate's cross-ld stamps SYSV otherwise.
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    echo "  OSABI->substrate on $(basename "${so}")"
done

echo "==> Done.  libXau staged under ${DESTDIR}"
