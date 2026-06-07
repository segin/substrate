#!/bin/sh
#
# contrib/lmdb/fetch.sh — fetch the Symas LMDB tarball, verify, extract.
# Optional CDE dependency (AC_CHECK_LIB(lmdb, mdb_version)); a tiny
# memory-mapped key/value store, two C files with a hand-written Makefile.

set -eu

VERSION="0.9.31"
TARBALL="lmdb-${VERSION}.tar.gz"
URL="https://github.com/LMDB/lmdb/archive/refs/tags/LMDB_${VERSION}.tar.gz"
SHA256="dd70a8c67807b3b8532b3e987b0a4e998962ecc28643e1af5ec77696b081c9b0"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/lmdb-LMDB_${VERSION}"

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

echo "==> LMDB ${VERSION} ready at ${TREE_DIR}"
