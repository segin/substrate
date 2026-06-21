#!/bin/sh
# contrib/tde/tdegames/fetch.sh — download + verify + extract tdegames
# (TDE applications: games (kmines, kpat, kreversi, ...)).  Depends on tdelibs.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tdegames-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/core/${TARBALL}"
SHA512="2d867e1a56adfcd1602ed6ae3f1caa2efaf758e6ffd5e6676a5be906c66f9a4bfe2b0a560de8594ecb0256ad18accbcf003edf6ca3390be15c1690df035b00b1"
TREE="tdegames-trinity-${VERSION}"

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

if [ -d "${HERE}/patches" ] && [ -f "${HERE}/series" ]; then
    echo "==> Applying patch series"
    while IFS= read -r p; do
        [ -z "${p}" ] && continue
        echo "    ${p}"
        patch -p1 -d "${TREE}" < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> Done.  ${HERE}/build/${TREE}"
