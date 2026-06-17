#!/bin/sh
#
# contrib/xprop/fetch.sh — fetch the xprop tarball, verify, extract,
# apply the substrate patch series.

set -eu

VERSION="1.2.8"
TARBALL="xprop-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/app/${TARBALL}"
SHA256="d689e2adb7ef7b439f6469b51cda8a7daefc83243854c2a3b8f84d0f029d67ee"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/xprop-${VERSION}"

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
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then
            echo "==> Patch ${p} already applied"; continue
        fi
        echo "==> Applying ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> xprop ${VERSION} ready at ${TREE_DIR}"
