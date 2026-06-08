#!/bin/sh
#
# contrib/xrdb/fetch.sh — fetch the xrdb tarball, verify, extract,
# apply the substrate patch series.

set -eu

VERSION="1.2.2"
TARBALL="xrdb-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/app/${TARBALL}"
SHA256="31f5fcab231b38f255b00b066cf7ea3b496df712c9eb2d0d50c670b63e5033f4"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/xrdb-${VERSION}"

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

# Teach the bundled config.sub about the substrate OS (same one-token shape
# used across contrib; the X.Org app tarballs ship the newer list form).
if [ -f "${TREE_DIR}/config.sub" ]; then
    grep -q substrate "${TREE_DIR}/config.sub" || \
      sed -i 's/\(-sortix\* \)/\1| -substrate* /; s/\(| sortix\* \)/\1| substrate* /' "${TREE_DIR}/config.sub"
fi

echo "==> Done.  xrdb-${VERSION} ready under ${TREE_DIR}"
