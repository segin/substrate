#!/bin/sh
# contrib/libevent/build.sh — cross-build libevent for substrate.
#
# The event loop library tmux is written on.  Built without OpenSSL (tmux
# does not use bufferevent_ssl, and nothing else here needs it), without
# the samples and without the regression suite, which wants a host run.
#
# Of libevent's backends substrate has poll(2) and select(2); configure
# finds no epoll, kqueue, /dev/poll or event ports and leaves them out.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.1.13-stable"; TREE_DIR="${HERE}/build/libevent-${VERSION}"; BUILD_DIR="${HERE}/build/build-substrate"
if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"; while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do p=$(dirname "${p}"); done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"; : "${DESTDIR:=${SUBSTRATE_TOP}/dist-overlay/dist-libevent}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
PATH="${STAGE1_PREFIX}/bin:${PATH}"; export PATH
. "${HERE}/../substrate-autotools.sh"
[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
substrate_config_sub_fix "${TREE_DIR}"
substrate_libtool_fix "${TREE_DIR}/configure"
export LDFLAGS="-Wl,--copy-dt-needed-entries"
rm -rf "${BUILD_DIR}"; mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr --libdir=/usr/lib --includedir=/usr/include \
    --enable-shared --enable-static \
    --disable-openssl --disable-samples --disable-libevent-regress \
    --disable-debug-mode --disable-doxygen-html \
    CC=i386-unknown-substrate-gcc \
    AR=i386-unknown-substrate-ar RANLIB=i386-unknown-substrate-ranlib \
    CFLAGS="-march=i486 -mtune=i486 -O2 -g"
make -j"${JOBS}"
rm -rf "${DESTDIR}"; mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"
# event_rpcgen.py is a build-host code generator, not something to ship.
rm -f "${DESTDIR}/usr/bin/event_rpcgen.py"
rmdir "${DESTDIR}/usr/bin" 2>/dev/null || true
substrate_so_finalize "${DESTDIR}"
echo "==> libevent staged at ${DESTDIR}"
