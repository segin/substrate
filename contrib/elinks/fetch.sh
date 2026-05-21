#!/bin/sh
set -eu

VERSION="0.19.1"
TARBALL="elinks-${VERSION}.tar.xz"
URL="https://github.com/rkd77/elinks/releases/download/v${VERSION}/${TARBALL}"
SHA256="31960cd471246692b84008bffec89182f25818472f86ee1a41a09bf0dad09eeb"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/elinks-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
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
