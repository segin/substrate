#!/bin/sh
# fetch.sh — download + verify + extract + patch GNU gzip.  Idempotent.
set -eu

VERSION="1.13"
TARBALL="gzip-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/gzip/${TARBALL}"
# SHA-256 verified via PGP signature gzip-1.13.tar.xz.sig from
# ftp.gnu.org, signed by Jim Meyering <jim@meyering.net>
# (key 155D3FC500C834486D1EEA677FD9FCCB000BEEEE) on 2023-08-19.
SHA256="7454eb6935db17c6655576c2e1b0fabefd38b4d0936e0f87f48cd062ce91a057"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/gzip-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"
    else wget -O "${TARBALL}" "${URL}"; fi
fi

got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA mismatch (got ${got})" >&2; exit 1; }

rm -rf "${TREE_DIR}"
echo "==> Extracting ${TARBALL}"
tar -xJf "${TARBALL}"

SERIES_FILE="${HERE}/series"
if [ -s "${SERIES_FILE}" ]; then
    echo "==> Applying patches from ${SERIES_FILE}"
    cd "${TREE_DIR}"
    while IFS= read -r p || [ -n "${p}" ]; do
        case "${p}" in ''|\#*) continue;; esac
        echo "  apply ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${SERIES_FILE}"
fi

echo "==> Ready at ${TREE_DIR}"
