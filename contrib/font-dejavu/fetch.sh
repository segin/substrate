#!/bin/sh
# contrib/font-dejavu/fetch.sh — DejaVu 2.37 TrueType fonts (prebuilt).
# The SHA-256 matches FreeBSD's x11-fonts/dejavu distinfo for the same
# GitHub release asset.
set -eu

VERSION="2.37"
TARBALL="dejavu-fonts-ttf-${VERSION}.tar.bz2"
URL="https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/${TARBALL}"
SHA256="fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7"

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

[ -d "dejavu-fonts-ttf-${VERSION}" ] || tar xf "${TARBALL}"
echo "==> DejaVu ${VERSION} ready"
