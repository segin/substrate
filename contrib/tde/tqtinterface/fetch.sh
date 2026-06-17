#!/bin/sh
# contrib/tde/tqtinterface/fetch.sh — download + verify + extract
# tqtinterface (the TQt<->Qt compatibility layer: the tq*.h headers,
# libtqt, and the DCOP-IDL / moc / uic wrappers TDE builds against).
#
# Builds cleanly against substrate's cross TQt3 with no patches.
# Nothing under build/ is vendored; this script reproduces it.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tqtinterface-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/dependencies/${TARBALL}"
SHA512="f95ea71ca4269e7a70142e4fbb7f9d245ca3f5e8c12ee812f3411e1f77e8253f540b54112eac0c4428404b4282a571e258e622efd8f2f49c38adf72fa64ea84d"
TREE="tqtinterface-trinity-${VERSION}"

mkdir -p "${HERE}/build"
cd "${HERE}/build"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: ${TARBALL} missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    curl -fSL -o "${TARBALL}" "${URL}"
fi

echo "==> Verifying SHA512"
echo "${SHA512}  ${TARBALL}" | sha512sum -c -

echo "==> Extracting"
rm -rf "${TREE}"
tar xf "${TARBALL}"

# No patch series: tqtinterface cross-builds unmodified.

echo "==> Done.  ${HERE}/build/${TREE}"
