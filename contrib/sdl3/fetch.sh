#!/bin/sh
#
# contrib/sdl3/fetch.sh — fetch the SDL3 tarball, verify, extract, apply patches.

set -eu

VERSION="3.4.14"
TARBALL="SDL3-${VERSION}.tar.gz"
URL="https://github.com/libsdl-org/SDL/releases/download/release-${VERSION}/${TARBALL}"
SHA256="30d4aa2b3037718142b32dffd4e72f917ebb6cc5227150e7bb9c45efb2153aeb"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/SDL3-${VERSION}"

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

echo "==> SDL3 ${VERSION} ready in ${TREE_DIR}"
