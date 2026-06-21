#!/bin/sh
# contrib/tde/tdetoys/fetch.sh — download + verify + extract tdetoys
# (TDE applications: toys (kmoon, kworldwatch, amor, ...)).  Depends on tdelibs.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tdetoys-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/core/${TARBALL}"
SHA512="78b55e142b5e04bc27361014d5a9283ccf9ef4ec2b043589e4f81edae214478d72c2d325afe1fe70a118e92e55567ca75f925ec0cf78135197928204f6cf9bcd"
TREE="tdetoys-trinity-${VERSION}"

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
