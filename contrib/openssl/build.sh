#!/bin/sh
#
# build.sh — Configure + build + install OpenSSL for substrate.
#
# Uses the linux-generic32 Configure target with no-asm so we don't
# pull in i386 ASM bits that may not assemble against substrate-
# binutils.  Disables tests/engine/quic to keep the build small.
#
# Env:
#   STAGE1_PREFIX   substrate toolchain prefix (default /opt/substrate)
#   DESTDIR         staging dir (default ${SUBSTRATE_TOP}/dist-overlay/dist-openssl)
#   JOBS            parallel jobs (default `nproc`)

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="3.0.13"
TREE_DIR="${HERE}/build/openssl-${VERSION}"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-openssl}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }

cd "${TREE_DIR}"

# OpenSSL builds .so files with -Wl,-z,defs (no undefined symbols
# allowed at link time).  Substrate-gcc's default for `-shared` does
# NOT auto-add -lc, so symbols like memset go unresolved.  Force the
# default-libs onto every link line.
# -lpthread for the same reason: OpenSSL's pthread threading backend
# references pthread_rwlock_*, pthread_once and the TSD calls, and -z defs
# rejects the .so if they are left undefined.
export LDFLAGS="${LDFLAGS:-} -Wl,--no-as-needed -lc -lpthread"

echo "==> Configure"
# -march=i486 -mtune=i486 baseline so we don't accidentally pull in
# SSE/SSE2 codegen via the default arch on the substrate cross gcc.
./Configure linux-generic32 \
    --prefix=/usr \
    --openssldir=/etc/ssl \
    --cross-compile-prefix=i386-unknown-substrate- \
    -march=i486 -mtune=i486 \
    no-asm \
    no-engine \
    no-tests \
    shared

# The substrate cross gcc does not accept -pthread: it is a target-specific
# driver option gcc only defines for OSes it knows, and OpenSSL's linux
# templates add it to CNF_CFLAGS/CNF_CXXFLAGS as soon as threads are enabled:
#
#     i386-unknown-substrate-gcc: error: unrecognized command-line option
#     '-pthread'; did you mean '-fpthread'?
#
# It means "-D_REENTRANT plus -lpthread", and LDFLAGS above already passes
# -lpthread, so rewrite the two generated flag lines instead of teaching the
# compiler an option (which would mean patching and rebuilding gcc).
# Compile lines want the macro; link lines want the library.
sed -i -e 's/^CNF_CFLAGS=-pthread$/CNF_CFLAGS=-D_REENTRANT/' \
       -e 's/^CNF_CXXFLAGS=\(.*\) -pthread$/CNF_CXXFLAGS=\1/' \
       -e 's/^CNF_EX_LIBS=\(.*\) -pthread$/CNF_EX_LIBS=\1 -lpthread/' \
       -e 's/^LDFLAGS=\(.*\) -pthread$/LDFLAGS=\1 -lpthread/' Makefile
if grep -nE '(^| )-pthread( |$)' Makefile; then
    echo "build.sh: -pthread survived in the generated Makefile" >&2
    exit 1
fi

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install_sw install_ssldirs DESTDIR="${DESTDIR}"

echo "==> Done.  Staged libs and headers under ${DESTDIR}/usr"
