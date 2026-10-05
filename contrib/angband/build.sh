#!/bin/sh
#
# contrib/angband/build.sh — configure + build + install Angband 4.2.5 for
# substrate, with the curses and X11 front ends.  Produces:
#   /usr/bin/angband
#   /usr/share/angband/         game data, fonts, tiles, help, default prefs
#   /etc/angband/               the customisable game files and prefs
#   /usr/share/man ... none: upstream ships no manual page

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="4.2.5"
TREE_DIR="${HERE}/build/Angband-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-angband}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
SR="${STAGE1_PREFIX}/i386-unknown-substrate"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -f "${SR}/include/curses.h" ] || { echo "ncurses not staged -- build contrib/ncurses first" >&2; exit 1; }
[ -e "${SR}/lib/libX11.so" ]    || { echo "libX11 not staged -- build contrib/libX11 first" >&2; exit 1; }

cd "${TREE_DIR}"

# configure asks an ncursesw6-config script how to compile and link with
# ncurses.  The one the ncurses port installs answers for the TARGET's
# filesystem (-I/usr/include/ncursesw), which a cross compiler would read
# as the build machine's.  This one answers for the sysroot.
NCCONF="${HERE}/build/ncursesw6-config"
cat > "${NCCONF}" <<EOF
#!/bin/sh
# Written by contrib/angband/build.sh: ncurses, as the cross compiler sees it.
for a in "\$@"; do
    case "\$a" in
        --cflags)  echo "-I${SR}/include" ;;
        --libs)    echo "-L${SR}/lib -lncursesw -ltinfow" ;;
        --version) echo "6.5" ;;
    esac
done
EOF
chmod +x "${NCCONF}"
[ -e "${SR}/lib/libncursesw.so" ] || { echo "build.sh: no wide-character ncurses (libncursesw) in the sysroot" >&2; exit 1; }
[ -e "${SR}/lib/libtinfow.so" ]   || sed -i 's/ -ltinfow//' "${NCCONF}"

echo "==> configure"
# --with-private-dirs: savefiles, scores and user preferences live in
#   ~/.angband/Angband, so the game needs no setgid bit and no shared
#   writable directory.
# --disable-ncursestest: the test compiles and RUNS a curses program.
# SDL front ends and sound are left out; the X11 front end needs no more
#   than libX11.
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --bindir=/usr/bin \
    --sysconfdir=/etc \
    --with-private-dirs \
    --enable-curses \
    --enable-x11 \
    --disable-sdl --disable-sdl2 \
    --disable-sdl-mixer --disable-sdl2-mixer \
    --disable-ncursestest \
    --x-includes="${SR}/include" \
    --x-libraries="${SR}/lib" \
    NCURSES_CONFIG="${NCCONF}" \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g -fno-pie" \
    LDFLAGS="-fno-pie"

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

[ -x "${DESTDIR}/usr/bin/angband" ] || { echo "build.sh: angband was not installed" >&2; exit 1; }
printf '\100' | dd of="${DESTDIR}/usr/bin/angband" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null

echo "==> Done.  Angband staged under ${DESTDIR}"
