#!/bin/sh
#
# contrib/rogue/fetch.sh — fetch the Rogue 5.4.4 tarball, verify, extract,
# apply patches.
#
# The Roguelike Restoration Project's own site (rogue.rogueforge.net) no
# longer answers; the tarball comes from Fedora's lookaside cache, which is
# addressed by the file's MD5 and so cannot change under the name.

set -eu

VERSION="5.4.4"
TARBALL="rogue${VERSION}-src.tar.gz"
URL="https://src.fedoraproject.org/repo/pkgs/rogue/${TARBALL}/033288f46444b06814c81ea69d96e075/${TARBALL}"
SHA256="7d37a61fc098bda0e6fac30799da347294067e8e079e4b40d6c781468e08e8a1"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/rogue${VERSION}"

. "${HERE}/../substrate-autotools.sh"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    if [ "${1:-}" = "--no-network" ]; then
        echo "fetch.sh: ${TARBALL} not present and --no-network given" >&2
        exit 1
    fi
    echo "==> Fetching ${URL}"
    curl -fSL --retry 3 --retry-delay 3 --retry-all-errors -o "${TARBALL}" "${URL}"
fi

echo "==> Verifying sha256"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ -d "${TREE_DIR}" ]; then
    echo "==> Removing existing tree ${TREE_DIR}"
    rm -rf "${TREE_DIR}"
fi

echo "==> Extracting"
tar xf "${TARBALL}"

# config.sub is from 2007.
substrate_config_sub_fix "${TREE_DIR}"

if [ -f "${HERE}/series" ]; then
    echo "==> Applying patch series"
    cd "${TREE_DIR}"
    while IFS= read -r p; do
        [ -z "${p}" ] && continue
        case "${p}" in \#*) continue ;; esac
        echo "    - ${p}"
        patch -p1 --no-backup-if-mismatch < "${HERE}/patches/${p}"
    done < "${HERE}/series"
fi

echo "==> rogue ${VERSION} tree ready at ${TREE_DIR}"
