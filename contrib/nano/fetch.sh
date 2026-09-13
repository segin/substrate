#!/bin/sh
# contrib/nano/fetch.sh — fetch GNU nano, verify, extract, apply patches.
set -eu
VERSION="9.2"
TARBALL="nano-${VERSION}.tar.xz"
URL="https://www.nano-editor.org/dist/v9/${TARBALL}"
SHA256="05ecb99247b782e8a5b3a25ed4101dd034b0236902f7449bc9795b717642f7e9"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/nano-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TARBALL}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }; curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "nano ${VERSION} ready"
