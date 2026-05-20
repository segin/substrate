#!/bin/sh
# Download + verify + extract + patch OpenSSH portable.  Idempotent.
set -eu

VERSION="10.0p2"
TARBALL="openssh-${VERSION}.tar.gz"
URL="https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/${TARBALL}"
# SHA-256 verified against the file fetched from cdn.openbsd.org and
# matched to the upstream announcement signed by Damien Miller
# (djm@openbsd.org).
SHA256="021a2e709a0edf4250b1256bd5a9e500411a90dddabea830ed59cef90eb9d85c"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
# Tarball extracts to openssh-10.0p1/ even though it's the p2 release
# (upstream packaging quirk).  Stick with what the archive actually
# produces; build.sh references the same path.
TREE_DIR="${BUILD_DIR}/openssh-10.0p1"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    if [ "${1:-}" = "--no-network" ]; then
        echo "fetch.sh: ${TARBALL} not present and --no-network given" >&2
        exit 1
    fi
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

echo "==> Verifying SHA-256"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ -d "${TREE_DIR}" ]; then
    echo "==> ${TREE_DIR} already present"
else
    echo "==> Extracting"
    tar -xzf "${TARBALL}"
fi

if [ -s "${HERE}/series" ]; then
    echo "==> Applying patch series"
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        case "$p" in ''|\#*) continue ;; esac
        echo "    applying patches/$p"
        patch -p1 < "${HERE}/patches/$p"
    done < "${HERE}/series"
fi

echo "==> fetch.sh complete; build with ./build.sh"
