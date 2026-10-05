#!/bin/sh
#
# contrib/nethack/fetch.sh — fetch the NetHack 3.6.7 tarball, verify,
# extract, apply patches.

set -eu

VERSION="3.6.7"
TARBALL="nethack-367-src.tgz"
URL="https://www.nethack.org/download/${VERSION}/${TARBALL}"
SHA256="98cf67df6debf9668a61745aa84c09bcab362e5d33f5b944ec5155d44d2aacb2"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/NetHack-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    if [ "${1:-}" = "--no-network" ]; then
        echo "fetch.sh: ${TARBALL} not present and --no-network given" >&2
        exit 1
    fi
    echo "==> Fetching ${URL}"
    curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"
fi

echo "==> Verifying sha256"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ -d "${TREE_DIR}" ]; then
    echo "==> Removing existing tree ${TREE_DIR}"
    rm -rf "${TREE_DIR}"
fi

echo "==> Extracting"
tar xf "${TARBALL}"

if [ -f "${HERE}/series" ]; then
    echo "==> Applying patch series"
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        [ -z "${p}" ] && continue
        case "${p}" in \#*) continue ;; esac
        echo "    - ${p}"
        patch -p1 --no-backup-if-mismatch < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> NetHack ${VERSION} tree ready at ${TREE_DIR}"
