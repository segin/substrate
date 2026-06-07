#!/bin/sh
#
# contrib/libtirpc/build.sh — cross-build libtirpc (Sun RPC) for substrate.
# Produces libtirpc.so.3 + the <rpc/*.h> headers under /usr/include/tirpc.
# This is the Sun RPC implementation CDE's ToolTalk requires.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-libtirpc)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="1.3.5"
TREE_DIR="${HERE}/build/libtirpc-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-libtirpc}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

# -lpthread must resolve the shared libpthread for a clean DT_NEEDED.
SYSLIB="${STAGE1_PREFIX}/i386-unknown-substrate/lib"
[ -e "${SYSLIB}/libpthread.so" ] || ln -sf libpthread.so.0 "${SYSLIB}/libpthread.so" 2>/dev/null || true

cd "${TREE_DIR}"

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

# CFLAGS notes:
#   -D__linux__          libtirpc gates its pthread thread-abstraction
#                        (reentrant.h) and reserved-port code on __linux__;
#                        substrate is pthread+ELF+BSD-sockets, so the Linux
#                        path is the right one (only 2 such guards in the tree).
#   -std=gnu11           old () prototypes; GCC 16/C23 would read them as (void).
#   -include string.h/stdlib.h
#                        several source files use memset/malloc without the
#                        include (they leaned on glibc's transitive headers).
#   -Wno-error=...       demote GCC 16's promoted-to-error legacy warnings.
CF="-march=i486 -mtune=i486 -O2 -g -fPIC -std=gnu11 -D__linux__ \
    -include string.h -include stdlib.h \
    -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
    -Wno-error=implicit-function-declaration -Wno-error=format"

echo "==> configure"
./configure \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --disable-gssapi \
    --disable-static \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CC_FOR_BUILD=gcc \
    CFLAGS="${CF}" \
    LIBS="-lpthread"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
rm -f "${DESTDIR}"/usr/lib/*.la

# OSABI -> ELFOSABI_SUBSTRATE on the shared objects.
_n=0
for so in "${DESTDIR}"/usr/lib/*.so.*; do
    [ -f "${so}" ] || continue
    [ -L "${so}" ] && continue
    printf '\100' | dd of="${so}" bs=1 seek=7 count=1 conv=notrunc status=none
    _n=$((_n + 1))
done
echo "  OSABI->substrate on ${_n} shared objects"
echo "==> Done.  libtirpc staged under ${DESTDIR} (headers in /usr/include/tirpc)"
