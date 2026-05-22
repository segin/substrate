#!/bin/sh
#
# contrib/libSM/fetch.sh — fetch the libSM tarball, verify, extract,
# apply the substrate patch series.

set -eu

VERSION="1.2.6"
TARBALL="libSM-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/lib/${TARBALL}"
SHA256="be7c0abdb15cbfd29ac62573c1c82e877f9d4047ad15321e7ea97d1e43d835be"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libSM-${VERSION}"

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
        case "$p" in
            ''|'#'*) continue ;;
        esac
        if [ ! -f "${HERE}/patches/${p}" ]; then
            echo "fetch.sh: missing patch ${p}" >&2
            exit 1
        fi
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> Patch ${p} already applied"
            continue
        fi
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> libSM ${VERSION} ready at ${TREE_DIR}"
