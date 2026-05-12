#!/bin/sh
#
# fetch.sh — download the pinned GCC tarball, verify sha256, extract,
# apply the substrate patch series.  Idempotent: re-running on an
# already-extracted tree blows it away first.
#
# Usage:  ./fetch.sh [--no-network]

set -eu

VERSION="16.1.0"
TARBALL="gcc-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/gcc/gcc-${VERSION}/${TARBALL}"
SHA256="50efb4d94c3397aff3b0d61a5abd748b4dd31d9d3f2ab7be05b171d36a510f79"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/gcc-${VERSION}"

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
    elif command -v wget >/dev/null 2>&1; then
        wget -O "${TARBALL}" "${URL}"
    else
        echo "fetch.sh: neither curl nor wget found" >&2
        exit 1
    fi
fi

echo "==> Verifying sha256"
ACTUAL=$(sha256sum "${TARBALL}" | awk '{print $1}')
if [ "${ACTUAL}" != "${SHA256}" ]; then
    echo "fetch.sh: sha256 mismatch on ${TARBALL}" >&2
    echo "  expected: ${SHA256}" >&2
    echo "  got:      ${ACTUAL}" >&2
    exit 1
fi

if [ -d "${TREE_DIR}" ]; then
    echo "==> Removing existing tree ${TREE_DIR}"
    rm -rf "${TREE_DIR}"
fi

echo "==> Extracting (this takes a minute — gcc is 1.3 GB)"
tar xf "${TARBALL}"

echo "==> Fetching gcc prerequisites (gmp / mpfr / mpc / isl)"
cd "${TREE_DIR}"
if [ -x ./contrib/download_prerequisites ]; then
    ./contrib/download_prerequisites
fi

echo "==> Applying patch series"
while read -r p; do
    case "${p}" in ""|\#*) continue ;; esac
    echo "    - ${p}"
    patch -p1 --no-backup-if-mismatch -i "${HERE}/patches/${p}" >/dev/null
done < "${HERE}/series"

# gcc's prerequisites (gmp / mpfr / mpc / isl / gettext) each ship
# their own copy of config.sub which is older than the top-level
# one and doesn't know about substrate.  Patch 0001 only touches
# the top-level config.sub; mirror it into the prereqs so their
# per-component configure scripts accept i386-unknown-substrate.
echo "==> Propagating top-level config.sub to bundled prereqs"
TOPSUB="$(pwd)/config.sub"
for f in $(find . -name config.sub | grep -v '^./config.sub$'); do
    cp "$TOPSUB" "$f"
done

echo "==> Done.  Tree is at ${TREE_DIR}"
echo ""
echo "Next: run ../build-toolchain.sh from contrib/, or for gcc"
echo "alone:    ./build.sh --stage=1"
