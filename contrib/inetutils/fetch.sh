#!/bin/sh
#
# fetch.sh — download the pinned inetutils tarball, verify sha256,
# extract it, and apply the substrate patch series.  Idempotent: re-
# running from a clean state produces a tree at
# $(pwd)/build/inetutils-${VERSION}/ ready for configure.
#
# Usage:
#   ./fetch.sh                 # download, verify, extract, patch
#   ./fetch.sh --no-network    # use a tarball already present in build/

set -eu

VERSION="2.5"
TARBALL="inetutils-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/inetutils/${TARBALL}"
# SHA-256 below verified against PGP signature inetutils-2.5.tar.xz.sig
# from ftp.gnu.org, signed by Simon Josefsson <simon@josefsson.org>
# (key A3CC9C870B9D310ABAD4CF2F51722B08FE4745A2).
SHA256="87697d60a31e10b5cb86a9f0651e1ec7bee98320d048c0739431aac3d5764fb6"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/inetutils-${VERSION}"

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
    elif command -v wget >/dev/null 2>&1; then
        wget -O "${TARBALL}" "${URL}"
    else
        echo "fetch.sh: neither curl nor wget found" >&2
        exit 1
    fi
fi

echo "==> Verifying SHA-256"
actual=$(sha256sum "${TARBALL}" | awk '{print $1}')
if [ "${actual}" != "${SHA256}" ]; then
    echo "fetch.sh: SHA-256 mismatch for ${TARBALL}" >&2
    echo "  expected ${SHA256}" >&2
    echo "  actual   ${actual}" >&2
    exit 1
fi

if [ -d "${TREE_DIR}" ]; then
    echo "==> Removing stale ${TREE_DIR}"
    rm -rf "${TREE_DIR}"
fi

echo "==> Extracting ${TARBALL}"
tar -xJf "${TARBALL}"
[ -d "${TREE_DIR}" ] || { echo "fetch.sh: expected ${TREE_DIR}" >&2; exit 1; }

# Apply patches in series order.
SERIES_FILE="${HERE}/series"
if [ -s "${SERIES_FILE}" ]; then
    echo "==> Applying patches from ${SERIES_FILE}"
    cd "${TREE_DIR}"
    while IFS= read -r patch || [ -n "${patch}" ]; do
        case "${patch}" in ''|\#*) continue;; esac
        echo "  apply ${patch}"
        patch -p1 < "${HERE}/patches/${patch}"
    done < "${SERIES_FILE}"
else
    echo "==> No patches in series (or empty); skipping patch step"
fi

echo "==> Ready at ${TREE_DIR}"
