#!/bin/sh
# contrib/gmp/fetch.sh — download + verify + extract GNU GMP.
# GMP is the arbitrary-precision arithmetic library; needed by kcalc
# (and broadly useful).
set -eu

VERSION="6.3.0"
TARBALL="gmp-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/gmp/${TARBALL}"
SHA256="a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/gmp-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi

got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA256 mismatch for ${TARBALL}" >&2; exit 1; }

rm -rf "${TREE_DIR}"
tar -xJf "${TARBALL}"

SERIES_FILE="${HERE}/series"
if [ -s "${SERIES_FILE}" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p || [ -n "${p}" ]; do
        case "${p}" in ''|\#*) continue;; esac
        echo "  apply ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${SERIES_FILE}"
fi

echo "==> Ready at ${TREE_DIR}"
