#!/bin/sh
# contrib/libogg/fetch.sh — fetch libogg, verify, extract, apply patches.
set -eu
VERSION="1.3.6"
TARBALL="libogg-${VERSION}.tar.gz"
URL="https://downloads.xiph.org/releases/ogg/${TARBALL}"
# Matches downloads.xiph.org/releases/ogg/SHA256SUMS.
SHA256="83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/libogg-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TARBALL}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }; curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "libogg ${VERSION} ready"
