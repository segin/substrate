#!/bin/sh
# contrib/libffi/fetch.sh — libffi 3.4.4 (glib2 gobject closures need it).
set -eu
VERSION="3.4.4"
TARBALL="libffi-${VERSION}.tar.gz"
URL="https://github.com/libffi/libffi/releases/download/v${VERSION}/${TARBALL}"
SHA256="d66c56ad259a82cf2a9dfc408b32bf5da52371500b84745f7fb8b645712df676"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/libffi-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> libffi ${VERSION} ready at ${TREE_DIR}"
