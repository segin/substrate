#!/bin/sh
#
# contrib/libX11/build.sh — cross-build libX11 (Xlib) for substrate.
# Produces:
#   /usr/lib/libX11.so.6 + libX11.a               core Xlib
#   /usr/lib/libX11-xcb.so.1 + .a                 Xlib/XCB bridge
#   /usr/include/X11/*.h                          Xlib headers
#   /usr/lib/pkgconfig/x11*.pc                    metadata
#   /usr/share/X11/locale/                        locale + compose data
#
# libX11 is Xlib, the classic X11 client API, layered on libxcb.
#
# Depends on contrib/{xorgproto,xtrans,libXau,libxcb} staged first.
#
# Env:
#   STAGE1_PREFIX     substrate toolchain prefix (default /opt/substrate)
#   XORGPROTO_STAGE   default ${SUBSTRATE_TOP}/dist-xorgproto
#   XTRANS_STAGE      default ${SUBSTRATE_TOP}/dist-xtrans
#   LIBXAU_STAGE      default ${SUBSTRATE_TOP}/dist-libXau
#   LIBXCB_STAGE      default ${SUBSTRATE_TOP}/dist-libxcb
#   DESTDIR           staging dir (default ${SUBSTRATE_TOP}/dist-libX11)
#   JOBS              parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.8.12"
TREE_DIR="${HERE}/build/libX11-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${XORGPROTO_STAGE:=${SUBSTRATE_TOP}/dist-xorgproto}"
: "${XTRANS_STAGE:=${SUBSTRATE_TOP}/dist-xtrans}"
: "${LIBXAU_STAGE:=${SUBSTRATE_TOP}/dist-libXau}"
: "${LIBXCB_STAGE:=${SUBSTRATE_TOP}/dist-libxcb}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-libX11}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
for dep in "${XORGPROTO_STAGE}:xorgproto" "${XTRANS_STAGE}:xtrans" \
           "${LIBXAU_STAGE}:libXau" "${LIBXCB_STAGE}:libxcb"; do
    d=${dep%:*}; n=${dep#*:}
    [ -d "${d}/usr" ] || {
        echo "build.sh: ${n} not staged at ${d} — build contrib/${n} first" >&2
        exit 1
    }
done

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Restrict pkg-config to the staged X11 deps.  pthread-stubs.pc is
# pulled in transitively (xcb.pc lists it in Requires.private), so
# the bundled copy from contrib/libxcb is added to the search path.
export PKG_CONFIG_LIBDIR="${XORGPROTO_STAGE}/usr/lib/pkgconfig:${XTRANS_STAGE}/usr/lib/pkgconfig:${LIBXAU_STAGE}/usr/lib/pkgconfig:${LIBXCB_STAGE}/usr/lib/pkgconfig:${SUBSTRATE_TOP}/contrib/libxcb/pkgconfig"
export CPPFLAGS="-I${XORGPROTO_STAGE}/usr/include -I${XTRANS_STAGE}/usr/include -I${LIBXCB_STAGE}/usr/include -I${LIBXAU_STAGE}/usr/include"
export LDFLAGS="-L${LIBXCB_STAGE}/usr/lib -L${LIBXAU_STAGE}/usr/lib"

# Xlib is built with thread support (--enable-xthreads, the
# default): libX11 1.8 nests non-threading code — <sys/ioctl.h>,
# _Xglobal_lock — inside #ifdef XTHREADS, so --disable-xthreads
# does not build.  libX11 uses only pthread mutex / cond / self
# (no TLS keys), all implemented by substrate's libpthread.
# configure's host_os case has no substrate branch, so the two
# values it would otherwise set are supplied explicitly:
#   XTHREADLIB=-lpthread             link against libpthread
#   XTHREAD_CFLAGS=-D_POSIX_THREAD_SAFE_FUNCTIONS
#       tells X11/Xos_r.h to use the 5-argument POSIX getpwnam_r /
#       getpwuid_r (substrate's signature) instead of the 4-arg
#       draft form — mirrors what configure does for netbsd.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --libdir=/usr/lib \
    --includedir=/usr/include \
    --enable-shared \
    --enable-static \
    --disable-specs \
    --without-xmlto \
    --without-fop \
    --without-xsltproc \
    --with-keysymdefdir="${XORGPROTO_STAGE}/usr/include/X11" \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    XTHREADLIB=-lpthread \
    XTHREAD_CFLAGS=-D_POSIX_THREAD_SAFE_FUNCTIONS \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

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
_n=0
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    _n=$((_n + 1))
done
echo "  OSABI->substrate on ${_n} shared objects"

echo "==> Done.  libX11 staged under ${DESTDIR}"
