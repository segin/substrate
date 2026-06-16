#!/bin/sh
#
# contrib/e2tools/build.sh — cross-build e2tools for substrate.
# Produces command-line tools that manipulate ext2/3/4 filesystem
# images without mounting them:
#   /usr/bin/{e2cp,e2ls,e2mkdir,e2rm,e2ln,e2mv,e2tail}
#
# Depends on contrib/e2fsprogs being staged first (libext2fs +
# libcom_err, headers and pkg-config files).
#
# Env:
#   STAGE1_PREFIX    substrate toolchain prefix (default /opt/substrate)
#   E2FSPROGS_STAGE  default ${SUBSTRATE_TOP}/dist-overlay/dist-e2fsprogs
#   DESTDIR          staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-e2tools)
#   JOBS             parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="0.1.0"
TREE_DIR="${HERE}/build/e2tools-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${E2FSPROGS_STAGE:=${SUBSTRATE_TOP}/dist-overlay/dist-e2fsprogs}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-e2tools}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${E2FSPROGS_STAGE}/usr/lib/pkgconfig/ext2fs.pc" ] || {
    echo "build.sh: e2fsprogs not staged at ${E2FSPROGS_STAGE}" >&2
    echo "         build contrib/e2fsprogs first" >&2
    exit 1
}

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Resolve libext2fs / libcom_err from the staged e2fsprogs tree.
# -include alloca.h: e2tools' util.c calls alloca() without
# including <alloca.h>; substrate does not re-expose alloca via
# <stdlib.h> the way glibc does.  The header is just a
# __builtin_alloca macro, so force-including it is harmless.
export PKG_CONFIG_LIBDIR="${E2FSPROGS_STAGE}/usr/lib/pkgconfig"
export CPPFLAGS="-I${E2FSPROGS_STAGE}/usr/include -include alloca.h"
export LDFLAGS="-L${E2FSPROGS_STAGE}/usr/lib -Wl,--copy-dt-needed-entries"

# LIBS: -lregex for POSIX regex (regcomp/regexec/regfree — in the separate
# libregex on substrate, see usr.lib/regex); -lpthread because libext2fs's
# unix_io.c uses pthread mutexes for I/O-channel thread-safety, and e2tools
# links libext2fs externally so the reference must be satisfied here.
echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar \
    RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LIBS="-lregex -lpthread"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  e2tools staged under ${DESTDIR}"
