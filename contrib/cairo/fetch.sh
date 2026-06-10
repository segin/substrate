#!/bin/sh
# contrib/cairo/fetch.sh — cairo 1.16.0 (2D graphics; pango/gtk2 backend).
set -eu
VERSION="1.16.0"
TARBALL="cairo-${VERSION}.tar.xz"
URL="https://www.cairographics.org/releases/${TARBALL}"
SHA256="5e7b29b3f113ef870d1e3ecf8adf21f923396401604bda16d44be45e66052331"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"; TREE_DIR="${BUILD_DIR}/cairo-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"; curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
. "${HERE}/../substrate-autotools.sh"
substrate_config_sub_fix "${TREE_DIR}"
if [ -f "${HERE}/series" ]; then
    while IFS= read -r _p; do [ -n "${_p}" ] || continue
        patch -d "${TREE_DIR}" -p1 --dry-run -R -s -f < "${HERE}/patches/${_p}" >/dev/null 2>&1 \
            || patch -d "${TREE_DIR}" -p1 < "${HERE}/patches/${_p}"
    done < "${HERE}/series"
fi
echo "==> cairo ${VERSION} ready at ${TREE_DIR}"
