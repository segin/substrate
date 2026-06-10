#!/bin/sh
# contrib/expat/fetch.sh — Expat 2.6.2 (XML parser; fontconfig needs it).
set -eu
VERSION="2.6.2"; UND="2_6_2"
TARBALL="expat-${VERSION}.tar.xz"
URL="https://github.com/libexpat/libexpat/releases/download/R_${UND}/${TARBALL}"
SHA256="ee14b4c5d8908b1bec37ad937607eab183d4d9806a08adee472c3c3121d27364"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/expat-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> expat ${VERSION} ready at ${TREE_DIR}"
