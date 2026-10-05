#!/bin/sh
#
# contrib/sudo/fetch.sh — fetch the sudo tarball, verify, extract,
# apply patches.

set -eu

VERSION="1.9.17p2"
TARBALL="sudo-${VERSION}.tar.gz"
URL="https://www.sudo.ws/dist/${TARBALL}"
SHA256="4a38a1ab3adb1199257edc2a7c4a2bd714665eb605b04368843b06dada2cfcfb"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/sudo-${VERSION}"

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

# sudo's config.sub predates the substrate triple.
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

echo "==> sudo-${VERSION} tree ready at ${TREE_DIR}"
