#!/bin/sh
# contrib/libpng/fetch.sh — libpng 1.6.43 (cairo/gdk-pixbuf PNG support).
set -eu
VERSION="1.6.58"
TARBALL="libpng-${VERSION}.tar.xz"
URL="https://download.sourceforge.net/libpng/${TARBALL}"
# sha256 of the tarball SourceForge serves; its stated md5 for this file
# (c6c372a9d7754c66e0b77a8d34987a3b, from best_release.json) matches the
# same download.
SHA256="28eb403f51f0f7405249132cecfe82ea5c0ef97f1b32c5a65828814ae0d34775"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/libpng-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> libpng ${VERSION} ready at ${TREE_DIR}"
