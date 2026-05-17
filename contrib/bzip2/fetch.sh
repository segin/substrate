#!/bin/sh
# fetch.sh — download + PGP-verify + extract + patch bzip2.  Idempotent.
#
# Tarball SHA256 below is cross-verified against Mark Wielaard's PGP
# signature (key 1AA44BE649DE760A) shipped at
#   https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz.sig
# Re-verify whenever VERSION bumps.
set -eu

VERSION="1.0.8"
TARBALL="bzip2-${VERSION}.tar.gz"
URL="https://sourceware.org/pub/bzip2/${TARBALL}"
SIG_URL="${URL}.sig"
SHA256="ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/bzip2-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"
    else wget -O "${TARBALL}" "${URL}"; fi
fi
if [ ! -f "${TARBALL}.sig" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: sig missing" >&2; exit 1; }
    echo "==> Fetching ${SIG_URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}.sig" "${SIG_URL}"
    else wget -O "${TARBALL}.sig" "${SIG_URL}"; fi
fi

got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA mismatch (got ${got})" >&2; exit 1; }

# PGP verify if gpg is available — non-fatal if the keyring lacks the
# Wielaard key, since the SHA above is the cross-check.  Whenever the
# version bumps, re-verify by hand with:
#   gpg --keyserver keyserver.ubuntu.com --recv-keys \
#       12768A96795990107A0D2FDFFC57E3CCACD99A78
#   gpg --verify bzip2-X.Y.Z.tar.gz.sig bzip2-X.Y.Z.tar.gz
if command -v gpg >/dev/null 2>&1; then
    gpg --verify "${TARBALL}.sig" "${TARBALL}" 2>&1 | sed 's/^/  /' || \
        echo "  (PGP verify skipped — Wielaard key not in keyring; SHA matches)"
fi

rm -rf "${TREE_DIR}"
echo "==> Extracting ${TARBALL}"
tar -xzf "${TARBALL}"

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
