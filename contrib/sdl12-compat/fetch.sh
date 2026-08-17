#!/bin/sh
#
# contrib/sdl12-compat/fetch.sh — fetch the sdl12-compat tarball, verify, extract, apply patches.

set -eu

VERSION="1.2.76"
TARBALL="sdl12-compat-${VERSION}.tar.gz"
URL="https://github.com/libsdl-org/sdl12-compat/releases/download/release-${VERSION}/${TARBALL}"
SHA256="a68477009c24bc6e876326b1e8dd0bedec2b0c37acbddbddf90acba48fba4b38"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/sdl12-compat-${VERSION}"

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

echo "==> sdl12-compat ${VERSION} ready in ${TREE_DIR}"
