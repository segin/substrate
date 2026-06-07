#!/bin/sh
#
# contrib/rpcbind/fetch.sh — fetch the rpcbind tarball, verify, extract.
# rpcbind is the Sun RPC portmapper (port 111); CDE's ToolTalk ttsession
# registers its RPC service via pmap_set(3), which needs a running portmapper,
# so without rpcbind ToolTalk (and thus a full CDE session) fails to start.

set -eu

VERSION="1.2.6"
TARBALL="rpcbind-${VERSION}.tar.bz2"
URL="https://downloads.sourceforge.net/project/rpcbind/rpcbind/${VERSION}/${TARBALL}"
SHA256="5613746489cae5ae23a443bb85c05a11741a5f12c8f55d2bb5e83b9defeee8de"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/rpcbind-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xjf "${TARBALL}"; }

echo "==> rpcbind ${VERSION} ready at ${TREE_DIR}"
