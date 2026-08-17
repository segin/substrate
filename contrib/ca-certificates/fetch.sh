#!/bin/sh
#
# contrib/ca-certificates/fetch.sh — fetch the Mozilla CA root bundle.
#
# There is no "ca-certificates" tarball to build: Debian's package is a shell
# script plus the Mozilla trust store extracted from NSS.  The store itself is
# the deliverable, and curl.se republishes it as a single PEM bundle generated
# from NSS's certdata.txt.  That is what this port installs.
#
# The DATED filename is used deliberately.  https://curl.se/ca/cacert.pem is a
# floating pointer that changes whenever Mozilla updates the store, so pinning
# it would make the port unreproducible and break SHA verification on every
# upstream refresh.  To update: bump VERSION, drop in the new SHA256 (curl.se
# publishes one at <url>.sha256), and re-run build.sh.

set -eu

VERSION="2026-08-13"
PEM="cacert-${VERSION}.pem"
URL="https://curl.se/ca/${PEM}"
SHA256="f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${PEM}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: ${PEM} missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${PEM}" "${URL}"
    else
        wget -O "${PEM}" "${URL}"
    fi
fi

echo "==> Verifying ${PEM}"
echo "${SHA256}  ${PEM}" | sha256sum -c -

echo "==> ca-certificates ${VERSION} ready ($(grep -c 'BEGIN CERTIFICATE' "${PEM}") roots)"
