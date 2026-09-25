#!/bin/sh
# contrib/spandsp/fetch.sh — fetch spandsp, verify, extract, apply patches.
set -eu
VERSION="0.0.6"
TARBALL="spandsp-${VERSION}.tar.gz"
URL="https://www.soft-switch.org/downloads/spandsp/${TARBALL}"
# The upstream host fails TLS handshakes, answers automated clients with
# HTTP 418, or serves an error page in place of the archive.  Fedora's
# source archive (keyed by the tarball's MD5) holds the byte-identical
# original; the SHA-256 below still decides what is accepted.
URL_FALLBACK="https://src.fedoraproject.org/repo/pkgs/spandsp/${TARBALL}/897d839516a6d4edb20397d4757a7ca3/${TARBALL}"
SHA256="cc053ac67e8ac4bb992f258fd94f275a7872df959f6a87763965feabfdcc9465"
HERE="$(cd "$(dirname "$0")" && pwd)"; BUILD_DIR="${HERE}/build"; TREE="${BUILD_DIR}/spandsp-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "missing tarball" >&2; exit 1; }
    for u in "${URL}" "${URL_FALLBACK}"; do
        echo "==> Fetching ${u}"
        curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${u}" || continue
        # Accept the first source that actually serves the archive.
        echo "${SHA256}  ${TARBALL}" | sha256sum -c --status - && break
        echo "    (wrong content from ${u}, trying the next source)"
        rm -f "${TARBALL}"
    done
fi
[ -f "${TARBALL}" ] || { echo "fetch.sh: could not download ${TARBALL}" >&2; exit 1; }
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE}" ] || tar xf "${TARBALL}"
if [ -f "${HERE}/series" ]; then cd "${TREE}"; while IFS= read -r p; do [ -z "$p" ] && continue; case "$p" in \#*) continue;; esac
  [ -f ".applied-$p" ] || { patch -p1 < "${HERE}/patches/$p"; touch ".applied-$p"; }; done < "${HERE}/series"; fi
echo "spandsp ${VERSION} ready"
