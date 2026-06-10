#!/bin/sh
# contrib/hexchat/fetch.sh — HexChat 2.10.2 (GTK+ 2 IRC client; last autotools release).
set -eu
VERSION="2.10.2"
TARBALL="hexchat-${VERSION}.tar.xz"
URL="https://dl.hexchat.net/hexchat/${TARBALL}"
SHA256="87ebf365c576656fa3f23f51d319b3a6d279e4a932f2f8961d891dd5a5e1b52c"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/hexchat-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
if [ "${SHA256}" != "REPLACE" ]; then echo "${SHA256}  ${TARBALL}" | sha256sum -c -; fi
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> hexchat ${VERSION} ready at ${TREE_DIR}"
