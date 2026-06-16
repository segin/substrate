#!/bin/sh
#
# contrib/lmdb/build.sh — cross-build Symas LMDB for substrate.
#
# LMDB is two C files (mdb.c, midl.c) with a plain Makefile that builds a
# soname-less .so.  We compile directly with the cross toolchain and give
# the shared object a proper soname (liblmdb.so.0).  Robust mutexes are
# disabled (MDB_USE_ROBUST=0) — substrate's pthread does not implement the
# PTHREAD_MUTEX_ROBUST recovery protocol LMDB would otherwise rely on.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-lmdb)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="0.9.31"
SRC="${HERE}/build/lmdb-LMDB_${VERSION}/libraries/liblmdb"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-lmdb}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH

[ -d "${SRC}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

CC=i386-unknown-substrate-gcc
AR=i386-unknown-substrate-ar
RANLIB=i386-unknown-substrate-ranlib
# substrate's gcc driver has no -pthread convenience flag; use -D_REENTRANT
# at compile and -lpthread at link.  BYTE_ORDER (via <sys/types.h> ->
# <endian.h>), O_SYNC, and posix_memalign are all provided by substrate's
# libc/headers now, so no compat shim is needed.
# Force pthread-mutex locking (MDB_USE_POSIX_MUTEX).  substrate's headers
# define BSD, which otherwise steers mdb.c onto POSIX named semaphores
# (sem_open/wait/...), an API substrate does not provide; pthread mutexes it
# does (pthread_mutexattr_setpshared lands best-effort).  MDB_USE_ROBUST=0:
# substrate pthread has no PTHREAD_MUTEX_ROBUST recovery protocol.
CFLAGS="-march=i486 -mtune=i486 -O2 -g -D_REENTRANT -DMDB_USE_ROBUST=0 -DMDB_USE_POSIX_MUTEX=1"
SONAME=liblmdb.so.0
SOFILE=liblmdb.so.0.0.0

BUILD="${HERE}/build/build-stage-substrate"
rm -rf "${BUILD}"; mkdir -p "${BUILD}"; cd "${BUILD}"
cp "${SRC}/mdb.c" "${SRC}/midl.c" "${SRC}/midl.h" "${SRC}/lmdb.h" .

# Guard the BSD->POSIX_SEM branch so an explicit -DMDB_USE_POSIX_MUTEX wins
# (otherwise mdb.c would define both and #error on the count check).
sed -i 's/^#elif defined(__APPLE__) || defined (BSD) || defined(__FreeBSD_kernel__)$/#elif !defined(MDB_USE_POSIX_MUTEX) \&\& (defined(__APPLE__) || defined (BSD) || defined(__FreeBSD_kernel__))/' mdb.c

echo "==> compile (static + PIC objects)"
for f in mdb midl; do
    ${CC} ${CFLAGS} -c "${f}.c" -o "${f}.o"
    ${CC} ${CFLAGS} -fPIC -c "${f}.c" -o "${f}.lo"
done

echo "==> static archive"
${AR} rcs liblmdb.a mdb.o midl.o
${RANLIB} liblmdb.a

echo "==> shared object (soname ${SONAME})"
${CC} ${CFLAGS} -shared -Wl,-soname,${SONAME} -o "${SOFILE}" mdb.lo midl.lo -lpthread

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}/usr/lib/pkgconfig" "${DESTDIR}/usr/include" "${DESTDIR}/usr/bin"
cp lmdb.h "${DESTDIR}/usr/include/"
cp liblmdb.a "${DESTDIR}/usr/lib/"
cp "${SOFILE}" "${DESTDIR}/usr/lib/"
ln -sf "${SOFILE}" "${DESTDIR}/usr/lib/${SONAME}"
ln -sf "${SONAME}" "${DESTDIR}/usr/lib/liblmdb.so"

cat > "${DESTDIR}/usr/lib/pkgconfig/lmdb.pc" <<EOF
prefix=/usr
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: lmdb
Description: Lightning Memory-Mapped Database
Version: ${VERSION}
Libs: -L\${libdir} -llmdb
Libs.private: -lpthread
Cflags: -I\${includedir}
EOF

# Build the mdb_* CLI tools (handy, cheap).
for t in mdb_stat mdb_copy mdb_dump mdb_load; do
    ${CC} ${CFLAGS} -fno-pie -no-pie "${SRC}/${t}.c" liblmdb.a -lpthread -o "${t}" 2>/dev/null \
        && cp "${t}" "${DESTDIR}/usr/bin/" || echo "  (skipped ${t})"
done

printf '\100' | dd of="${DESTDIR}/usr/lib/${SOFILE}" bs=1 seek=7 count=1 conv=notrunc status=none
echo "  OSABI->substrate on 1 shared object"
echo "==> Done.  LMDB staged under ${DESTDIR}"
