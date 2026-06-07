#!/bin/sh
#
# contrib/libjpeg/fetch.sh — fetch the IJG libjpeg tarball, verify, extract.
# Hard dependency of CDE (image/dticon code links -ljpeg).  IJG v9 is plain
# autotools with no SIMD/NASM requirement, so it cross-builds cleanly.

set -eu

VERSION="9f"
TARBALL="jpegsrc.v${VERSION}.tar.gz"
URL="https://www.ijg.org/files/${TARBALL}"
SHA256="04705c110cb2469caa79fb71fba3d7bf834914706e9641a4589485c1f832565b"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/jpeg-${VERSION}"

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

echo "==> libjpeg ${VERSION} ready at ${TREE_DIR}"
