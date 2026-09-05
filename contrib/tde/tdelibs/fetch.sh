#!/bin/sh
# contrib/tde/tdelibs/fetch.sh — download + verify + extract tdelibs
# (TDE Stage 4: the core libraries — tdecore, tdeui, tdeio, dcop, kjs,
# tdehtml, ...).  Depends on tqtinterface (Stage 2).
#
# Nothing under build/ is vendored; this script reproduces it.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tdelibs-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/core/${TARBALL}"
SHA512="c44935bfea9571b9cb8708a6af87522a514e4bba65a62d8796cee64ca3e10309c8a082dec556eca8248d1d67adb980aeea363808ff964a557ea32dacef391286"
TREE="tdelibs-trinity-${VERSION}"

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
