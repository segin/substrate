#!/bin/sh
#
# contrib/libICE/fetch.sh — fetch the libICE tarball, verify, extract,
# apply the substrate patch series.

set -eu

VERSION="1.1.2"
TARBALL="libICE-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/lib/${TARBALL}"
SHA256="974e4ed414225eb3c716985df9709f4da8d22a67a2890066bc6dfc89ad298625"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libICE-${VERSION}"

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

echo "==> libICE ${VERSION} ready at ${TREE_DIR}"
