#!/bin/sh
# contrib/taglib/fetch.sh — fetch TagLib 2.3.2, verify, extract, apply patches.
#
# TagLib 2.x needs the utfcpp (utf8cpp) header-only library.  Since 2.1 the
# release tarball carries it in 3rdparty/utfcpp/, so TagLib's
# add_subdirectory("3rdparty/utfcpp") fallback (which provides the utf8::cpp
# target) builds offline with nothing else to fetch.
set -eu

VERSION="2.3.2"
TARBALL="taglib-${VERSION}.tar.gz"
URL="https://github.com/taglib/taglib/releases/download/v${VERSION}/${TARBALL}"
URL_FALLBACK="https://taglib.github.io/releases/${TARBALL}"
# Matches the release asset's digest as GitHub records it.
SHA256="3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE="${BUILD_DIR}/taglib-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"

[ -f "${TARBALL}" ] || {
  [ "${1:-}" = "--no-network" ] && { echo "missing ${TARBALL}" >&2; exit 1; }
  curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}" || curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL_FALLBACK}"
}
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

[ -d "${TREE}" ] || tar xf "${TARBALL}"
[ -f "${TREE}/3rdparty/utfcpp/CMakeLists.txt" ] || {
  echo "fetch.sh: ${TARBALL} has no 3rdparty/utfcpp" >&2; exit 1; }

if [ -f "${HERE}/series" ]; then
  cd "${TREE}"
  while IFS= read -r p; do
    [ -z "$p" ] && continue
    case "$p" in \#*) continue;; esac
    [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }
  done < "${HERE}/series"
fi

echo "taglib ${VERSION} ready"
