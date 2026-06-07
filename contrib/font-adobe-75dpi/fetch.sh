#!/bin/sh
# contrib/font-adobe-75dpi/fetch.sh — substrate's port of X.Org's
# Adobe 75dpi BDF bitmap fonts (Helvetica, Times, Courier, New
# Century Schoolbook, Symbol).  Source-only fetch; the actual install
# is done by build.sh.  The 1.0.4 release ships ISO10646-1 (Unicode)
# BDFs, so the fonts work directly in a UTF-8 fontset.

set -eu

VERSION="1.0.4"
TARBALL="font-adobe-75dpi-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/font/${TARBALL}"
SHA256="1281a62dbeded169e495cae1a5b487e1f336f2b4d971d92911c59c103999b911"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/font-adobe-75dpi-${VERSION}"

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

echo "==> font-adobe-75dpi ${VERSION} ready at ${TREE_DIR}"
