#!/bin/sh
#
# fetch.sh — download the pinned gdb tarball, verify sha256, extract it, and
# apply the substrate patch series.  Idempotent: re-running from a clean state
# produces a tree at $(pwd)/build/gdb-${VERSION}/ ready for configure.
#
# Usage:
#   ./fetch.sh                 # download, verify, extract, patch
#   ./fetch.sh --no-network    # use a tarball already present in build/
#

set -eu

VERSION="16.2"
TARBALL="gdb-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/gdb/${TARBALL}"
SHA256="4002cb7f23f45c37c790536a13a720942ce4be0402d929c9085e92f10d480119"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/gdb-${VERSION}"

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
tar xf "${TARBALL}"

echo "==> Applying patch series"
cd "${TREE_DIR}"
while IFS= read -r p; do
    [ -z "${p}" ] && continue
    case "${p}" in \#*) continue ;; esac
    echo "    - ${p}"
    patch -p1 --no-backup-if-mismatch < "${HERE}/patches/${p}"
done < "${HERE}/series"

echo "==> gdb-${VERSION} tree ready at ${TREE_DIR}"
echo "    sanity: $(./config.sub i386-unknown-substrate)"
