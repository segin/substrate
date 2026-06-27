#!/bin/sh
# contrib/faad2/fetch.sh — AAC decoder (libfaad).  Fetch, verify, extract, patch.
# faad2 2.11.1 is CMake-only; cross-built by build.sh.
set -eu
VER="2.11.1"
TB="faad2-${VER}.tar.gz"
URL="https://github.com/knik0/faad2/archive/refs/tags/${VER}.tar.gz"
SHA256="72dbc0494de9ee38d240f670eccf2b10ef715fd0508c305532ca3def3225bb06"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/faad2-${VER}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TB}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }; curl -fSL -o "${TB}" "${URL}"; }
echo "${SHA256}  ${TB}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TB}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "faad2 ${VER} ready"
