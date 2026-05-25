#!/bin/sh
# contrib/font-misc-misc/fetch.sh — substrate's port of X.Org's
# misc-fixed BDF bitmap font collection.  Source-only fetch; the
# actual install is done by build.sh.

set -eu

VERSION="1.1.3"
TARBALL="font-misc-misc-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/font/${TARBALL}"
SHA256="79abe361f58bb21ade9f565898e486300ce1cc621d5285bec26e14b6a8618fed"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/font-misc-misc-${VERSION}"

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

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

echo "==> font-misc-misc ${VERSION} ready at ${TREE_DIR}"
