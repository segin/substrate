#!/bin/sh
# contrib/fontconfig/fetch.sh — fontconfig 2.14.2 (font config/matching).
set -eu
VERSION="2.14.2"
TARBALL="fontconfig-${VERSION}.tar.xz"
URL="https://www.freedesktop.org/software/fontconfig/release/${TARBALL}"
SHA256="dba695b57bce15023d2ceedef82062c2b925e51f5d4cc4aef736cf13f60a468b"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/fontconfig-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> fontconfig ${VERSION} ready at ${TREE_DIR}"
