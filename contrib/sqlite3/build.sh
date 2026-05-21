#!/bin/sh
#
# contrib/sqlite3/build.sh — cross-compile SQLite for substrate.
#
# SQLite ships as a single-file amalgamation, so there is no need to
# run its (autosetup) configure under cross-compilation: build.sh
# compiles sqlite3.c directly into a static libsqlite3.a, links the
# sqlite3 CLI, and writes a pkg-config file by hand.
#
# Produces: /usr/bin/sqlite3, /usr/lib/libsqlite3.a,
#           /usr/include/{sqlite3.h,sqlite3ext.h},
#           /usr/lib/pkgconfig/sqlite3.pc
#
# Env: STAGE1_PREFIX (/opt/substrate), DESTDIR, JOBS.

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="3530100"
SQLITE_VERSION="3.53.1"
TREE_DIR="${HERE}/build/sqlite-autoconf-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-sqlite3}"

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

echo "==> link sqlite3 CLI"
${CC} ${CFLAGS} ${SQLITE_DEFS} -fno-pie -o sqlite3 shell.c sqlite3.o \
    -lm -lpthread

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/usr/bin" "${DESTDIR}/usr/lib/pkgconfig" "${DESTDIR}/usr/include"
i386-unknown-substrate-strip sqlite3
install -m755 sqlite3 "${DESTDIR}/usr/bin/sqlite3"
install -m644 libsqlite3.a "${DESTDIR}/usr/lib/libsqlite3.a"
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

echo "==> Done.  sqlite3 + libsqlite3.a staged under ${DESTDIR}"
