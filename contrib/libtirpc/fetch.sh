#!/bin/sh
#
# contrib/libtirpc/fetch.sh — fetch libtirpc, verify, extract.  libtirpc is
# the transport-independent Sun RPC library (the implementation that used to
# live in glibc).  CDE's ToolTalk needs Sun RPC (<rpc/rpc.h>, XDR, svc_*);
# substrate's libc has none, so this provides it.

set -eu

VERSION="1.3.5"
TARBALL="libtirpc-${VERSION}.tar.bz2"
URL="https://downloads.sourceforge.net/project/libtirpc/libtirpc/${VERSION}/${TARBALL}"
SHA256="9b31370e5a38d3391bf37edfa22498e28fe2142467ae6be7a17c9068ec0bf12f"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libtirpc-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

echo "==> libtirpc ${VERSION} ready at ${TREE_DIR}"
