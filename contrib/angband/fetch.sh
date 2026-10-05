#!/bin/sh
#
# contrib/angband/fetch.sh — fetch the Angband tarball, verify, extract,
# apply patches.

set -eu

VERSION="4.2.5"
TARBALL="Angband-${VERSION}.tar.gz"
URL="https://github.com/angband/angband/releases/download/${VERSION}/${TARBALL}"
SHA256="c4cacbdf28f726fcb1a0b30b8763100fb06f88dbb570e955232e41d83e0718a6"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/Angband-${VERSION}"

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

# config.sub predates the substrate triple.
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

echo "==> Angband-${VERSION} tree ready at ${TREE_DIR}"
