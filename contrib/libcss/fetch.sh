#!/bin/sh
set -eu

VERSION="0.9.2"
LIB="libcss"
TARBALL="${LIB}-${VERSION}-src.tar.gz"
URL="https://download.netsurf-browser.org/libs/releases/${TARBALL}"
SHA256="2df215bbec34d51d60c1a04b01b2df4d5d18f510f1f3a7af4b80cddb5671154e"

BS_VERSION="1.10"
BS_TARBALL="buildsystem-${BS_VERSION}.tar.gz"
BS_URL="https://download.netsurf-browser.org/libs/releases/${BS_TARBALL}"
BS_SHA256="3d3e39d569e44677c4b179129bde614c65798e2b3e6253160239d1fd6eae4d79"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/${LIB}-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

fetch_verify() {
    tb="$1"; url="$2"; sha="$3"
    if [ ! -f "${tb}" ]; then
        [ "${NO_NETWORK:-0}" = "1" ] && { echo "fetch.sh: ${tb} missing" >&2; exit 1; }
        echo "==> Fetching ${url}"
        if command -v curl >/dev/null 2>&1; then curl -fSL -o "${tb}" "${url}"; else wget -O "${tb}" "${url}"; fi
    fi
    got=$(sha256sum "${tb}" | awk '{print $1}')
    [ "${got}" = "${sha}" ] || { echo "fetch.sh: SHA mismatch on ${tb} (got ${got})" >&2; exit 1; }
}

[ "${1:-}" = "--no-network" ] && NO_NETWORK=1
fetch_verify "${TARBALL}" "${URL}" "${SHA256}"
fetch_verify "${BS_TARBALL}" "${BS_URL}" "${BS_SHA256}"

rm -rf "${TREE_DIR}" "${BUILD_DIR}/buildsystem-${BS_VERSION}"
tar -xzf "${TARBALL}"
tar -xzf "${BS_TARBALL}"

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
