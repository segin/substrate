#!/bin/sh
# contrib/tde/tde-cmake/fetch.sh — download + verify + extract the TDE
# shared CMake modules (TDEMacros, TDEVersion, FindTQt, tde_automoc,
# tde_uic, ...).  Every contrib/tde/* CMake sub-port adds this tree's
# modules/ dir to CMAKE_MODULE_PATH; nothing is built or installed here.
#
# Nothing under build/ is vendored; this script reproduces it.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tde-cmake-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/dependencies/${TARBALL}"
SHA512="3be0b62634b904f5ba96ae2a5ef6c62b957111e9483ebc51de973bee1c3142ae6e4819efd7201fd46e06d8a3506f7b7831e071366ca7b5857dbbcafc06d37161"
TREE="tde-cmake-trinity-${VERSION}"

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

echo "==> Done.  CMake modules under ${HERE}/build/${TREE}/modules"
