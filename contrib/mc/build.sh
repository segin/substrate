#!/bin/sh
# contrib/mc/build.sh — cross-build GNU Midnight Commander for substrate.
#
# Configured as a linux host so autoconf behaves; CC is the substrate cross
# gcc, so the output is a substrate binary once byte 7 of each ELF header is
# stamped with ELFOSABI_SUBSTRATE (0x40).
#
# Depends on glib2 (mc's utility layer, and gmodule), the wide-character
# ncurses (the screen), and e2fsprogs (libext2fs/libe2p, for the chattr
# dialog) -- all staged in the cross sysroot first.
#
#   --with-screen=ncurses : the other choice is S-Lang, which is not ported.
#                           mc's ncurses backend draws shadows with the
#                           cchar_t API, which only the wide ncurses has.
#   --disable-vfs-sftp    : the SFTP VFS needs libssh2, which is not ported.
#   --sysconfdir=/etc     : mc's system-wide config lives in /etc/mc.
#   --disable-nls         : no translations are installed.
#
# Nothing in this port patches mc.  4.8.33 exposed one substrate-side gap,
# fixed there: struct stat stored st_mode as uint16_t, and
# lib/vfs/parse_ls_vga.c passes &st->st_mode to a function taking mode_t *.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"; PKG="mc"; VERSION="4.8.33"
TREE="${HERE}/build/mc-${VERSION}"; BS="${HERE}/build/bs"
if [ -z "${SUBSTRATE_TOP:-}" ]; then p="${HERE}"; while [ "$p" != "/" ] && [ ! -f "$p/CLAUDE.md" ] && [ ! -f "$p/AGENTS.md" ]; do p=$(dirname "$p"); done; SUBSTRATE_TOP="$p"; fi
: "${STAGE1_PREFIX:=/opt/substrate}"; SR="${STAGE1_PREFIX}/i386-unknown-substrate"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-${PKG}}"; : "${JOBS:=$(nproc 2>/dev/null||echo 4)}"
export PATH="${STAGE1_PREFIX}/bin:${PATH}"
[ -d "${TREE}" ] || { echo "run ./fetch.sh first" >&2; exit 1; }

export PKG_CONFIG_LIBDIR="${SR}/lib/pkgconfig"
for m in glib-2.0 gmodule-2.0 ext2fs e2p; do
    pkg-config --exists "$m" || { echo "${m}.pc not staged -- build its contrib port first" >&2; exit 1; }
done
grep -q 'NCURSES_WIDECHAR 1' "${SR}/include/curses.h" 2>/dev/null || \
    { echo "wide-character ncurses not staged -- build contrib/ncurses first" >&2; exit 1; }

# The staged .pc files carry prefix=/usr, so --cflags emits -I/usr/include/...
# and -I/usr/lib/glib-2.0/include.  An absolute -I is not rewritten by the
# compiler's sysroot: it names the BUILD HOST's directory.  On a machine with
# glib installed that silently compiles mc against the host's glibconfig.h
# (an x86_64 one); on a clean runner it fails with glib.h not found.  Rewrite
# each module's flags into the sysroot and hand them to configure, which
# PKG_CHECK_MODULES then uses instead of asking pkg-config.
sysroot_cflags() {
    pkg-config --cflags "$@" | sed -e "s|-I/usr/include|-I${SR}/include|g" \
                                   -e "s|-I/usr/lib/|-I${SR}/lib/|g"
}
GLIB_CFLAGS=$(sysroot_cflags glib-2.0)
GMODULE_CFLAGS=$(sysroot_cflags gmodule-2.0)
EXT2FS_CFLAGS=$(sysroot_cflags ext2fs)
E2P_CFLAGS=$(sysroot_cflags e2p)

# LIBS=-ldl: the staged libgmodule-2.0.so calls dlopen/dlsym/dlclose/dlerror
# but records no DT_NEEDED on libdl -- glib2 was configured as a linux host,
# where those live in libc -- so the executable has to bring libdl in itself.
rm -rf "${BS}"; mkdir -p "${BS}"; cd "${BS}"
"${TREE}/configure" --host=i386-unknown-linux-gnu --prefix=/usr --sysconfdir=/etc \
  --with-screen=ncurses --disable-vfs-sftp --disable-nls \
  CC=i386-unknown-substrate-gcc CFLAGS="-march=i486 -mtune=i486 -O2 -g" \
  LDFLAGS="-L${SR}/lib" LIBS="-ldl" \
  GLIB_CFLAGS="${GLIB_CFLAGS}" GMODULE_CFLAGS="${GMODULE_CFLAGS}" \
  EXT2FS_CFLAGS="${EXT2FS_CFLAGS}" E2P_CFLAGS="${E2P_CFLAGS}"

# Belt and braces: fail here, not hours later on a clean runner, if any
# configure-substituted flag still points at the host's /usr.
if grep -nE "^[A-Z0-9_]+_CFLAGS='[^']*-I/usr/" config.log; then
    echo "mc: a host include path leaked into the flags above" >&2; exit 1
fi

make -j"${JOBS}"
rm -rf "${DESTDIR}"; make install DESTDIR="${DESTDIR}"
# Stamp every ELF program: /usr/bin/mc and the helpers in /usr/libexec/mc.
find "${DESTDIR}/usr" -type f | while IFS= read -r f; do
    [ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" = '177ELF' ] || continue
    printf '\100' | dd of="$f" bs=1 seek=7 count=1 conv=notrunc 2>/dev/null
done
echo "==> ${PKG} staged under ${DESTDIR}"
