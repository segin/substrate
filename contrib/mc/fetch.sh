#!/bin/sh
# contrib/mc/fetch.sh — fetch GNU Midnight Commander, verify, extract, apply patches.
set -eu
VERSION="4.8.33"
TARBALL="mc-${VERSION}.tar.xz"
URL="https://ftp.osuosl.org/pub/midnightcommander/${TARBALL}"
SHA256="cae149d42f844e5185d8c81d7db3913a8fa214c65f852200a9d896b468af164c"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/mc-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TARBALL}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }; curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "mc ${VERSION} ready"
