#!/bin/sh
# contrib/disasterparty/build.sh — cross-build disasterparty for substrate.
# Produces libdisasterparty.a, the disasterparty.h header, and the
# disasterparty.pc pkg-config file.  Depends on the staged libcurl + libcjson
# (and curl's openssl/zlib chain).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="0.6.0"
TREE_DIR="${HERE}/build/disasterparty-${VERSION}"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-disasterparty}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# Pin pkg-config + include/lib search at the substrate dist trees so neither the
# curl/cjson probes nor the link pick up host libraries.
PKGP=""; CPP=""; LDF=""
for d in cjson curl openssl zlib; do
    st="${SUBSTRATE_TOP}/dist-${d}"
    [ -d "${st}/usr" ] || continue
    [ -d "${st}/usr/lib/pkgconfig" ] && PKGP="${PKGP}${PKGP:+:}${st}/usr/lib/pkgconfig"
    [ -d "${st}/usr/include" ] && CPP="${CPP} -I${st}/usr/include"
    [ -d "${st}/usr/lib" ] && LDF="${LDF} -L${st}/usr/lib"
done
export PKG_CONFIG_LIBDIR="${PKGP}"
export CPPFLAGS="${CPP}"
export LDFLAGS="${LDF}"

cd "${TREE_DIR}"
[ -f Makefile ] && make distclean >/dev/null 2>&1 || true
# Teach the (autoreconf-generated) libtool that substrate builds ELF shared
# libraries so --enable-shared yields libdisasterparty.so, not just the .a.
sh "${HERE}/../substrate-libtool-shared.sh" ./configure
echo "==> configure"
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"

# Build + install only the library, header, and pkg-config file.  The tests/
# subdir links runnable programs against live API endpoints — out of scope for
# a cross build — and man/ is documentation; install both lib bits directly.
echo "==> make -j${JOBS} -C src"
make -j"${JOBS}" -C src

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make -C src install DESTDIR="${DESTDIR}"
# The .pc is generated at top level by configure from disasterparty.pc.in.
mkdir -p "${DESTDIR}/usr/lib/pkgconfig"
install -m 0644 disasterparty.pc "${DESTDIR}/usr/lib/pkgconfig/"
if [ -d man ]; then make -C man install DESTDIR="${DESTDIR}" 2>/dev/null || true; fi
rm -f "${DESTDIR}"/usr/lib/*.la
echo "==> Done.  disasterparty staged at ${DESTDIR}/usr/{lib/libdisasterparty.a,include/disasterparty.h}"
