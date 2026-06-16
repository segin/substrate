#!/bin/sh
# contrib/tde/tqt3/fetch.sh — download + verify + extract TQt3 (the Qt3
# fork that the entire Trinity Desktop Environment is built on), then
# install the substrate mkspec and apply the substrate patch series.
#
# Nothing under build/ is vendored; this script reproduces it.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tqt-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/dependencies/${TARBALL}"
SHA512="f2af1d61f55870271f936b62692c9b84205935d95f5df86eedf13e15e0ce7699aef34a4491fb1839b3a0b825bd53b65e86a363a1c8730312360871928d1f9277"
TREE="tqt-trinity-${VERSION}"

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

echo "==> Installing substrate-g++ mkspec"
cp -a "${HERE}/substrate-g++" "${TREE}/mkspecs/substrate-g++"

echo "==> Applying patch series"
while IFS= read -r p; do
    [ -z "${p}" ] && continue
    echo "    ${p}"
    patch -p1 -d "${TREE}" < "${HERE}/patches/${p}"
done < "${HERE}/series"

echo "==> Done.  Source tree at build/${TREE}"
