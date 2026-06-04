#!/bin/sh
# contrib/disasterparty/fetch.sh — fetch disasterparty, verify, extract,
# autoreconf, patch.  disasterparty is an LLM-API client library (OpenAI /
# Anthropic / Gemini) over libcurl + libcjson.  The GitHub source archive does
# not ship a generated configure, so we run autoreconf on the build host.
set -eu
VERSION="0.6.0"
TARBALL="disasterparty-${VERSION}.tar.gz"
URL="https://github.com/segin/disasterparty/archive/refs/tags/${VERSION}.tar.gz"
SHA256="347d47d56a625c84f0b9e27a260743d87e993a248675d95e0580918d972752a9"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/disasterparty-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    if command -v curl >/dev/null 2>&1; then curl -fSL -o "${TARBALL}" "${URL}"; else wget -O "${TARBALL}" "${URL}"; fi
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
cd "${TREE_DIR}"
if [ -f "${HERE}/series" ]; then
    while IFS= read -r p; do
        case "$p" in ''|'#'*) continue ;; esac
        [ -f "${HERE}/patches/${p}" ] || { echo "fetch.sh: missing patch ${p}" >&2; exit 1; }
        if patch -p1 --dry-run -s -R < "${HERE}/patches/${p}" >/dev/null 2>&1; then echo "==> ${p} already applied"; continue; fi
        echo "==> Applying ${p}"; patch -p1 < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi
# Generate the build system (no configure in the source archive).  Run on the
# build host with the host autotools — produces a portable, target-agnostic
# configure that we then point at the cross compiler in build.sh.
if [ ! -f configure ]; then
    echo "==> autoreconf --force --install"
    autoreconf --force --install
fi
# autoreconf installs a fresh config.sub from the host automake that does not
# know the substrate OS triplet; teach it inline (same fixup as the other ports).
for cs in $(find "${TREE_DIR}" -name config.sub); do
    grep -q substrate "$cs" || sed -i 's/\(-sortix\* \)/\1| -substrate* /; s/\(| sortix\* \)/\1| substrate* /' "$cs"
done
echo "==> disasterparty ${VERSION} ready at ${TREE_DIR}"
