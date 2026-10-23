#!/bin/sh
# fetch.sh — download + verify + extract + patch the Linux kernel sources
# that build.sh compiles with substrate's own cc, as and ld.  Idempotent.
#
# Needs network access to cdn.kernel.org (or the tarball already in
# build/; --no-network refuses to download).
set -eu

VERSION="7.3"
TARBALL="linux-${VERSION}.tar.xz"
URL="https://cdn.kernel.org/pub/linux/kernel/v7.x/${TARBALL}"
# SHA-256 from kernel.org's sha256sums.asc for v7.x (signed by the
# kernel.org autosigner key B8868C80BA62A1FFFAF5FDA9632D3A06589DA6B1),
# fetched 2026-10-21.
SHA256="87363c24f10e06321eb362765521094e94471f6bff8bce2d6b3e40f39f004953"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/linux-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    . "${HERE}/../substrate-fetch.sh"
    substrate_fetch "${URL}" "${TARBALL}"
fi

got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA mismatch (got ${got})" >&2; exit 1; }

rm -rf "${TREE_DIR}"
echo "==> Extracting ${TARBALL}"
tar -xJf "${TARBALL}"

SERIES_FILE="${HERE}/series"
if [ -s "${SERIES_FILE}" ]; then
    echo "==> Applying patches from ${SERIES_FILE}"
    cd "${TREE_DIR}"
    while IFS= read -r p || [ -n "${p}" ]; do
        case "${p}" in ''|\#*) continue;; esac
        echo "  apply ${p}"
        patch -p1 < "${HERE}/patches/${p}"
    done < "${SERIES_FILE}"
fi

echo "==> Ready at ${TREE_DIR}"
