#!/bin/sh
#
# build.sh — configure + build + install zsh for substrate.
#
# Env:
#   STAGE1_PREFIX   default /opt/substrate
#   DESTDIR         default ${SUBSTRATE_TOP}/dist-overlay/dist-zsh
#   JOBS            default `nproc`

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="5.9"
TREE_DIR="${HERE}/build/zsh-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-zsh}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

echo "==> configure"
# --without-tcsetpgrp avoids autoconf failing the runtime test when
#   it can't actually exec on the cross host; we know substrate has
#   tcsetpgrp from its tty layer.
# --disable-dynamic disables loadable modules (no /usr/lib/zsh/* needed,
#   one self-contained binary).
# --disable-cap avoids linking libcap.
# --with-tcsetpgrp gives the right value so we don't fail the runtime probe.
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --bindir=/usr/bin \
    --sysconfdir=/etc \
    --enable-pcre=no \
    --disable-dynamic \
    --disable-cap \
    --disable-gdbm \
    --disable-multibyte \
    --with-tcsetpgrp \
    CFLAGS="-O2 -g -march=i486 -mtune=i486 -Wno-incompatible-pointer-types"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  Staged at ${DESTDIR}/usr/bin/zsh"
