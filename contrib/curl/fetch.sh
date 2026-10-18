#!/bin/sh
set -eu

VERSION="8.23.0"
TARBALL="curl-${VERSION}.tar.xz"
URL="https://curl.se/download/${TARBALL}"
# SHA-256 below verified via PGP signature (curl-8.23.0.tar.xz.asc)
# from curl.se, signed by Daniel Stenberg <daniel@haxx.se>
# key 27EDEAF22F3ABCEB50DB9A125CC908FDB71E12C2.
SHA256="8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/curl-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi

got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA mismatch (got ${got})" >&2; exit 1; }

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
