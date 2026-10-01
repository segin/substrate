#!/bin/sh
# contrib/font-liberation/fetch.sh — Liberation 2.1.5 TrueType fonts
# (prebuilt).  The SHA-256 matches FreeBSD's x11-fonts/liberation-fonts-ttf
# distinfo for the same GitHub release asset.
set -eu

VERSION="2.1.5"
TARBALL="liberation-fonts-ttf-${VERSION}.tar.gz"
URL="https://github.com/liberationfonts/liberation-fonts/files/7261482/${TARBALL}"
SHA256="7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
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

[ -d "liberation-fonts-ttf-${VERSION}" ] || tar xf "${TARBALL}"
echo "==> Liberation ${VERSION} ready"
