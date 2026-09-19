#!/bin/sh
# contrib/python/fetch.sh — fetch CPython, verify, extract, apply patches.
#
# The SHA-256 below is the digest upstream signed: it is the value inside
# Python-3.14.7.tar.xz.sigstore, python.org's sigstore bundle for this file
# (the release pages publish no checksum text).  Verified to match the
# tarball this port downloads.
set -eu
VERSION="3.14.7"
TARBALL="Python-${VERSION}.tar.xz"
URL="https://www.python.org/ftp/python/${VERSION}/${TARBALL}"
SHA256="3b48dac8fb59f62eaa67ac83c1eb12bda1b7a08406dd286e252c11a66be27f81"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/Python-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
[ -f "${TARBALL}" ] || { [ "${1:-}" = "--no-network" ] && { echo "missing ${TARBALL}" >&2; exit 1; }; curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -s "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "python ${VERSION} ready"
