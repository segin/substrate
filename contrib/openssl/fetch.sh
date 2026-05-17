#!/bin/sh
# Download + verify + extract + patch OpenSSL.  Idempotent.
set -eu

VERSION="3.0.13"
TARBALL="openssl-${VERSION}.tar.gz"
URL="https://www.openssl.org/source/${TARBALL}"
# SHA-256 below cross-checked against openssl.org's published
# openssl-3.0.13.tar.gz.sha256 sidecar AND verified via PGP signature
# (openssl-3.0.13.tar.gz.asc) signed by OpenSSL OMC <openssl-omc@openssl.org>
# key EFC0A467D613CB83C7ED6D30D894E2CE8B3D79F5 on 2024-01-30.
SHA256="88525753f79d3bec27d2fa7c66aa0b92b3aa9498dafd93d7cfa4b3780cdae313"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/openssl-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    if [ "${1:-}" = "--no-network" ]; then
        echo "fetch.sh: ${TARBALL} not present and --no-network given" >&2
        exit 1
    fi
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

actual=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${actual}" = "${SHA256}" ] || {
    echo "fetch.sh: SHA-256 mismatch (expected ${SHA256}, got ${actual})" >&2
    exit 1
}

[ -d "${TREE_DIR}" ] && rm -rf "${TREE_DIR}"
tar -xzf "${TARBALL}"
[ -d "${TREE_DIR}" ] || { echo "fetch.sh: expected ${TREE_DIR}" >&2; exit 1; }

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
