#!/bin/sh
# contrib/mpfr/fetch.sh — download + verify + extract GNU MPFR.
# MPFR is the correctly-rounded multiple-precision float library; gdb
# requires it (with GMP) for its target-value arithmetic.
set -eu

VERSION="4.2.2"
TARBALL="mpfr-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/mpfr/${TARBALL}"
# Upstream also publishes this tarball at www.mpfr.org; both, and the
# kernel.org GNU mirror, carry the same bytes.
URL_FALLBACK="https://www.mpfr.org/mpfr-${VERSION}/${TARBALL}"
SHA256="b67ba0383ef7e8a8563734e2e889ef5ec3c3b898a01d00fa0a6869ad81c6ce01"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/mpfr-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    for u in "${URL}" "${URL_FALLBACK}"; do
        echo "==> Fetching ${u}"
        if command -v curl >/dev/null 2>&1; then
            curl -fSL -o "${TARBALL}" "${u}" && break
        else
            wget -O "${TARBALL}" "${u}" && break
        fi
    done
fi
[ -f "${TARBALL}" ] || { echo "fetch.sh: could not download ${TARBALL}" >&2; exit 1; }

got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA256 mismatch for ${TARBALL}" >&2; exit 1; }

rm -rf "${TREE_DIR}"
tar -xJf "${TARBALL}"

SERIES_FILE="${HERE}/series"
if [ -s "${SERIES_FILE}" ]; then
    cd "${TREE_DIR}"
    while IFS= read -r p || [ -n "${p}" ]; do
        case "${p}" in ''|\#*) continue;; esac
        echo "  apply ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${SERIES_FILE}"
fi

echo "==> Ready at ${TREE_DIR}"
