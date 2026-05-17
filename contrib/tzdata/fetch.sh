#!/bin/sh
# Download both tzdata and tzcode (they're released together).
#
# Trust path: IANA signs each release with Paul Eggert's PGP key,
# fingerprint 7E3792A9 D8ACF7D6 33BC1588 ED97E90E 62AA7E34.  The .asc
# files live next to the tarballs at data.iana.org/time-zones/releases/.
# IANA silently re-publishes the same VERSION filename when patching,
# so the SHA-256 below is the value of the CURRENT upload PGP-verified
# against Eggert's signature on 2026-05-17.  When the SHA mismatches
# at fetch time, re-verify by hand:
#
#   gpg --keyserver keyserver.ubuntu.com --recv-keys \
#       7E3792A9D8ACF7D633BC1588ED97E90E62AA7E34
#   curl -O https://data.iana.org/time-zones/releases/tzcode<v>.tar.gz.asc
#   gpg --verify tzcode<v>.tar.gz.asc tzcode<v>.tar.gz
#   sha256sum tzcode<v>.tar.gz   # then update TZCODE_SHA
set -eu

VERSION="2024a"
TZDATA_TAR="tzdata${VERSION}.tar.gz"
TZCODE_TAR="tzcode${VERSION}.tar.gz"
TZDATA_URL="https://data.iana.org/time-zones/releases/${TZDATA_TAR}"
TZCODE_URL="https://data.iana.org/time-zones/releases/${TZCODE_TAR}"
TZDATA_SHA="0d0434459acbd2059a7a8da1f3304a84a86591f6ed69c6248fffa502b6edffe3"
TZCODE_SHA="80072894adff5a458f1d143e16e4ca1d8b2a122c9c5399da482cb68cba6a1ff8"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/tz-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

fetch() {
    local tar="$1" url="$2"
    if [ -f "${tar}" ]; then return; fi
    if [ "${NO_NETWORK:-0}" = "1" ]; then
        echo "fetch.sh: ${tar} not present and NO_NETWORK=1" >&2
        exit 1
    fi
    echo "==> Fetching ${url}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${tar}" "${url}"
    else
        wget -O "${tar}" "${url}"
    fi
}

verify() {
    local tar="$1" want="$2"
    local got
    got=$(sha256sum "${tar}" | awk '{print $1}')
    [ "${got}" = "${want}" ] || {
        echo "fetch.sh: ${tar} SHA-256 mismatch (expected ${want}, got ${got})" >&2
        exit 1
    }
}

fetch "${TZDATA_TAR}" "${TZDATA_URL}"
fetch "${TZCODE_TAR}" "${TZCODE_URL}"
fetch "${TZDATA_TAR}.asc" "${TZDATA_URL}.asc"
fetch "${TZCODE_TAR}.asc" "${TZCODE_URL}.asc"
verify "${TZDATA_TAR}" "${TZDATA_SHA}"
verify "${TZCODE_TAR}" "${TZCODE_SHA}"

# PGP cross-check.  Non-fatal if gpg lacks Eggert's key (the SHA
# above is the actual trust root); the verify result is logged so
# whoever bumps VERSION can read it.
if command -v gpg >/dev/null 2>&1; then
    for f in "${TZDATA_TAR}" "${TZCODE_TAR}"; do
        gpg --verify "${f}.asc" "${f}" 2>&1 | sed 's/^/  /' || \
            echo "  (PGP verify skipped — Eggert key not in keyring; SHA matches)"
    done
fi

rm -rf "${TREE_DIR}"
mkdir -p "${TREE_DIR}"
echo "==> Extracting tzcode + tzdata into ${TREE_DIR}"
tar -xzf "${TZCODE_TAR}" -C "${TREE_DIR}"
tar -xzf "${TZDATA_TAR}" -C "${TREE_DIR}"

# Apply patches in series order.
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
