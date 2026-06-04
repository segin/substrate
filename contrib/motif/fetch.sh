#!/bin/sh
# contrib/motif/fetch.sh — fetch OpenMotif 2.3.8, verify, extract, patch.
set -eu
VERSION="2.3.8"
TARBALL="motif-${VERSION}.tar.gz"
URL="https://downloads.sourceforge.net/project/motif/Motif%20${VERSION}%20Source%20Code/${TARBALL}"
SHA256="859b723666eeac7df018209d66045c9853b50b4218cecadb794e2359619ebce7"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/motif-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
# Teach the bundled config.sub about the substrate OS triplet.
for cs in $(find "${TREE_DIR}" -name config.sub); do
    grep -q substrate "$cs" || sed -i 's/\(-sortix\* \)/\1| -substrate* /; s/\(| sortix\* \)/\1| substrate* /' "$cs"
done
if [ -f "${HERE}/series" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then echo "==> ${p} already applied"; continue; fi
        echo "==> Applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi
echo "==> motif ${VERSION} ready at ${TREE_DIR}"
