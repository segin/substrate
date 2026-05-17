#!/bin/sh
#
# build.sh — configure + build + install curl for substrate.
#
# Depends on contrib/openssl being staged first (build.sh emits to
# ${SUBSTRATE_TOP}/dist-openssl).  Disables everything that needs
# a contrib package we don't have yet — see README.SUBSTRATE.md.
#
# Env:
#   STAGE1_PREFIX   default /opt/substrate
#   OPENSSL_STAGE   default ${SUBSTRATE_TOP}/dist-openssl
#   DESTDIR         default ${SUBSTRATE_TOP}/dist-curl
#   JOBS            default `nproc`

set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="8.7.1"
TREE_DIR="${HERE}/build/curl-${VERSION}"
BUILD_DIR="${HERE}/build/build-stage-substrate"

if [ -z "${SUBSTRATE_TOP:-}" ]; then
    p="${HERE}"
    while [ "${p}" != "/" ] && [ ! -f "${p}/AGENTS.md" ] && [ ! -f "${p}/CLAUDE.md" ]; do
        p=$(dirname "${p}")
    done
    SUBSTRATE_TOP="${p}"
fi
: "${STAGE1_PREFIX:=/opt/substrate}"
: "${OPENSSL_STAGE:=${SUBSTRATE_TOP}/dist-openssl}"
: "${DESTDIR:=${SUBSTRATE_TOP}/dist-curl}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

PATH="${STAGE1_PREFIX}/bin:${PATH}"
export PATH

[ -d "${TREE_DIR}" ] || { echo "build.sh: run ./fetch.sh first" >&2; exit 1; }
[ -d "${OPENSSL_STAGE}/usr/include/openssl" ] || {
    echo "build.sh: OpenSSL not staged at ${OPENSSL_STAGE}" >&2
    echo "         build contrib/openssl first" >&2
    exit 1
}

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Point pkg-config at the staged openssl tree so configure picks up
# the right -I/-L/-l flags.
export PKG_CONFIG_PATH="${OPENSSL_STAGE}/usr/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
# Curl's configure also honors CPPFLAGS/LDFLAGS — make sure libcrypto
# is visible at link time even when pkg-config metadata is incomplete.
export CPPFLAGS="-I${OPENSSL_STAGE}/usr/include ${CPPFLAGS:-}"
# Force <sys/select.h> early in every TU.  Substrate keeps the
# POSIX header layout strict — fd_set / FD_ZERO / select don't
# re-export from sys/types.h or sys/socket.h, but curl's own
# headers (multi.h, easy.h) use them unguarded.  curl's autoconf
# conftests don't include sys/select.h either, so without this
# every size-of-curl_socket_t probe fails.
CPPFLAGS="-include sys/select.h ${CPPFLAGS}"
export LDFLAGS="-L${OPENSSL_STAGE}/usr/lib -Wl,--copy-dt-needed-entries ${LDFLAGS:-}"

echo "==> configure"
"${TREE_DIR}/configure" \
    --host=i386-unknown-substrate \
    --prefix=/usr \
    --enable-shared --enable-static \
    --with-openssl="${OPENSSL_STAGE}/usr" \
    --without-libssh2 --without-libssh --without-rustls \
    --without-libpsl --without-libidn2 --without-brotli \
    --without-zstd --without-nghttp2 --without-nghttp3 \
    --without-ngtcp2 --without-quiche --without-msh3 \
    --without-libgsasl --without-librtmp \
    --disable-ldap --disable-ldaps \
    --disable-manual --disable-docs \
    --disable-ipv6 \
    --disable-threaded-resolver \
    --enable-http --enable-ftp --enable-file --enable-tftp \
    --enable-smtp --enable-pop3 --enable-imap \
    --enable-gopher --enable-dict --enable-telnet \
    --enable-cookies --enable-progress-meter

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> install into ${DESTDIR}"
rm -rf "${DESTDIR}"
mkdir -p "${DESTDIR}"
make install DESTDIR="${DESTDIR}"

echo "==> Done.  /usr/bin/curl + libcurl staged under ${DESTDIR}"
