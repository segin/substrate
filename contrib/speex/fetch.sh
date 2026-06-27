#!/bin/sh
# contrib/speex/fetch.sh — fetch speex, verify, extract, apply patches.
set -eu
VERSION="1.2.1"
TARBALL="speex-${VERSION}.tar.gz"
URL="https://downloads.xiph.org/releases/speex/${TARBALL}"
SHA256="4b44d4f2b38a370a2d98a78329fefc56a0cf93d1c1be70029217baae6628feea"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/speex-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TARBALL}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }; curl -fSL -o "${TARBALL}" "${URL}"; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "speex ${VERSION} ready"
