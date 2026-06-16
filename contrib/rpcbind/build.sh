#!/bin/sh
#
# contrib/rpcbind/build.sh — cross-build rpcbind (the Sun RPC portmapper) for
# substrate.  Produces /sbin/rpcbind.  CDE's ToolTalk ttsession registers its
# RPC service with the portmapper (pmap_set), so rpcbind must run before the
# desktop starts (see /etc/rc.d).
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-rpcbind)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.2.6"
TREE_DIR="${HERE}/build/rpcbind-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-rpcbind}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

TIRPC="${SUBSTRATE_TOP}/dist-overlay/dist-libtirpc/usr"
[ -d "${TIRPC}/include/tirpc" ] || { echo "build.sh: build contrib/libtirpc first" >&2; exit 1; }

SYSLIB="${STAGE1_PREFIX}/i386-unknown-substrate/lib"
[ -e "${SYSLIB}/libpthread.so" ] || ln -sf libpthread.so.0 "${SYSLIB}/libpthread.so" 2>/dev/null || true
[ -e "${SYSLIB}/libtirpc.so" ]   || ln -sf libtirpc.so.3   "${SYSLIB}/libtirpc.so"   2>/dev/null || true
cp -a "${TIRPC}/lib/libtirpc.so"* "${SYSLIB}/" 2>/dev/null || true

cd "${TREE_DIR}"

# rpcbind ships no config.sub/config.guess; borrow the (already substrate-aware)
# pair from the libtirpc tree so AC_CANONICAL_HOST can canonicalize --host.
for f in config.sub config.guess; do
    [ -f "${f}" ] || cp "${SUBSTRATE_TOP}/contrib/libtirpc/build/libtirpc-1.3.5/${f}" "${f}"
done

# rpcb_svc_com.c includes glibc-internal <bits/poll.h> right after <poll.h>;
# substrate has no bits/poll.h and <poll.h> already defines POLLIN/... — drop it.
sed -i 's|^#include <bits/poll.h>|/* bits/poll.h dropped: <poll.h> provides POLLIN/... on substrate */|' \
    src/rpcb_svc_com.c

# Standard substrate autotools adjustments (idempotent).
if ! grep -q 'substrate\*' config.sub; then
    sed -i 's/\(| sortix\* \)/\1| substrate* /' config.sub
fi
if ! grep -q 'substrate\*' configure; then
    sed -i \
      -e 's@linux\* | k\*bsd\*-gnu | kopensolaris\*-gnu | gnu\*)@linux* | k*bsd*-gnu | kopensolaris*-gnu | gnu* | substrate*)@g' \
      -e 's@gnu\* | linux\* | tpf\* | k\*bsd\*-gnu | kopensolaris\*-gnu)@gnu* | linux* | tpf* | k*bsd*-gnu | kopensolaris*-gnu | substrate*)@g' \
      -e 's@^\(\s*\)linux\*)@\1linux* | substrate*)@g' \
      configure
fi

# CFLAGS:
#   -D__linux__   rpcbind has scattered Linux/BSD #ifdefs; substrate is the
#                 pthread+ELF+BSD-sockets Linux-shaped path.
#   -std=gnu11    old () prototypes (GCC 16/C23 would read them as (void)).
#   -I tirpc      rpcbind uses libtirpc's <rpc/*.h>.
#   -include ...  a few files use memset/strdup without the include.
#   -Wno-error    demote GCC 16's promoted-to-error legacy warnings.
CF="-march=i486 -mtune=i486 -O2 -g -std=gnu11 -D__linux__ \
    -I${TIRPC}/include/tirpc -include string.h -include stdlib.h \
    -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
    -Wno-error=implicit-function-declaration -Wno-error=format \
    -Wno-error=return-mismatch"

echo "==> configure"
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --sbindir=/sbin \
    --with-statedir=/var/run \
    --with-rpcuser=root \
    --disable-libwrap \
    --without-systemdsystemunitdir \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="${CF}" \
    TIRPC_CFLAGS="-I${TIRPC}/include/tirpc" \
    TIRPC_LIBS="-L${TIRPC}/lib -ltirpc" \
    LIBS="-lpthread"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  rpcbind staged under ${DESTDIR} (/sbin/rpcbind)"
