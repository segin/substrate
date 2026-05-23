#!/bin/sh
# contrib/xkeyboard-config/fetch.sh

set -eu

# 2.36 is the last release that still ships the data tree as plain
# text (later versions also do, but the build system is meson-only
# and we don't have a cross-meson chain wired up yet).  The XKB
# rules / symbols / keycodes / types / compat / geometry data isn't
# generated — we just stage the tree directly, no build step.
VERSION="2.36"
TARBALL="xkeyboard-config-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/data/xkeyboard-config/${TARBALL}"
SHA256="1f1bb1292a161d520a3485d378609277d108cd07cde0327c16811ff54c3e1595"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/xkeyboard-config-${VERSION}"

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

echo "==> xkeyboard-config ${VERSION} ready at ${TREE_DIR}"
