#!/bin/sh
#
# contrib/links/build.sh — configure + build + install the Twibright
# Links console web browser for substrate.  Produces:
#   /usr/bin/links
#   /usr/share/man/man1/links.1
#
# Console-only build: graphics mode is left disabled, so no X /
# framebuffer / image libraries are needed.  HTTPS comes from the
# substrate OpenSSL port and gzip/deflate content decoding from
# zlib — both are expected in the cross-toolchain sysroot.
#
# Env:
#   STAGE1_PREFIX   default /opt/substrate
#   DESTDIR         default ${SUBSTRATE_TOP}/dist-overlay/dist-links
#   JOBS            default `nproc`

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.30"
TREE_DIR="${HERE}/build/links-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-links}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# links ships an autoconf-2.13 configure: it takes CC / CFLAGS /
# LDFLAGS from the *environment* (the modern `./configure VAR=value`
# form is rejected as a stray host triple), and does not derive the
# cross compiler from --host, so CC must be set explicitly.
export CC="i386-unknown-substrate-gcc"
export CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie"
export LDFLAGS="-fno-pie -Wl,--copy-dt-needed-entries"
# links's configure runs link/run probes that a cross build cannot
# execute; pre-seed the cache values it cannot determine itself.
export ac_cv_path_install="/usr/bin/install -c"

echo "==> configure"
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --mandir=/usr/share/man \
    --without-libevent \
    --without-x \
    --with-ssl \
    --disable-ssl-pkgconfig

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  /usr/bin/links staged under ${DESTDIR}"
