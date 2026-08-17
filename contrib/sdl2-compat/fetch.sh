#!/bin/sh
#
# contrib/sdl2-compat/fetch.sh — fetch the sdl2-compat tarball, verify, extract, apply patches.

set -eu

VERSION="2.32.70"
TARBALL="sdl2-compat-${VERSION}.tar.gz"
URL="https://github.com/libsdl-org/sdl2-compat/releases/download/release-${VERSION}/${TARBALL}"
SHA256="998fa62557eb46ffe7e5c3e2c123bc332f7df9d9f593b3ceed88ed1158428a44"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/sdl2-compat-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        [ -z "${p}" ] && continue
        case "${p}" in \#*) continue ;; esac
        if [ ! -f ".applied-${p}" ]; then
            echo "==> Applying ${p}"
            patch -p1 < "${HERE}/patches/${p}"
            touch ".applied-${p}"
        fi
    done < "${HERE}/series"
fi

echo "==> sdl2-compat ${VERSION} ready in ${TREE_DIR}"
