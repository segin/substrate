#!/bin/sh
#
# contrib/nethack/build.sh — build + install NetHack 3.6.7 for substrate,
# with the tty and curses interfaces.  Produces:
#   /usr/bin/nethack                 wrapper script
#   /usr/lib/nethack/                the game, recover, nhdat, sysconf
#   /var/games/nethack/              scores, logs, bones, saved games
#   /usr/share/man/man6/{nethack,recover}.6
#   /usr/share/doc/nethack/Guidebook.txt
#
# NetHack 3.6 has no notion of cross-compiling: its build compiles
# makedefs, lev_comp, dgn_comp and dlb with the one $(CC) and then RUNS
# them, to generate headers and to compile the dungeon and its levels into
# binary files the game reads back as raw C structures.  So it is built
# twice, from two copies of the tree:
#
#   host/    the four tools, with the build machine's compiler, and
#            everything they generate
#   target/  the game and recover, with the cross compiler, using the
#            generated files from host/
#
# The level files hold ints, longs and pointers-sized fields as the tool's
# compiler laid them out, so the tools are built for a machine with the
# TARGET's data model: -m32, here.  Both trees get the same feature
# defines, because makedefs records them in the options file and the game
# checks its own against it.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="3.6.7"
TREE_DIR="${HERE}/build/NetHack-${VERSION}"
HOST_DIR="${HERE}/build/host"
TARGET_DIR="${HERE}/build/target"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-nethack}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${SR}/include/curses.h" ] || { echo "ncurses not staged -- build contrib/ncurses first" >&2; exit 1; }

HACKDIR=/usr/lib/nethack
VARDIR=/var/games/nethack

# What both builds are configured with.  LINUX is NetHack's name for "a
# POSIX system with termios and the usual BSD extras", which is what
# substrate is; the build machine's compiler defines it by itself.
# The quotes are for make: this text goes into a hints file.
FEATURES='-DLINUX -DNOTPARMDECL -DDLB -DSECURE -DTIMED_DELAY -DDUMPLOG \
    -DSYSCF -DSYSCF_FILE=\"'"${HACKDIR}"'/sysconf\" \
    -DHACKDIR=\"'"${HACKDIR}"'\" -DVAR_PLAYGROUND=\"'"${VARDIR}"'\" \
    -DCOMPRESS=\"/usr/bin/gzip\" -DCOMPRESS_EXTENSION=\".gz\" \
    -DCONFIG_ERROR_SECURE=FALSE -DCURSES_GRAPHICS'

# write_hints FILE CC CFLAGS LFLAGS
# "#-PRE" marks what follows as the part setup.sh puts ahead of each
# Makefile; without the marker the hints are silently left out.
write_hints() {
    cat > "$1" <<EOF
# Written by contrib/nethack/build.sh.
#-PRE
PREFIX=/usr
HACKDIR=${HACKDIR}
SHELLDIR=/usr/bin
INSTDIR=${HACKDIR}
VARDIR=${VARDIR}
CC=$2
CFLAGS=$3 -I../include ${FEATURES}
LINK=\$(CC)
LFLAGS=$4
WINSRC = \$(WINTTYSRC) \$(WINCURSESSRC)
WINOBJ = \$(WINTTYOBJ) \$(WINCURSESOBJ)
WINLIB = \$(WINTTYLIB) \$(WINCURSESLIB)
WINTTYLIB=-lncurses -ltinfo
CHOWN=true
CHGRP=true
EOF
}

# --- host: the tools and what they generate ---------------------------------
echo "==> host tools (makedefs, lev_comp, dgn_comp, dlb)"
rm -rf "${HOST_DIR}" "${TARGET_DIR}"
cp -a "${TREE_DIR}" "${HOST_DIR}"
# -std=gnu17: the cross compiler's default, and what code with K&R
# declarations needs; a current host gcc defaults to C23, where `f()'
# means `f(void)'.
write_hints "${HOST_DIR}/sys/unix/hints/substrate-host" "cc -m32 -std=gnu17" "-O1 -g" "-m32"
( cd "${HOST_DIR}" && sh sys/unix/setup.sh sys/unix/hints/substrate-host )

HOSTMAKE="make YACC=bison\ -y LEX=flex"
( cd "${HOST_DIR}/util" && eval "${HOSTMAKE}" makedefs )
# The headers, in the order src/Makefile's own rules give them.
( cd "${HOST_DIR}/src" && eval "${HOSTMAKE}" \
      ../include/onames.h ../include/pm.h ../include/vis_tab.h ../include/date.h )
( cd "${HOST_DIR}/util" && eval "${HOSTMAKE}" lev_comp dgn_comp dlb )
# The data: the text databases, the dungeon description, every level.
( cd "${HOST_DIR}/dat" && eval "${HOSTMAKE}" \
      options data rumors quest.dat oracles engrave epitaph bogusmon \
      spec_levs quest_levs dungeon )
# And the archive the game opens, built as the top-level Makefile's `dlb'
# target builds it.  The member list is its DATDLB.
DATDLB=$(cd "${HOST_DIR}" && make -pn dlb 2>/dev/null | sed -n 's/^DATDLB = //p' | head -1)
DATHELP=$(cd "${HOST_DIR}" && make -pn dlb 2>/dev/null | sed -n 's/^DATHELP = //p' | head -1)
SPEC_LEVS=$(cd "${HOST_DIR}" && make -pn dlb 2>/dev/null | sed -n 's/^SPEC_LEVS = //p' | head -1)
QUEST_LEVS=$(cd "${HOST_DIR}" && make -pn dlb 2>/dev/null | sed -n 's/^QUEST_LEVS = //p' | head -1)
VARDATD=$(cd "${HOST_DIR}" && make -pn dlb 2>/dev/null | sed -n 's/^VARDATD = //p' | head -1)
[ -n "${DATDLB}" ] && [ -n "${SPEC_LEVS}" ] && [ -n "${VARDATD}" ] || {
    echo "build.sh: could not read the data file lists out of NetHack's Makefile" >&2; exit 1; }
( cd "${HOST_DIR}/dat" && LC_ALL=C && export LC_ALL &&
  eval "../util/dlb cf nhdat ${DATHELP} dungeon tribute ${SPEC_LEVS} ${QUEST_LEVS} ${VARDATD}" )
[ -s "${HOST_DIR}/dat/nhdat" ] || { echo "build.sh: nhdat was not built" >&2; exit 1; }

# --- target: the game ---------------------------------------------------------
echo "==> cross-compiling the game"
cp -a "${TREE_DIR}" "${TARGET_DIR}"
# -include sys/ioctl.h: sys/share/ioctl.c calls ioctl(TIOCGWINSZ) whenever
# <termios.h> defines the request, but includes the header that declares
# ioctl() only for compilers that predefine __linux__ or BSD.
write_hints "${TARGET_DIR}/sys/unix/hints/substrate" "i386-unknown-substrate-gcc" \
            "-march=i486 -mtune=i486 -O2 -g -fno-pie -include sys/ioctl.h" "-fno-pie"
( cd "${TARGET_DIR}" && sh sys/unix/setup.sh sys/unix/hints/substrate )

GENERATED="include/onames.h include/pm.h include/vis_tab.h include/date.h src/vis_tab.c"
for f in ${GENERATED}; do
    [ -s "${HOST_DIR}/${f}" ] || { echo "build.sh: host build did not make ${f}" >&2; exit 1; }
    cp "${HOST_DIR}/${f}" "${TARGET_DIR}/${f}"
done
# -o: these are finished.  Without it make would go and rebuild makedefs
# with the cross compiler, and then try to run it.
OLD="-o ../include/onames.h -o ../include/pm.h -o ../include/vis_tab.h -o ../include/date.h -o vis_tab.c"
# REGEXOBJ=pmatchregex.o: NetHack's own glob-style matcher for MENUCOLOR,
# MSGTYPE and autopickup exceptions, instead of POSIX regcomp().  Its POSIX
# back end defines functions called regex_compile, regex_match and
# regex_free, which substrate's <regex.h> declares, with other signatures,
# as libregex's native interface.
( cd "${TARGET_DIR}/src" && make -j"${JOBS}" ${OLD} REGEXOBJ=pmatchregex.o nethack )
# recover is one source file.  Compiled directly: util/Makefile reaches
# date.h through a recursive make into src/, where the -o names above
# mean nothing, and that make sets about rebuilding makedefs.
( cd "${TARGET_DIR}/util" &&
  eval "i386-unknown-substrate-gcc -march=i486 -mtune=i486 -O2 -g -fno-pie \
        -I../include ${FEATURES} -o recover recover.c" )

# --- install ------------------------------------------------------------------
# By hand: the top-level `install' wants to build and run the tools again,
# and writes straight to the real HACKDIR.
echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
D_HACK="${DESTDIR}${HACKDIR}"
D_VAR="${DESTDIR}${VARDIR}"
mkdir -p "${D_HACK}" "${D_VAR}/save" "${DESTDIR}/usr/bin" \
         "${DESTDIR}/usr/share/man/man6" "${DESTDIR}/usr/share/doc/nethack"

install -m 0755 "${TARGET_DIR}/src/nethack" "${D_HACK}/nethack"
install -m 0755 "${TARGET_DIR}/util/recover" "${D_HACK}/recover"
install -m 0644 "${HOST_DIR}/dat/nhdat" "${D_HACK}/nhdat"
install -m 0644 "${HOST_DIR}/dat/license" "${D_HACK}/license"
install -m 0644 "${HOST_DIR}/dat/symbols" "${D_HACK}/symbols"
install -m 0644 "${TREE_DIR}/sys/unix/sysconf" "${D_HACK}/sysconf"

# The wrapper, as the top-level Makefile makes it.
sed -e "s;/usr/games/lib/nethackdir;${HACKDIR};" \
    < "${TREE_DIR}/sys/unix/nethack.sh" > "${DESTDIR}/usr/bin/nethack"
chmod 0755 "${DESTDIR}/usr/bin/nethack"

# The files the game appends to.  build-rootfs.sh gives them, and the game,
# to group games.
for f in perm record logfile xlogfile; do : > "${D_VAR}/${f}"; done

install -m 0644 "${TREE_DIR}/doc/nethack.6" "${DESTDIR}/usr/share/man/man6/nethack.6"
install -m 0644 "${TREE_DIR}/doc/recover.6" "${DESTDIR}/usr/share/man/man6/recover.6"
install -m 0644 "${TREE_DIR}/doc/Guidebook.txt" "${DESTDIR}/usr/share/doc/nethack/Guidebook.txt"
install -m 0644 "${TREE_DIR}/dat/license" "${DESTDIR}/usr/share/doc/nethack/license"

for f in "${D_HACK}/nethack" "${D_HACK}/recover"; do
    printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
done

echo "==> Done.  NetHack staged under ${DESTDIR}"
