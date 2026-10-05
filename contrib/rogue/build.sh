#!/bin/sh
#
# contrib/rogue/build.sh — configure + build + install Rogue 5.4.4, the
# original dungeon crawl, for substrate.  Produces:
#   /usr/bin/rogue                        (setgid games, for the scoreboard)
#   /var/games/rogue/                     (scoreboard and its lock file)
#   /usr/share/man/man6/rogue.6
#   /usr/share/doc/rogue/                 (the Guide to the Dungeons of Doom)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="5.4.4"
TREE_DIR="${HERE}/build/rogue${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-rogue}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${SR}/include/curses.h" ] || { echo "ncurses not staged -- build contrib/ncurses first" >&2; exit 1; }

cd "${TREE_DIR}"

SCOREDIR=/var/games/rogue

echo "==> configure"
# The scoreboard is shared by every player, so the game is setgid games and
# the directory is group-writable; build-rootfs.sh applies both, inside the
# image.  --enable-setgid is NOT given: it makes `make install` chgrp and
# chmod the staged binary, which an unprivileged build cannot do.
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --mandir=/usr/share/man \
    --docdir=/usr/share/doc/rogue \
    --with-ncurses \
    --enable-scorefile="${SCOREDIR}/rogue.scr" \
    --enable-lockfile="${SCOREDIR}/rogue.lck" \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
mkdir -p "${DESTDIR}${SCOREDIR}"

# The Makefile picks the manual page's directory by testing whether
# /usr/share/man/man6 exists on the BUILD machine, and drops the page
# straight into man/ if it does not.
if [ -f "${DESTDIR}/usr/share/man/rogue.6" ]; then
    mkdir -p "${DESTDIR}/usr/share/man/man6"
    mv "${DESTDIR}/usr/share/man/rogue.6" "${DESTDIR}/usr/share/man/man6/rogue.6"
fi
[ -f "${DESTDIR}/usr/share/man/man6/rogue.6" ] || { echo "build.sh: rogue.6 was not installed" >&2; exit 1; }

printf '\100' | dd of="${DESTDIR}/usr/bin/rogue" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null

echo "==> Done.  /usr/bin/rogue staged under ${DESTDIR}"
