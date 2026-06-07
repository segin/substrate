#!/bin/sh
#
# contrib/libXScrnSaver/fetch.sh — fetch the libXScrnSaver tarball, verify,
# extract.  The X Screen Saver client extension library (libXss); CDE's
# dtsession queries and drives the screensaver through it
# (XScreenSaverQueryInfo/Register/SelectInput/...).  No substrate patches are
# needed; the saver protocol headers (saver.h / saverproto.h) already ship in
# contrib/xorgproto.

set -eu

VERSION="1.2.4"
TARBALL="libXScrnSaver-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/lib/${TARBALL}"
SHA256="75cd2859f38e207a090cac980d76bc71e9da99d48d09703584e00585abc920fe"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libXScrnSaver-${VERSION}"

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

echo "==> libXScrnSaver ${VERSION} ready at ${TREE_DIR}"
