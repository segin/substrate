#!/bin/sh
# contrib/gdk-pixbuf/fetch.sh — gdk-pixbuf 2.36.12 (last autotools; GTK+ 2.x images).
set -eu
VERSION="2.36.12"; SERIES="2.36"
TARBALL="gdk-pixbuf-${VERSION}.tar.xz"
URL="https://download.gnome.org/sources/gdk-pixbuf/${SERIES}/${TARBALL}"
SHA256="fff85cf48223ab60e3c3c8318e2087131b590fd6f1737e42cb3759a3b427a334"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/gdk-pixbuf-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> gdk-pixbuf ${VERSION} ready at ${TREE_DIR}"
