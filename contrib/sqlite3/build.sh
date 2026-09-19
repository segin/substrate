#!/bin/sh
#
# contrib/sqlite3/build.sh — cross-compile SQLite for substrate.
#
# SQLite ships as a single-file amalgamation, so there is no need to
# run its (autosetup) configure under cross-compilation: build.sh
# compiles sqlite3.c directly -- twice, once plain for libsqlite3.a and
# once -fPIC for libsqlite3.so -- links the sqlite3 CLI against the
# shared library, and writes a pkg-config file by hand.
#
# The soname is libsqlite3.so.0 and the file is libsqlite3.so.0.8.6, the
# names upstream's libtool build produces, so anything linked elsewhere
# against a distribution SQLite finds what it expects.
#
# Produces: /usr/bin/sqlite3,
#           /usr/lib/libsqlite3.{a,so,so.0,so.0.8.6},
#           /usr/include/{sqlite3.h,sqlite3ext.h},
#           /usr/lib/pkgconfig/sqlite3.pc
#
# Env: STAGE1_PREFIX (/opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="3530100"
SQLITE_VERSION="3.53.1"
# Upstream's libtool -version-info for the shared library.
SO_VERSION="0.8.6"
TREE_DIR="${HERE}/build/sqlite-autoconf-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-sqlite3}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

CC="i386-unknown-substrate-gcc"
AR="i386-unknown-substrate-ar"
CFLAGS="-march=i586 -mtune=i686 -O2 -fno-pie"
# Full thread safety (SQLITE_THREADSAFE=1).  substrate's
# pthread_mutex_t is a bare futex word with no room for an owner id
# or recursion count, so SQLITE_HOMEGROWN_RECURSIVE_MUTEX has SQLite
# track recursion itself over plain (non-recursive) pthread mutexes.
SQLITE_DEFS="-DSQLITE_THREADSAFE=1 -DSQLITE_HOMEGROWN_RECURSIVE_MUTEX \
    -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_RTREE \
    -DSQLITE_OMIT_LOAD_EXTENSION -DHAVE_USLEEP"

echo "==> compile sqlite3.c"
${CC} ${CFLAGS} ${SQLITE_DEFS} -c sqlite3.c -o sqlite3.o
${AR} rcs libsqlite3.a sqlite3.o

# Second pass for the shared library.  CFLAGS carries -fno-pie for the static
# objects and the CLI; -fno-pie and -fPIC in one compile contradict each other
# and gcc silently honours the last, so drop it here rather than rely on order.
PIC_CFLAGS=$(printf '%s' "${CFLAGS}" | sed 's/-fno-pie//')
echo "==> compile sqlite3.c -fPIC"
${CC} ${PIC_CFLAGS} -fPIC ${SQLITE_DEFS} -c sqlite3.c -o sqlite3.pic.o

echo "==> link libsqlite3.so.${SO_VERSION}"
${CC} ${PIC_CFLAGS} -fPIC -shared -Wl,-soname,libsqlite3.so.0 \
    -o "libsqlite3.so.${SO_VERSION}" sqlite3.pic.o -lm -lpthread
ln -sf "libsqlite3.so.${SO_VERSION}" libsqlite3.so.0
ln -sf libsqlite3.so.0 libsqlite3.so

# The CLI links the shared library: -lsqlite3 finds libsqlite3.so here, so the
# binary carries DT_NEEDED libsqlite3.so.0 rather than a second copy of the
# 1.4 MB amalgamation.
echo "==> link sqlite3 CLI"
${CC} ${CFLAGS} ${SQLITE_DEFS} -fno-pie -o sqlite3 shell.c \
    -L. -lsqlite3 -lm -lpthread

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/usr/bin" "${DESTDIR}/usr/lib/pkgconfig" "${DESTDIR}/usr/include"
i386-unknown-substrate-strip sqlite3
install -m755 sqlite3 "${DESTDIR}/usr/bin/sqlite3"
install -m644 libsqlite3.a "${DESTDIR}/usr/lib/libsqlite3.a"
install -m755 "libsqlite3.so.${SO_VERSION}" "${DESTDIR}/usr/lib/libsqlite3.so.${SO_VERSION}"
ln -sf "libsqlite3.so.${SO_VERSION}" "${DESTDIR}/usr/lib/libsqlite3.so.0"
# The link name ld resolves -lsqlite3 against.  Without it a consumer falls
# through to libsqlite3.a with no diagnostic at all.
ln -sf libsqlite3.so.0 "${DESTDIR}/usr/lib/libsqlite3.so"
# Brand the shared object ELFOSABI_SUBSTRATE (byte 7 = 0x40).
. "${HERE}/../substrate-autotools.sh"
substrate_so_finalize "${DESTDIR}"
install -m644 sqlite3.h sqlite3ext.h "${DESTDIR}/usr/include/"

cat > "${DESTDIR}/usr/lib/pkgconfig/sqlite3.pc" <<EOF
prefix=/usr
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: SQLite
Description: SQL database engine
Version: ${SQLITE_VERSION}
Libs: -L\${libdir} -lsqlite3
Libs.private: -lm -lpthread
Cflags: -I\${includedir}
EOF

echo "==> Done.  sqlite3 + libsqlite3.{a,so.'${SO_VERSION}'} staged under ${DESTDIR}"
