#!/bin/sh
#
# fetch.sh — download mpg123, verify sha256, extract, apply patches.
# Idempotent: re-running from a clean state produces a tree at
# $(pwd)/build/mpg123-${VERSION}/ ready for configure.
#
# Usage:
#   ./fetch.sh                 # download, verify, extract, patch
#   ./fetch.sh --no-network    # use a tarball already present in build/
#

set -eu

VERSION="1.32.10"
TARBALL="mpg123-${VERSION}.tar.bz2"
URL="https://www.mpg123.de/download/${TARBALL}"
SHA256="87b2c17fe0c979d3ef38eeceff6362b35b28ac8589fbf1854b5be75c9ab6557c"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/mpg123-${VERSION}"

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

echo "==> Verifying sha256"
ACTUAL=$(sha256sum "${TARBALL}" | awk '{print $1}')
if [ "${ACTUAL}" != "${SHA256}" ]; then
    echo "fetch.sh: sha256 mismatch on ${TARBALL}" >&2
    echo "  expected: ${SHA256}" >&2
    echo "  got:      ${ACTUAL}" >&2
    exit 1
fi

if [ -d "${TREE_DIR}" ]; then
    echo "==> Removing existing tree ${TREE_DIR}"
    rm -rf "${TREE_DIR}"
fi

echo "==> Extracting"
tar xjf "${TARBALL}"

echo "==> Applying patch series"
cd "${TREE_DIR}"
if [ -s "${HERE}/series" ]; then
    while read -r p; do
        case "${p}" in ""|\#*) continue ;; esac
        echo "    - ${p}"
        patch -p1 --no-backup-if-mismatch -i "${HERE}/patches/${p}" >/dev/null
    done < "${HERE}/series"
else
    echo "    (series file empty — no patches to apply)"
fi

echo "==> Done.  Tree is at ${TREE_DIR}"
echo ""
echo "Next step:"
echo "  ${HERE}/build.sh"
