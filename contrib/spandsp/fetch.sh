#!/bin/sh
# contrib/spandsp/fetch.sh — fetch spandsp, verify, extract, apply patches.
set -eu
VERSION="0.0.6"
TARBALL="spandsp-${VERSION}.tar.gz"
URL="https://www.soft-switch.org/downloads/spandsp/${TARBALL}"
SHA256="cc053ac67e8ac4bb992f258fd94f275a7872df959f6a87763965feabfdcc9465"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/spandsp-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TARBALL}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }; curl -fSL -o "${TARBALL}" "${URL}"; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "spandsp ${VERSION} ready"
