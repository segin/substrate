#!/bin/sh
# contrib/fribidi/fetch.sh — GNU FriBidi 1.0.13 (Unicode bidi; pango dep).
set -eu
VERSION="1.0.13"
TARBALL="fribidi-${VERSION}.tar.xz"
URL="https://github.com/fribidi/fribidi/releases/download/v${VERSION}/${TARBALL}"
SHA256="7fa16c80c81bd622f7b198d31356da139cc318a63fc7761217af4130903f54a2"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/fribidi-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> fribidi ${VERSION} ready at ${TREE_DIR}"
