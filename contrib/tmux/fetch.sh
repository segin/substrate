#!/bin/sh
# contrib/tmux/fetch.sh — fetch tmux, verify, extract, apply patches.
set -eu
VERSION="3.7c"
TARBALL="tmux-${VERSION}.tar.gz"
URL="https://github.com/tmux/tmux/releases/download/${VERSION}/${TARBALL}"
# The digest GitHub records for the release asset.
SHA256="7c60cae9a0e25288e2e24750aafc9e8800fc7fd4555e447e1b29ee4201cfb3bf"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/tmux-${VERSION}"
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
echo "tmux ${VERSION} ready"
