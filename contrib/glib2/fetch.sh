#!/bin/sh
# contrib/glib2/fetch.sh — GLib 2.56.4 (last autotools GLib; GTK+ 2.x base).
set -eu
VERSION="2.56.4"; SERIES="2.56"
TARBALL="glib-${VERSION}.tar.xz"
URL="https://download.gnome.org/sources/glib/${SERIES}/${TARBALL}"
SHA256="27f703d125efb07f8a743666b580df0b4095c59fc8750e8890132c91d437504c"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/glib-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> glib2 ${VERSION} ready at ${TREE_DIR}"
