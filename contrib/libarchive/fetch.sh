#!/bin/sh
# fetch.sh — download + PGP-verify + extract + patch libarchive.  Idempotent.
#
# SHA256 below is cross-verified against Martin Matuska's PGP
# signature (key 5848A18B8F14184B) shipped at
#   https://www.libarchive.org/downloads/libarchive-3.7.7.tar.gz.asc
# The key has expired but the signature itself was valid at signing
# time (2024-10-13).  Re-verify whenever VERSION bumps.
set -eu

VERSION="3.7.7"
TARBALL="libarchive-${VERSION}.tar.gz"
URL="https://www.libarchive.org/downloads/${TARBALL}"
SIG_URL="${URL}.asc"
SHA256="4cc540a3e9a1eebdefa1045d2e4184831100667e6d7d5b315bb1cbc951f8ddff"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libarchive-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"
    else wget -O "${TARBALL}" "${URL}"; fi
fi
if [ ! -f "${TARBALL}.asc" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: sig missing" >&2; exit 1; }
    echo "==> Fetching ${SIG_URL}"
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}.asc" "${SIG_URL}"
    else wget -O "${TARBALL}.asc" "${SIG_URL}"; fi
fi

got=$(sha256sum "${TARBALL}" | awk '{print $1}')
[ "${got}" = "${SHA256}" ] || { echo "fetch.sh: SHA mismatch (got ${got})" >&2; exit 1; }

# PGP verify — non-fatal if the keyring lacks the Matuska key, since
# the SHA above is the cross-check.  Whenever the version bumps,
# re-verify by hand with:
#   gpg --keyserver keyserver.ubuntu.com --recv-keys \
#       DB2C7CF1B4C265FAEF56E3FC5848A18B8F14184B
#   gpg --verify libarchive-X.Y.Z.tar.gz.asc libarchive-X.Y.Z.tar.gz
if command -v gpg >/dev/null 2>&1; then
    gpg --verify "${TARBALL}.asc" "${TARBALL}" 2>&1 | sed 's/^/  /' || \
        echo "  (PGP verify skipped — Matuska key not in keyring; SHA matches)"
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
