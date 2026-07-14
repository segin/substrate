#!/bin/sh
# contrib/cmake/fetch.sh — download the CMake source, verify, extract, patch.
set -eu
VERSION="3.30.5"
TARBALL="cmake-${VERSION}.tar.gz"
URL="https://github.com/Kitware/CMake/releases/download/v${VERSION}/${TARBALL}"
SHA256="9f55e1a40508f2f29b7e065fa08c29f82c402fa0402da839fffe64a25755a86d"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/cmake-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi
got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA mismatch (got ${got})" >&2; exit 1; }
rm -rf "${TREE}"
tar -xzf "${TARBALL}"
cd "${TREE}"
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
        echo "  apply ${p}"; patch -p1 < "${HERE}/patches/${p}"; done < "${HERE}/series"
fi
echo "CMake ${VERSION} unpacked + patched at ${TREE}"
