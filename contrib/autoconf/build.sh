#!/bin/sh
#
# build.sh — configure + build + install GNU Autoconf for substrate.
#
# Autoconf is perl and shell scripts plus m4 sources; nothing is compiled
# for the target, so this is a plain build on the host with the paths the
# scripts will use on substrate: /usr/bin/perl (contrib/perl) and
# /usr/bin/m4 (contrib/m4).  The host needs a perl and a GNU m4 at those
# same paths, which configure runs to check them and make runs to freeze
# autoconf.m4f; frozen state is portable across GNU m4 1.4.x.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.73"
TREE_DIR="${HERE}/build/autoconf-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-autoconf}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
echo "==> configure"
"${TREE_DIR}/configure" \
    --prefix=/usr \
    PERL=/usr/bin/perl \
    M4=/usr/bin/m4

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  Staged at ${DESTDIR}/usr/bin/autoconf"
