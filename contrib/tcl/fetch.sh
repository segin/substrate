#!/bin/sh
#
# contrib/tcl/fetch.sh — fetch the Tcl 8.6 source tarball, verify, extract.
# Tcl is a CDE dependency (configure --with-tcl; dtinfo and parts of the
# build use it).  8.6 is the version CDE expects.

set -eu

VERSION="8.6.16"
TARBALL="tcl${VERSION}-src.tar.gz"
URL="https://downloads.sourceforge.net/project/tcl/Tcl/${VERSION}/${TARBALL}"
SHA256="91cb8fa61771c63c262efb553059b7c7ad6757afa5857af6265e4b0bdc2a14a5"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/tcl${VERSION}"

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

echo "==> Tcl ${VERSION} ready at ${TREE_DIR}"
