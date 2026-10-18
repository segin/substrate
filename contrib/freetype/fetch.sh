#!/bin/sh
# contrib/freetype/fetch.sh — FreeType 2.15.0 (font rasterizer; cairo/pango/fontconfig).
set -eu
VERSION="2.15.0"
TARBALL="freetype-${VERSION}.tar.xz"
URL="https://download.savannah.gnu.org/releases/freetype/${TARBALL}"
# savannah returned HTTP 504 mid-bootstrap and took the whole run with it.
# SourceForge carries the byte-identical tarball (same SHA256, verified).
URL_FALLBACK="https://downloads.sourceforge.net/freetype/${TARBALL}"
# Verified against the release's PGP signature (freetype-2.15.0.tar.xz.sig),
# Werner Lemberg <wl@gnu.org>, key E30674707856409FF1948010BE6C3AAC63AD8E3F.
SHA256="93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/freetype-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
# Discard a tarball that fails its checksum: otherwise a truncated or
# error-page download is kept and fails verification on every later run.
if [ -f "${TARBALL}" ] && ! echo "${SHA256}  ${TARBALL}" | sha256sum -c --status -; then
    echo "==> ${TARBALL} fails its checksum — discarding and refetching"
    rm -f "${TARBALL}"
fi
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    for u in "${URL}" "${URL_FALLBACK}"; do
        echo "==> Fetching ${u}"
        # --retry covers the transient 5xx that started this; the mirror
        # covers an outage that outlasts it.
        curl -fSL --retry 3 --retry-delay 3 --retry-all-errors \
             -o "${TARBALL}" "${u}" || continue
        echo "${SHA256}  ${TARBALL}" | sha256sum -c --status - && break
        echo "    (wrong content from ${u}, trying the next source)"
        rm -f "${TARBALL}"
    done
fi
[ -f "${TARBALL}" ] || { echo "fetch.sh: could not download ${TARBALL}" >&2; exit 1; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
echo "==> freetype ${VERSION} ready at ${TREE_DIR}"
