#!/bin/sh
# contrib/gtk1/fetch.sh — GTK+ 1.2.10 (bundled GDK; Xlib-only, no Cairo/Pango).
set -eu
VERSION="1.2.10"
TARBALL="gtk+-${VERSION}.tar.gz"
URL="https://download.gnome.org/sources/gtk+/1.2/${TARBALL}"
SHA256="3fb843ea671c89b909fd145fa09fd2276af3312e58cbab29ed1c93b462108c34"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/gtk+-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    curl -fSL -o "${TARBALL}" "${URL}"
fi
if [ "${SHA256}" != "REPLACE" ]; then echo "${SHA256}  ${TARBALL}" | sha256sum -c -; fi
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
BINU="$(ls -d "${HERE}"/../binutils/build/binutils-*/ 2>/dev/null | head -1)"
[ -n "${BINU}" ] || { echo "fetch.sh: need contrib/binutils fetched (for config.sub)" >&2; exit 1; }
cp -f "${BINU}/config.sub" "${BINU}/config.guess" "${TREE_DIR}/"
if [ -f "${HERE}/series" ]; then
    while IFS= read -r patch; do
        [ -n "${patch}" ] || continue
        if patch -d "${TREE_DIR}" -p1 --dry-run -R -s -f < "${HERE}/patches/${patch}" >/dev/null 2>&1; then
            echo "    (already applied: ${patch})"
        else
            echo "    applying ${patch}"; patch -d "${TREE_DIR}" -p1 < "${HERE}/patches/${patch}"
        fi
    done < "${HERE}/series"
fi
echo "==> gtk+ ${VERSION} ready at ${TREE_DIR}"
