#!/bin/sh
# contrib/tmux/build.sh — cross-build tmux for substrate.
#
# Depends on libevent (contrib/libevent) for its event loop and on ncurses
# (contrib/ncurses, the wide-character build) for terminfo; both are taken
# from their staging trees.  utf8proc is not ported and tmux does without
# it, using its own width tables.
#
# Configured for the substrate host, so tmux's per-platform file is
# osdep-unknown.c: tmux does not learn the name or the working directory
# of the process in a pane from the system, which costs the automatic
# window names and #{pane_current_path}, and nothing else.
#
# The parser (cmd-parse.y) is built with the host's yacc.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
PKG="tmux"; VERSION="3.7c"
TREE_DIR="${HERE}/build/tmux-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
SR="${HERE}/build/sysroot"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
substrate_config_sub_fix "${TREE_DIR}"
substrate_sysroot "${SR}" libevent ncurses

rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --sysconfdir=/etc --mandir=/usr/share/man \
    --disable-utf8proc \
    CC=i386-unknown-substrate-gcc \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
printf '\100' | dd of="${DESTDIR}/usr/bin/tmux" bs=1 seek=7 count=1 conv=notrunc status=none
echo "==> ${PKG} staged under ${DESTDIR}"
