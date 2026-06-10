#!/bin/sh
# contrib/freetype/fetch.sh — FreeType 2.13.2 (font rasterizer; cairo/pango/fontconfig).
set -eu
VERSION="2.13.2"
TARBALL="freetype-${VERSION}.tar.xz"
URL="https://download.savannah.gnu.org/releases/freetype/${TARBALL}"
SHA256="12991c4e55c506dd7f9b765933e62fd2be2e06d421505d7950a132e4f1bb484d"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/freetype-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> freetype ${VERSION} ready at ${TREE_DIR}"
