#!/bin/sh
#
# contrib/tk/fetch.sh — fetch the Tk 8.6 source tarball, verify, extract.
# Tk is Tcl's GUI toolkit; it must match contrib/tcl's version exactly.
# The SHA-256 is the one Arch's tk 8.6.16-1 package pins for the same file.

set -eu

VERSION="8.6.16"
TARBALL="tk${VERSION}-src.tar.gz"
URL="https://downloads.sourceforge.net/project/tcl/Tcl/${VERSION}/${TARBALL}"
SHA256="be9f94d3575d4b3099d84bc3c10de8994df2d7aa405208173c709cc404a7e5fe"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/tk${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"
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

echo "==> Tk ${VERSION} ready at ${TREE_DIR}"
