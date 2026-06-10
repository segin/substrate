#!/bin/sh
# contrib/atk/fetch.sh — ATK 2.28.1 (accessibility toolkit; GTK+ 2.x dep).
set -eu
VERSION="2.28.1"; SERIES="2.28"
TARBALL="atk-${VERSION}.tar.xz"
URL="https://download.gnome.org/sources/atk/${SERIES}/${TARBALL}"
SHA256="cd3a1ea6ecc268a2497f0cd018e970860de24a6d42086919d6bf6c8e8d53f4fc"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/atk-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> atk ${VERSION} ready at ${TREE_DIR}"
