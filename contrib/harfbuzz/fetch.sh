#!/bin/sh
# contrib/harfbuzz/fetch.sh — HarfBuzz 14.6.0 (text shaping; pango and psymp3 need it).
set -eu
VERSION="14.6.0"
TARBALL="harfbuzz-${VERSION}.tar.xz"
URL="https://github.com/harfbuzz/harfbuzz/releases/download/${VERSION}/${TARBALL}"
# Matches the release asset's digest as GitHub records it.
SHA256="d07a007327277708a2a73ae437887cdbaf282937f6d03ca5467723e9099af586"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/harfbuzz-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
echo "==> harfbuzz ${VERSION} ready at ${TREE_DIR}"
