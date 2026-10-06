#!/bin/sh
# contrib/libevent/fetch.sh — fetch libevent, verify, extract, apply patches.
set -eu
VERSION="2.1.13-stable"
TARBALL="libevent-${VERSION}.tar.gz"
URL="https://github.com/libevent/libevent/releases/download/release-${VERSION}/${TARBALL}"
# The digest GitHub records for the release asset.
SHA256="f7e9383b8c0baa81b687e5b5eecc01beefaf1b19b64151d95ed61647fe7a315c"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/libevent-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    . "${HERE}/../substrate-fetch.sh"
    substrate_fetch "${URL}" "${TARBALL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "libevent ${VERSION} ready"
