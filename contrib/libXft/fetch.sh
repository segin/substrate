#!/bin/sh
# contrib/libXft/fetch.sh — fetch + verify + extract libXft (Xft: antialiased
# X fonts via fontconfig + freetype + libXrender).  Needed by TQt3/TDE.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="2.3.9"
TARBALL="libXft-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/lib/${TARBALL}"
SHA256="60a25b78945ed6932635b3bb1899a517d31df7456e69867ffba27f89ff3976f5"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/libXft-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: ${TARBALL} missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "==> Verifying"; echo "${SHA256}  ${TARBALL}" | sha256sum -c -
echo "==> Extracting"; rm -rf "${TREE_DIR}"; tar xf "${TARBALL}"
# config.sub/config.guess in this tarball predate the substrate target.
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> Done.  ${TREE_DIR}"
