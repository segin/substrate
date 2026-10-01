#!/bin/sh
# contrib/font-unifont/fetch.sh — GNU Unifont 18.0.01 release tarball,
# whose font/precompiled/ holds the ready-built OpenType fonts.  The SHA-256
# matches the hash nixpkgs' unifont package pins for the same file.
set -eu

VERSION="18.0.01"
TARBALL="unifont-${VERSION}.tar.gz"
URL="https://ftp.gnu.org/gnu/unifont/unifont-${VERSION}/${TARBALL}"
SHA256="eab60847aac34c8768765cecc7821faf50de2636187b452b9b5fa50a12b00bc3"

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

[ -d "unifont-${VERSION}" ] || tar xf "${TARBALL}"
echo "==> Unifont ${VERSION} ready"
