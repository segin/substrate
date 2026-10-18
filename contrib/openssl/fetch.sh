#!/bin/sh
# Download + verify + extract + patch OpenSSL.  Idempotent.
set -eu

VERSION="3.5.9"
TARBALL="openssl-${VERSION}.tar.gz"
URL="https://github.com/openssl/openssl/releases/download/openssl-${VERSION}/${TARBALL}"
# SHA-256 below cross-checked against the release's published
# openssl-3.5.9.tar.gz.sha256 sidecar.
SHA256="603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"

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
        curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"
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
