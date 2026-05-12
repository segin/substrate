#!/bin/sh
#
# fetch.sh — download the pinned binutils tarball, verify sha256,
# extract it, and apply the substrate patch series.  Idempotent: re-running
# from a clean state produces a tree at $(pwd)/build/binutils-${VERSION}/
# ready for configure.
#
# Usage:
#   ./fetch.sh                 # download, verify, extract, patch
#   ./fetch.sh --no-network    # use a tarball already present in build/
#

set -eu

VERSION="2.46.0"
TARBALL="binutils-${VERSION}.tar.xz"
URL="https://ftp.gnu.org/gnu/binutils/${TARBALL}"
SHA256="d75a94f4d73e7a4086f7513e67e439e8fcdcbb726ffe63f4661744e6256b2cf2"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/binutils-${VERSION}"

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

echo "==> Extracting"
tar xf "${TARBALL}"

echo "==> Applying patch series"
cd "${TREE_DIR}"
while read -r p; do
    case "${p}" in ""|\#*) continue ;; esac
    echo "    - ${p}"
    patch -p1 --no-backup-if-mismatch -i "${HERE}/patches/${p}" >/dev/null
done < "${HERE}/series"

echo "==> Done.  Tree is at ${TREE_DIR}"
echo ""
echo "Next steps:"
echo "  mkdir build-i386-substrate && cd build-i386-substrate"
echo "  ${TREE_DIR}/configure \\"
echo "      --target=i386-unknown-substrate \\"
echo "      --prefix=/opt/substrate-toolchain \\"
echo "      --with-sysroot=\$(realpath ../../../dist) \\"
echo "      --disable-werror --disable-nls --disable-gdb \\"
echo "      --disable-gdbserver --disable-sim"
echo "  make -j\$(nproc)"
echo "  sudo make install"
