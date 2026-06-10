#!/bin/sh
# contrib/harfbuzz/fetch.sh — HarfBuzz 2.6.8 (text shaping; pango 1.42 needs it).
set -eu
VERSION="2.6.8"
TARBALL="harfbuzz-${VERSION}.tar.xz"
URL="https://github.com/harfbuzz/harfbuzz/releases/download/${VERSION}/${TARBALL}"
SHA256="6648a571a27f186e47094121f0095e1b809e918b3037c630c7f38ffad86e3035"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/harfbuzz-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> harfbuzz ${VERSION} ready at ${TREE_DIR}"
