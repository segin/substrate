#!/bin/sh
# contrib/tde/tdeutils/fetch.sh — download + verify + extract tdeutils
# (TDE applications: kcalc, kcharselect, khexedit, kjots, ktimer, kdf, ...).
# Depends on tdelibs (Stage 4).
#
# Nothing under build/ is vendored; this script reproduces it.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tdeutils-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/core/${TARBALL}"
SHA512="8bc89f1bc9e0c2806929997d738a20f79bc9963cc23838b483782dd9d00db41218c45c7a01d483323a3641c78aeff23543a66d9e14539a2f032b38c304cc9035"
TREE="tdeutils-trinity-${VERSION}"

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
