#!/bin/sh
# contrib/tde/tdebase/fetch.sh — download + verify + extract tdebase
# (TDE Stage 5: the desktop base — tdecore runtime, twin, kicker,
# kdesktop, konqueror, tdm, ...).  Depends on tdelibs (Stage 4).
#
# Nothing under build/ is vendored; this script reproduces it.
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="14.1.6"
TARBALL="tdebase-trinity-${VERSION}.tar.xz"
URL="https://mirror.ppa.trinitydesktop.org/trinity/releases/R${VERSION}/main/core/${TARBALL}"
SHA512="ab684af6bd67f3da4a0cc77000447fef552c5d050a0fc3732de4efd64dbea2906ff94e5a0ccf5815601ac7d6a576c7ba5a3519b03bf5ba67eccf50451bf13fd7"
TREE="tdebase-trinity-${VERSION}"

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
