#!/bin/sh
# contrib/glib1/fetch.sh — GLib 1.2.10, the GTK+ 1.2 base library.
# Source-only fetch; build.sh cross-compiles.
set -eu
VERSION="1.2.10"
TARBALL="glib-${VERSION}.tar.gz"
URL="https://download.gnome.org/sources/glib/1.2/${TARBALL}"
SHA256="6e1ce7eedae713b11db82f11434d455d8a1379f783a79812cd2e05fc024a8d9f"
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${HERE}/build"
TREE_DIR="${BUILD_DIR}/glib-${VERSION}"
mkdir -p "${BUILD_DIR}"; cd "${BUILD_DIR}"
if [ ! -f "${TARBALL}" ]; then
    [ "${1:-}" = "--no-network" ] && { echo "fetch.sh: tarball missing" >&2; exit 1; }
    echo "==> Fetching ${URL}"
    curl -fSL -o "${TARBALL}" "${URL}"
fi
echo "${SHA256}  ${TARBALL}" | sha256sum -c -
[ -d "${TREE_DIR}" ] || { echo "==> Extracting"; tar xf "${TARBALL}"; }
# 2001-era config.{sub,guess} don't know the substrate triple — borrow the
# substrate-patched copies from the binutils port (toolchain prerequisite).
BINU="$(ls -d "${HERE}"/../binutils/build/binutils-*/ 2>/dev/null | head -1)"
[ -n "${BINU}" ] || { echo "fetch.sh: need contrib/binutils fetched (for config.sub)" >&2; exit 1; }
cp -f "${BINU}/config.sub" "${BINU}/config.guess" "${TREE_DIR}/"

# Apply the substrate patch series (idempotent: skip already-applied).
if [ -f "${HERE}/series" ]; then
    while IFS= read -r patch; do
        [ -n "${patch}" ] || continue
        if patch -d "${TREE_DIR}" -p1 --dry-run -R -s -f < "${HERE}/patches/${patch}" >/dev/null 2>&1; then
            echo "    (already applied: ${patch})"
        else
            echo "    applying ${patch}"
            patch -d "${TREE_DIR}" -p1 < "${HERE}/patches/${patch}"
        fi
    done < "${HERE}/series"
fi
echo "==> glib ${VERSION} ready at ${TREE_DIR}"
