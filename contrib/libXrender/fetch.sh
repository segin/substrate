#!/bin/sh
#
# contrib/libXrender/fetch.sh — fetch the libXrender tarball, verify, extract,
# apply the substrate patch series.

set -eu

VERSION="0.9.11"
TARBALL="libXrender-${VERSION}.tar.xz"
URL="https://www.x.org/releases/individual/lib/${TARBALL}"
SHA256="bc53759a3a83d1ff702fb59641b3d2f7c56e05051fa0cfa93501166fa782dc24"

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/libXrender-${VERSION}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${TARBALL}" "${URL}"
    else
        wget -O "${TARBALL}" "${URL}"
    fi
fi

echo "==> Verifying ${TARBALL}"
echo "${SHA256}  ${TARBALL}" | sha256sum -c -

if [ ! -d "${TREE_DIR}" ]; then
    echo "==> Extracting"
    tar xf "${TARBALL}"
fi

. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
if [ -f "${HERE}/series" ]; then
    while IFS= read -r _p; do
        [ -n "${_p}" ] || continue
        patch -d "${TREE_DIR}" -p1 --dry-run -R -s -f < "${HERE}/patches/${_p}" >/dev/null 2>&1 \
            || patch -d "${TREE_DIR}" -p1 < "${HERE}/patches/${_p}"
    done < "${HERE}/series"
fi

echo "==> libXrender ${VERSION} ready at ${TREE_DIR}"
