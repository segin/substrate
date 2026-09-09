#!/bin/sh
# contrib/opusfile/fetch.sh — fetch opusfile, verify, extract, apply patches.
set -eu
VERSION="0.12"
TARBALL="opusfile-${VERSION}.tar.gz"
URL="https://downloads.xiph.org/releases/opus/${TARBALL}"
SHA256="118d8601c12dd6a44f52423e68ca9083cc9f2bfe72da7a8c1acb22a80ae3550b"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/opusfile-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TARBALL}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }; curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "opusfile ${VERSION} ready"
