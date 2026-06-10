#!/bin/sh
# contrib/gtk2/fetch.sh — GTK+ 2.24.33 (last GTK+ 2.x).
set -eu
VERSION="2.24.33"; SERIES="2.24"
TARBALL="gtk+-${VERSION}.tar.xz"
URL="https://download.gnome.org/sources/gtk+/${SERIES}/${TARBALL}"
SHA256="ac2ac757f5942d318a311a54b0c80b5ef295f299c2a73c632f6bfb1ff49cc6da"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/gtk+-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> gtk2 ${VERSION} ready at ${TREE_DIR}"
